#include "agent_runtime.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace magda::agent {
namespace {

ToolResult makeErrorResult(const ToolCall& call, std::uint64_t revision, juce::String code,
                           juce::String message, juce::var details = {}) {
    return {.callId = call.id,
            .toolName = call.name,
            .success = false,
            .content = {},
            .error = ToolError{.code = std::move(code),
                               .message = std::move(message),
                               .details = std::move(details)},
            .projectRevision = revision,
            .mutated = false};
}

juce::String callFingerprint(const ToolCall& call, std::uint64_t revision) {
    return call.name + "\n" + juce::JSON::toString(call.arguments, true) + "\n@" +
           juce::String(revision);
}

const ToolDefinition* findTool(const AgentDefinition& definition, const juce::String& name) {
    const auto it = std::find_if(definition.tools.begin(), definition.tools.end(),
                                 [&name](const ToolDefinition& tool) { return tool.name == name; });
    return it == definition.tools.end() ? nullptr : &*it;
}

}  // namespace

AgentRuntime::AgentRuntime(Model& model, ToolExecutor& executor, ApprovalHook approvalHook, Now now)
    : model_(model),
      executor_(executor),
      approvalHook_(std::move(approvalHook)),
      now_(std::move(now)) {}

RunResult AgentRuntime::run(const AgentDefinition& definition, AgentRunInput input,
                            CancellationToken cancellation) {
    RunResult result;
    result.state = RunState::Running;
    result.finalRevision = input.projectRevision;
    result.conversation = std::move(input.conversation);
    result.conversation.push_back(
        {.role = ConversationRole::User, .text = std::move(input.userMessage)});

    const auto startedAt = now_();
    const auto deadline = startedAt + definition.budget.maxWallTime;
    std::map<juce::String, std::size_t> identicalCallCounts;
    std::set<juce::String> seenToolCallIds;

    const auto stop = [&](TerminalReason reason, juce::String detail = {}) {
        result.state =
            reason == TerminalReason::Completed ? RunState::Completed : RunState::Stopped;
        result.reason = reason;
        result.detail = std::move(detail);
        return result;
    };

    const auto timedOut = [&] { return now_() >= deadline; };

    while (true) {
        if (cancellation.isCancellationRequested())
            return stop(TerminalReason::Cancelled, "Run cancelled");
        if (timedOut())
            return stop(TerminalReason::TimeLimit, "Wall-time budget exhausted");
        if (result.steps >= definition.budget.maxSteps)
            return stop(TerminalReason::StepLimit, "Model-step budget exhausted");
        if (result.tokensUsed >= definition.budget.maxTokens)
            return stop(TerminalReason::TokenLimit, "Token budget exhausted");

        ModelRequest request{
            .agentId = definition.id,
            .systemPrompt = definition.systemPrompt,
            .baselineContext = definition.baselineContext,
            .tools = definition.tools,
            .conversation = result.conversation,
            .projectRevision = result.finalRevision,
            .remainingSteps = definition.budget.maxSteps - result.steps,
            .remainingTokens = definition.budget.maxTokens > result.tokensUsed
                                   ? definition.budget.maxTokens - result.tokensUsed
                                   : 0,
            .remainingMutations = definition.budget.maxMutations > result.mutations
                                      ? definition.budget.maxMutations - result.mutations
                                      : 0,
        };

        ModelResponse response;
        try {
            response = model_.generate(request, cancellation);
        } catch (const std::exception& error) {
            return stop(TerminalReason::ModelError, error.what());
        } catch (...) {
            return stop(TerminalReason::ModelError, "Model threw an unknown exception");
        }

        ++result.steps;
        result.tokensUsed += response.tokensUsed;
        StepTrace trace{.index = result.steps,
                        .revisionBefore = result.finalRevision,
                        .revisionAfter = result.finalRevision,
                        .tokensUsed = response.tokensUsed,
                        .assistantText = response.text,
                        .toolCalls = response.toolCalls};

        if (cancellation.isCancellationRequested()) {
            result.trace.push_back(std::move(trace));
            return stop(TerminalReason::Cancelled, "Run cancelled");
        }
        if (timedOut()) {
            result.trace.push_back(std::move(trace));
            return stop(TerminalReason::TimeLimit, "Wall-time budget exhausted");
        }
        if (!response.success) {
            result.trace.push_back(std::move(trace));
            return stop(TerminalReason::ModelError,
                        response.error.isNotEmpty() ? response.error : "Model request failed");
        }
        if (result.tokensUsed > definition.budget.maxTokens) {
            result.trace.push_back(std::move(trace));
            return stop(TerminalReason::TokenLimit, "Token budget exhausted");
        }

        result.conversation.push_back({.role = ConversationRole::Assistant,
                                       .text = response.text,
                                       .toolCalls = response.toolCalls});

        if (response.toolCalls.empty()) {
            result.trace.push_back(std::move(trace));
            if (response.text.isEmpty())
                return stop(TerminalReason::InvalidModelResponse,
                            "Model returned neither final text nor tool calls");
            result.finalText = std::move(response.text);
            return stop(TerminalReason::Completed);
        }

        if (response.toolCallExecution == ModelResponse::ToolCallExecution::Parallel &&
            response.toolCalls.size() > 1 &&
            std::any_of(response.toolCalls.begin(), response.toolCalls.end(),
                        [&](const ToolCall& call) {
                            const auto* tool = findTool(definition, call.name);
                            return tool != nullptr && tool->access == ToolAccess::Mutation;
                        })) {
            result.trace.push_back(std::move(trace));
            return stop(TerminalReason::UnsafeParallelMutation,
                        "Parallel tool batches containing mutations are not safe; retry "
                        "sequentially");
        }

        for (const auto& call : response.toolCalls) {
            if (cancellation.isCancellationRequested()) {
                result.trace.push_back(std::move(trace));
                return stop(TerminalReason::Cancelled, "Run cancelled");
            }
            if (timedOut()) {
                result.trace.push_back(std::move(trace));
                return stop(TerminalReason::TimeLimit, "Wall-time budget exhausted");
            }

            const auto fingerprint = callFingerprint(call, result.finalRevision);
            auto& identicalCount = identicalCallCounts[fingerprint];
            ++identicalCount;
            if (identicalCount > definition.budget.maxIdenticalCallsAtRevision) {
                result.trace.push_back(std::move(trace));
                return stop(TerminalReason::RepeatedToolCall,
                            "Repeated tool call at the same project revision: " + call.name);
            }

            ToolResult toolResult;
            const auto* tool = findTool(definition, call.name);
            if (call.id.isEmpty()) {
                toolResult = makeErrorResult(call, result.finalRevision, "invalid_tool_call",
                                             "Tool call ID must not be empty");
            } else if (!seenToolCallIds.insert(call.id).second) {
                toolResult = makeErrorResult(call, result.finalRevision, "duplicate_tool_call_id",
                                             "Tool call ID was already used in this run");
            } else if (tool == nullptr) {
                toolResult = makeErrorResult(call, result.finalRevision, "tool_not_allowed",
                                             "Tool is not allowed for this agent surface");
            } else {
                const bool isMutation = tool->access == ToolAccess::Mutation;
                if (isMutation && result.mutations >= definition.budget.maxMutations) {
                    result.trace.push_back(std::move(trace));
                    return stop(TerminalReason::MutationLimit, "Mutation budget exhausted");
                }

                if (isMutation && approvalHook_) {
                    try {
                        const auto approval =
                            approvalHook_({.tool = *tool,
                                           .call = call,
                                           .projectRevision = result.finalRevision,
                                           .permissions = input.permissions});
                        if (!approval.approved) {
                            toolResult = makeErrorResult(
                                call, result.finalRevision, "approval_denied",
                                approval.reason.isNotEmpty() ? approval.reason
                                                             : "Mutation was not approved");
                        }
                    } catch (const std::exception& error) {
                        toolResult = makeErrorResult(call, result.finalRevision, "approval_error",
                                                     error.what());
                    } catch (...) {
                        toolResult = makeErrorResult(call, result.finalRevision, "approval_error",
                                                     "Approval hook threw an unknown exception");
                    }
                }

                if (!toolResult.error.has_value()) {
                    if (cancellation.isCancellationRequested()) {
                        result.trace.push_back(std::move(trace));
                        return stop(TerminalReason::Cancelled, "Run cancelled");
                    }
                    if (timedOut()) {
                        result.trace.push_back(std::move(trace));
                        return stop(TerminalReason::TimeLimit, "Wall-time budget exhausted");
                    }
                    if (isMutation)
                        ++result.mutations;
                    try {
                        toolResult = executor_.execute({.call = call,
                                                        .expectedRevision = result.finalRevision,
                                                        .permissions = input.permissions,
                                                        .deadline = deadline},
                                                       cancellation);
                    } catch (const std::exception& error) {
                        toolResult = makeErrorResult(call, result.finalRevision, "execution_error",
                                                     error.what());
                    } catch (...) {
                        toolResult = makeErrorResult(call, result.finalRevision, "execution_error",
                                                     "Tool executor threw an unknown exception");
                    }

                    toolResult.callId = call.id;
                    toolResult.toolName = call.name;
                    if (toolResult.projectRevision < result.finalRevision) {
                        toolResult =
                            makeErrorResult(call, result.finalRevision, "revision_regressed",
                                            "Tool result returned an older project revision");
                    }
                }
            }

            result.finalRevision = toolResult.projectRevision;
            trace.revisionAfter = result.finalRevision;
            trace.toolResults.push_back(toolResult);
            result.conversation.push_back(
                {.role = ConversationRole::Tool, .toolResult = std::move(toolResult)});
        }

        result.trace.push_back(std::move(trace));
    }
}

const char* toString(TerminalReason reason) {
    switch (reason) {
        case TerminalReason::Completed:
            return "completed";
        case TerminalReason::Cancelled:
            return "cancelled";
        case TerminalReason::StepLimit:
            return "step_limit";
        case TerminalReason::TokenLimit:
            return "token_limit";
        case TerminalReason::MutationLimit:
            return "mutation_limit";
        case TerminalReason::TimeLimit:
            return "time_limit";
        case TerminalReason::RepeatedToolCall:
            return "repeated_tool_call";
        case TerminalReason::UnsafeParallelMutation:
            return "unsafe_parallel_mutation";
        case TerminalReason::ModelError:
            return "model_error";
        case TerminalReason::InvalidModelResponse:
            return "invalid_model_response";
    }
    return "unknown";
}

}  // namespace magda::agent
