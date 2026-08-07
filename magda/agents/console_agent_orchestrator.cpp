#include "console_agent_orchestrator.hpp"

#include <utility>

#include "../daw/core/ClipManager.hpp"
#include "../daw/core/Config.hpp"
#include "../daw/core/LLMClientProvider.hpp"
#include "../daw/core/MixAnalysisService.hpp"
#include "../daw/core/TrackManager.hpp"
#include "automation_agent.hpp"
#include "automation_executor.hpp"
#include "command_agent.hpp"
#include "drummer_agent.hpp"
#include "dsl_interpreter.hpp"
#include "instruction_executor.hpp"
#include "mixing_agent.hpp"
#include "music_agent.hpp"

namespace magda::agent {
namespace {

void emit(const ConsoleRunObserver& observer, ConsoleRunEventType type,
          ConsoleRunStream stream = ConsoleRunStream::Primary, const juce::String& text = {}) {
    if (observer)
        observer({.type = type, .stream = stream, .text = text});
}

ConsoleRunOutput errorOutput(std::string error) {
    ConsoleRunOutput output;
    output.error = std::move(error);
    return output;
}

}  // namespace

bool ConsoleRunOutput::hasContent() const {
    return !dslCode.empty() || !musicInstructions.empty() || !automationInstructions.empty() ||
           !prose.empty();
}

ConsoleAgentOrchestrator::ConsoleAgentOrchestrator(CommandAgent& command, MusicAgent& music,
                                                   AutomationAgent& automation,
                                                   DrummerAgent& drummer)
    : command_(&command), music_(&music), automation_(&automation), drummer_(&drummer) {
    workflows_.command = [&command](const ConsoleRunRequest& request,
                                    const ConsoleRunObserver& observer,
                                    const CancellationToken& cancellation) {
        if (cancellation.isCancellationRequested())
            return ConsoleRunOutput{.cancelled = true};
        // The on-device command model is a fixed intent+slot tagger trained on
        // a single bare command: prepending conversation history shifts every
        // span and corrupts the intent. Feed it the raw request; every other
        // backend gets the full contextual prompt.
        const bool fastInference =
            Config::getInstance().getAgentLLMConfig(role::COMMAND).provider ==
            provider::FAST_INFERENCE;
        const std::string prompt = fastInference ? request.userMessage : composePrompt(request);
        auto result = command.generateStreaming(prompt, [&](const juce::String& token) {
            emit(observer, ConsoleRunEventType::Token, ConsoleRunStream::Primary, token);
            return !cancellation.isCancellationRequested();
        });
        if (result.hasError)
            return errorOutput(result.error);
        ConsoleRunOutput output;
        output.dslCode = std::move(result.dslOutput);
        return output;
    };

    workflows_.music = [&music](const ConsoleRunRequest& request,
                                const ConsoleRunObserver& observer,
                                const CancellationToken& cancellation) {
        if (cancellation.isCancellationRequested())
            return ConsoleRunOutput{.cancelled = true};
        auto result =
            music.generateStreaming(composePrompt(request), [&](const juce::String& token) {
                emit(observer, ConsoleRunEventType::Token, ConsoleRunStream::Primary, token);
                return !cancellation.isCancellationRequested();
            });
        if (result.hasError)
            return errorOutput(result.error);
        ConsoleRunOutput output;
        output.musicInstructions = std::move(result.instructions);
        output.musicDescription = std::move(result.description);
        return output;
    };

    workflows_.automation = [&automation](const ConsoleRunRequest& request,
                                          const ConsoleRunObserver& observer,
                                          const CancellationToken& cancellation) {
        if (cancellation.isCancellationRequested())
            return ConsoleRunOutput{.cancelled = true};
        auto result =
            automation.generateStreaming(composePrompt(request), [&](const juce::String& token) {
                emit(observer, ConsoleRunEventType::Token, ConsoleRunStream::Primary, token);
                return !cancellation.isCancellationRequested();
            });
        if (result.hasError)
            return errorOutput(result.error);
        ConsoleRunOutput output;
        output.automationInstructions = std::move(result.instructions);
        return output;
    };

    workflows_.drummer = [&drummer](const ConsoleRunRequest& request,
                                    const ConsoleRunObserver& observer,
                                    const CancellationToken& cancellation) {
        if (cancellation.isCancellationRequested())
            return ConsoleRunOutput{.cancelled = true};
        auto prompt = composePrompt(request);
        if (!request.drummerContext.empty())
            prompt = request.drummerContext + "\n" + prompt;
        auto result = drummer.generateStreaming(prompt, [&](const juce::String& token) {
            emit(observer, ConsoleRunEventType::Token, ConsoleRunStream::Primary, token);
            return !cancellation.isCancellationRequested();
        });
        if (result.hasError)
            return errorOutput(result.error);
        ConsoleRunOutput output;
        output.musicInstructions = std::move(result.instructions);
        output.musicDescription = std::move(result.description);
        return output;
    };

    workflows_.mixing = [](const ConsoleRunRequest& request, const ConsoleRunObserver& observer,
                           const CancellationToken& cancellation) {
        auto cached = MixAnalysisService::getInstance().latest();
        if (!cached.has_value() || cached->tracks.empty())
            return errorOutput(
                "No mix analysis yet. Run one from the mixer's Analyze button first.");

        MixAnalysisAgent::Input input;
        input.measurements = std::move(*cached);
        input.question = request.userMessage;
        input.priorContext = request.priorConversation;
        MixAnalysisAgent agent;
        auto result = agent.generateStreaming(input, [&](const juce::String& token) {
            emit(observer, ConsoleRunEventType::Token, ConsoleRunStream::Primary, token);
            return !cancellation.isCancellationRequested();
        });
        if (result.hasError)
            return errorOutput(result.error.empty() ? "Mix analysis failed." : result.error);
        ConsoleRunOutput output;
        output.prose = std::move(result.analysis);
        return output;
    };
}

ConsoleAgentOrchestrator::ConsoleAgentOrchestrator(Workflows workflows)
    : workflows_(std::move(workflows)) {}

ConsoleRunOutput ConsoleAgentOrchestrator::run(const ConsoleRunRequest& request,
                                               ConsoleRunObserver observer,
                                               CancellationToken cancellation) {
    const CancellationToken combined([this, cancellation] {
        return cancelled_.load() || cancellation.isCancellationRequested();
    });
    emit(observer, ConsoleRunEventType::Started);
    if (combined.isCancellationRequested()) {
        emit(observer, ConsoleRunEventType::Cancelled);
        return {.cancelled = true};
    }

    ConsoleRunOutput output;
    const decltype(workflows_.command)* workflow = nullptr;
    switch (request.surface) {
        case AgentSurfaceId::Arrangement:
        case AgentSurfaceId::DevicePanel:
            workflow = &workflows_.command;
            break;
        case AgentSurfaceId::PianoRoll:
            workflow = &workflows_.music;
            break;
        case AgentSurfaceId::Automation:
            workflow = &workflows_.automation;
            break;
        case AgentSurfaceId::Drummer:
            workflow = &workflows_.drummer;
            break;
        case AgentSurfaceId::Mixer:
        case AgentSurfaceId::Master:
            workflow = &workflows_.mixing;
            break;
        case AgentSurfaceId::Session:
            output = errorOutput("The session agent isn't available yet.");
            break;
    }
    if (workflow != nullptr)
        output = runWorkflow(*workflow, request, observer, combined);

    if (combined.isCancellationRequested() || output.cancelled) {
        output.cancelled = true;
        emit(observer, ConsoleRunEventType::Cancelled);
    } else if (!output.error.empty() && !output.hasContent()) {
        emit(observer, ConsoleRunEventType::Failed, ConsoleRunStream::Primary, output.error);
    } else {
        emit(observer, ConsoleRunEventType::Completed);
    }
    return output;
}

void ConsoleAgentOrchestrator::cancel() {
    cancelled_.store(true);
    if (command_ != nullptr)
        command_->requestCancel();
    if (music_ != nullptr)
        music_->requestCancel();
    if (automation_ != nullptr)
        automation_->requestCancel();
    if (drummer_ != nullptr)
        drummer_->requestCancel();
}

void ConsoleAgentOrchestrator::resetCancellation() {
    cancelled_.store(false);
    if (command_ != nullptr)
        command_->resetCancel();
    if (music_ != nullptr)
        music_->resetCancel();
    if (automation_ != nullptr)
        automation_->resetCancel();
    if (drummer_ != nullptr)
        drummer_->resetCancel();
}

std::string ConsoleAgentOrchestrator::composePrompt(const ConsoleRunRequest& request) {
    const auto contextualMessage =
        request.priorConversation.empty()
            ? request.userMessage
            : request.priorConversation + "\nUser request: " + request.userMessage;
    auto prompt = contextualMessage;
    if (!request.midiContext.empty()) {
        prompt =
            request.midiContext + "\n\nConversation and current request:\n" + contextualMessage;
    }
    if (request.reviseTargetClipId != INVALID_CLIP_ID) {
        prompt =
            "[OUTPUT_MODE]\nRevise the most recent AI-created MIDI clip in place. Return the "
            "complete desired replacement, not only the changed notes. Do not create a new track "
            "or clip.\n[/OUTPUT_MODE]\n\n" +
            prompt;
    }
    return prompt;
}

ConsoleRunOutput ConsoleAgentOrchestrator::runWorkflow(
    const std::function<ConsoleRunOutput(const ConsoleRunRequest&, const ConsoleRunObserver&,
                                         const CancellationToken&)>& workflow,
    const ConsoleRunRequest& request, const ConsoleRunObserver& observer,
    const CancellationToken& cancellation) {
    if (!workflow)
        return errorOutput("Agent workflow is unavailable.");
    if (cancellation.isCancellationRequested())
        return {.cancelled = true};
    return workflow(request, observer, cancellation);
}

ConsoleExecutionResult ConsoleAgentResultExecutor::execute(ConsoleRunOutput output,
                                                           ClipId reviseTargetClipId) {
    ConsoleExecutionResult result;
    if (!output.error.empty() && !output.hasContent()) {
        result.response = std::move(output.error);
        return result;
    }

    ClipManager::BatchScope clipBatch;
    TrackManager::BatchScope trackBatch;
    int commandClipId = -1;

    if (!output.dslCode.empty()) {
        dsl::Interpreter interpreter(api_);
        if (interpreter.execute(output.dslCode.c_str())) {
            auto results = interpreter.getResults().toStdString();
            result.response = results.empty() ? "OK" : results;
            commandClipId = interpreter.getCurrentClipId();
        } else {
            result.response = "Error: " + std::string(interpreter.getError());
        }
    }

    if (!output.musicInstructions.empty()) {
        if (!output.musicDescription.empty()) {
            if (!result.response.empty())
                result.response += "\n";
            result.response += output.musicDescription;
        }
        InstructionExecutor executor(api_);
        const int seedClipId =
            reviseTargetClipId != INVALID_CLIP_ID ? reviseTargetClipId : commandClipId;
        executor.setSeedClipId(seedClipId);
        executor.setReplaceSeedClipContents(reviseTargetClipId != INVALID_CLIP_ID);
        if (executor.execute(output.musicInstructions)) {
            result.generatedMidiClipId = executor.getCurrentClipId();
            if (reviseTargetClipId == INVALID_CLIP_ID && !output.musicDescription.empty() &&
                executor.getCurrentClipId() >= 0) {
                constexpr int kMaxClipNameLen = 40;
                juce::String clipName(output.musicDescription);
                const auto clausePos = clipName.indexOfAnyOf(".,;");
                if (clausePos > 0 && clausePos < kMaxClipNameLen)
                    clipName = clipName.substring(0, clausePos);
                if (clipName.length() > kMaxClipNameLen)
                    clipName =
                        clipName.substring(0, kMaxClipNameLen).trim() + juce::String::fromUTF8("…");
                ClipManager::getInstance().setClipName(executor.getCurrentClipId(),
                                                       clipName.trim());
            }
            auto results = executor.getResults().toStdString();
            if (!result.response.empty())
                result.response += "\n";
            result.response += results.empty() ? "OK" : results;
        } else {
            if (!result.response.empty())
                result.response += "\n";
            result.response += "Error: " + executor.getError().toStdString();
        }
    }

    if (!output.automationInstructions.empty()) {
        AutomationExecutor executor(api_);
        if (executor.execute(output.automationInstructions)) {
            auto results = executor.getResults().toStdString();
            if (!result.response.empty())
                result.response += "\n";
            result.response += results.empty() ? "OK" : results;
        } else {
            if (!result.response.empty())
                result.response += "\n";
            result.response += "Error: " + executor.getError().toStdString();
        }
    }

    if (!output.prose.empty()) {
        if (!result.response.empty())
            result.response += "\n";
        result.response += output.prose;
    }
    if (!output.error.empty())
        result.response += "\n[Warning] " + output.error;
    return result;
}

}  // namespace magda::agent
