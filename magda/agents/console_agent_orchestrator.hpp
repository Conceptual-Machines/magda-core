#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../daw/core/ClipTypes.hpp"
#include "../daw/core/ConsoleRouting.hpp"
#include "agent_runtime.hpp"
#include "automation_parser.hpp"
#include "compact_parser.hpp"

namespace magda {

class AutomationAgent;
class CommandAgent;
class DrummerAgent;
class MagdaApi;
class MusicAgent;

namespace agent {

enum class ConsoleRunEventType { Started, Token, Completed, Failed, Cancelled };
enum class ConsoleRunStream { Primary, Command, Music };

struct ConsoleRunEvent {
    ConsoleRunEventType type = ConsoleRunEventType::Started;
    ConsoleRunStream stream = ConsoleRunStream::Primary;
    juce::String text;
};

using ConsoleRunObserver = std::function<void(const ConsoleRunEvent&)>;

struct ConsoleRunRequest {
    AgentSurfaceId surface = AgentSurfaceId::Arrangement;
    std::string userMessage;
    std::string priorConversation;
    std::string midiContext;
    std::string drummerContext;
    ClipId reviseTargetClipId = INVALID_CLIP_ID;
};

struct ConsoleRunOutput {
    std::string dslCode;
    std::vector<Instruction> musicInstructions;
    std::string musicDescription;
    std::vector<AutoInstruction> automationInstructions;
    std::string prose;
    std::string error;
    bool cancelled = false;

    bool hasContent() const;
};

/**
 * Production orchestrator for context-selected console agent workflows.
 *
 * It owns surface-based agent selection, prompt composition, streaming events,
 * cancellation, and completion semantics while constrained DSL/IR workflows
 * remain available as bounded extensions.
 */
class ConsoleAgentOrchestrator {
  public:
    struct Workflows {
        std::function<ConsoleRunOutput(const ConsoleRunRequest&, const ConsoleRunObserver&,
                                       const CancellationToken&)>
            command;
        std::function<ConsoleRunOutput(const ConsoleRunRequest&, const ConsoleRunObserver&,
                                       const CancellationToken&)>
            music;
        std::function<ConsoleRunOutput(const ConsoleRunRequest&, const ConsoleRunObserver&,
                                       const CancellationToken&)>
            automation;
        std::function<ConsoleRunOutput(const ConsoleRunRequest&, const ConsoleRunObserver&,
                                       const CancellationToken&)>
            drummer;
        std::function<ConsoleRunOutput(const ConsoleRunRequest&, const ConsoleRunObserver&,
                                       const CancellationToken&)>
            mixing;
    };

    ConsoleAgentOrchestrator(CommandAgent& command, MusicAgent& music, AutomationAgent& automation,
                             DrummerAgent& drummer);
    explicit ConsoleAgentOrchestrator(Workflows workflows);

    ConsoleRunOutput run(const ConsoleRunRequest& request, ConsoleRunObserver observer = {},
                         CancellationToken cancellation = {});

    void cancel();
    void resetCancellation();

    static std::string composePrompt(const ConsoleRunRequest& request);

  private:
    ConsoleRunOutput runWorkflow(
        const std::function<ConsoleRunOutput(const ConsoleRunRequest&, const ConsoleRunObserver&,
                                             const CancellationToken&)>& workflow,
        const ConsoleRunRequest& request, const ConsoleRunObserver& observer,
        const CancellationToken& cancellation);

    Workflows workflows_;
    CommandAgent* command_ = nullptr;
    MusicAgent* music_ = nullptr;
    AutomationAgent* automation_ = nullptr;
    DrummerAgent* drummer_ = nullptr;
    std::atomic<bool> cancelled_{false};
};

struct ConsoleExecutionResult {
    std::string response;
    ClipId generatedMidiClipId = INVALID_CLIP_ID;
};

/** Execute constrained workflow artifacts on the message thread. */
class ConsoleAgentResultExecutor {
  public:
    explicit ConsoleAgentResultExecutor(MagdaApi& api) : api_(api) {}

    ConsoleExecutionResult execute(ConsoleRunOutput output,
                                   ClipId reviseTargetClipId = INVALID_CLIP_ID);

  private:
    MagdaApi& api_;
};

}  // namespace agent
}  // namespace magda
