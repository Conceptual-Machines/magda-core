#include "agent_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
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

bool validConfidence(float confidence) {
    return std::isfinite(confidence) && confidence >= 0.0f && confidence <= 1.0f;
}

}  // namespace

AgentRuntime::AgentRuntime(Model& model, ToolExecutor& executor, ApprovalHook approvalHook, Now now,
                           FastInferencePolicy* fastInferencePolicy)
    : model_(model),
      executor_(executor),
      approvalHook_(std::move(approvalHook)),
      now_(std::move(now)),
      fastInferencePolicy_(fastInferencePolicy) {}

RunResult AgentRuntime::run(const AgentDefinition& definition, AgentRunInput input,
                            CancellationToken cancellation) {
    RunResult result;
    result.state = RunState::Running;
    result.finalRevision = input.projectRevision;
    const auto startedAt = now_();
    const auto deadline = startedAt + definition.budget.maxWallTime;

    AgentDefinition effectiveDefinition = definition;
    std::optional<ModelResponse> directPlan;
    if (definition.fastInference.enabled && fastInferencePolicy_ != nullptr &&
        !cancellation.isCancellationRequested()) {
        result.fastInference.evaluated = true;
        const auto inferenceStartedAt = now_();
        FastInferenceRequest request{
            .userMessage = input.userMessage,
            .agentId = definition.id,
            .contextFeatures = input.fastInferenceContext,
        };
        request.availableOperations.reserve(definition.tools.size());
        for (const auto& tool : definition.tools)
            request.availableOperations.push_back(tool.name);

        try {
            auto advice = fastInferencePolicy_->evaluate(request, cancellation);
            result.fastInference.predictedDecision = advice.decision;
            result.fastInference.confidence = advice.confidence;

            if (!validConfidence(advice.confidence)) {
                result.fastInference.fallbackReason = "Policy returned an invalid confidence";
            } else if (advice.decision == FastInferenceDecision::DirectPlan) {
                std::vector<ToolCall> calls;
                std::vector<juce::String> selectedTools;
                calls.reserve(advice.operations.size());
                selectedTools.reserve(advice.operations.size());
                bool eligible = !advice.operations.empty();
                for (std::size_t i = 0; i < advice.operations.size() && eligible; ++i) {
                    const auto& candidate = advice.operations[i];
                    const auto threshold =
                        definition.fastInference.directPlanConfidenceByOperation.find(
                            candidate.operationId);
                    if (candidate.operationId.isEmpty() ||
                        findTool(definition, candidate.operationId) == nullptr) {
                        result.fastInference.fallbackReason =
                            "Direct plan predicted an unavailable operation";
                        eligible = false;
                    } else if (!validConfidence(candidate.confidence) ||
                               threshold ==
                                   definition.fastInference.directPlanConfidenceByOperation.end() ||
                               !validConfidence(threshold->second) ||
                               candidate.confidence < threshold->second) {
                        result.fastInference.fallbackReason =
                            "Direct plan did not meet the operation confidence policy";
                        eligible = false;
                    } else {
                        const auto id = "fast-inference-" + juce::String(i + 1);
                        calls.push_back({.id = id,
                                         .name = candidate.operationId,
                                         .arguments = candidate.arguments});
                        selectedTools.push_back(candidate.operationId);
                    }
                }
                if (eligible) {
                    directPlan = ModelResponse{.success = true, .toolCalls = std::move(calls)};
                    result.fastInference.appliedDecision = FastInferenceDecision::DirectPlan;
                    result.fastInference.selectedTools = std::move(selectedTools);
                } else if (result.fastInference.fallbackReason.isEmpty()) {
                    result.fastInference.fallbackReason = "Direct plan contained no operations";
                }
            } else if (advice.decision == FastInferenceDecision::NarrowAgentTools &&
                       definition.fastInference.allowToolNarrowing &&
                       definition.fastInference.maxNarrowedTools > 0 &&
                       validConfidence(definition.fastInference.minimumNarrowingConfidence)) {
                auto candidates = advice.operations;
                std::stable_sort(
                    candidates.begin(), candidates.end(),
                    [](const OperationCandidate& left, const OperationCandidate& right) {
                        const auto leftScore =
                            validConfidence(left.confidence) ? left.confidence : -1.0f;
                        const auto rightScore =
                            validConfidence(right.confidence) ? right.confidence : -1.0f;
                        return leftScore > rightScore;
                    });
                std::set<juce::String> selected;
                for (const auto& candidate : candidates) {
                    if (selected.size() >= definition.fastInference.maxNarrowedTools)
                        break;
                    if (!validConfidence(candidate.confidence) ||
                        candidate.confidence <
                            definition.fastInference.minimumNarrowingConfidence ||
                        findTool(definition, candidate.operationId) == nullptr)
                        continue;
                    selected.insert(candidate.operationId);
                }
                if (!selected.empty()) {
                    effectiveDefinition.tools.erase(
                        std::remove_if(effectiveDefinition.tools.begin(),
                                       effectiveDefinition.tools.end(),
                                       [&](const ToolDefinition& tool) {
                                           return selected.find(tool.name) == selected.end();
                                       }),
                        effectiveDefinition.tools.end());
                    for (const auto& tool : effectiveDefinition.tools)
                        result.fastInference.selectedTools.push_back(tool.name);
                    result.fastInference.appliedDecision = FastInferenceDecision::NarrowAgentTools;
                } else {
                    result.fastInference.fallbackReason =
                        "Tool narrowing did not select an available operation";
                }
            } else {
                result.fastInference.fallbackReason = "Policy deferred to the general agent";
            }
        } catch (const std::exception& error) {
            result.fastInference.fallbackReason =
                "Fast inference failed: " + juce::String(error.what());
        } catch (...) {
            result.fastInference.fallbackReason = "Fast inference failed with an unknown error";
        }
        result.fastInference.latency =
            std::chrono::duration_cast<std::chrono::microseconds>(now_() - inferenceStartedAt);
    }

    result.conversation = std::move(input.conversation);
    result.conversation.push_back(
        {.role = ConversationRole::User, .text = std::move(input.userMessage)});

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
        if (result.steps >= effectiveDefinition.budget.maxSteps)
            return stop(TerminalReason::StepLimit, "Model-step budget exhausted");
        if (result.tokensUsed >= effectiveDefinition.budget.maxTokens)
            return stop(TerminalReason::TokenLimit, "Token budget exhausted");

        ModelRequest request{
            .agentId = effectiveDefinition.id,
            .systemPrompt = effectiveDefinition.systemPrompt,
            .baselineContext = effectiveDefinition.baselineContext,
            .tools = effectiveDefinition.tools,
            .conversation = result.conversation,
            .projectRevision = result.finalRevision,
            .remainingSteps = effectiveDefinition.budget.maxSteps - result.steps,
            .remainingTokens = effectiveDefinition.budget.maxTokens > result.tokensUsed
                                   ? effectiveDefinition.budget.maxTokens - result.tokensUsed
                                   : 0,
            .remainingMutations = effectiveDefinition.budget.maxMutations > result.mutations
                                      ? effectiveDefinition.budget.maxMutations - result.mutations
                                      : 0,
        };

        const bool isDirectPlanStep = directPlan.has_value();
        ModelResponse response;
        if (isDirectPlanStep) {
            response = std::move(*directPlan);
            directPlan.reset();
        } else {
            try {
                response = model_.generate(request, cancellation);
            } catch (const std::exception& error) {
                return stop(TerminalReason::ModelError, error.what());
            } catch (...) {
                return stop(TerminalReason::ModelError, "Model threw an unknown exception");
            }
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
        if (result.tokensUsed > effectiveDefinition.budget.maxTokens) {
            result.trace.push_back(std::move(trace));
            return stop(TerminalReason::TokenLimit, "Token budget exhausted");
        }

        result.conversation.push_back({.role = ConversationRole::Assistant,
                                       .text = response.text,
                                       .toolCalls = response.toolCalls});

        const auto completeUnexecutedCalls = [&](std::size_t firstCall, const juce::String& code,
                                                 const juce::String& message) {
            for (std::size_t index = firstCall; index < response.toolCalls.size(); ++index) {
                auto toolResult =
                    makeErrorResult(response.toolCalls[index], result.finalRevision, code, message);
                trace.toolResults.push_back(toolResult);
                result.conversation.push_back(
                    {.role = ConversationRole::Tool, .toolResult = std::move(toolResult)});
            }
        };

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
                            const auto* tool = findTool(effectiveDefinition, call.name);
                            return tool != nullptr && tool->access == ToolAccess::Mutation;
                        })) {
            completeUnexecutedCalls(0, "unsafe_parallel_mutation",
                                    "Parallel mutation batch was not executed");
            result.trace.push_back(std::move(trace));
            return stop(TerminalReason::UnsafeParallelMutation,
                        "Parallel tool batches containing mutations are not safe; retry "
                        "sequentially");
        }

        for (std::size_t callIndex = 0; callIndex < response.toolCalls.size(); ++callIndex) {
            const auto& call = response.toolCalls[callIndex];
            if (cancellation.isCancellationRequested()) {
                completeUnexecutedCalls(callIndex, "cancelled", "Run cancelled before execution");
                result.trace.push_back(std::move(trace));
                return stop(TerminalReason::Cancelled, "Run cancelled");
            }
            if (timedOut()) {
                completeUnexecutedCalls(callIndex, "time_limit",
                                        "Wall-time budget exhausted before execution");
                result.trace.push_back(std::move(trace));
                return stop(TerminalReason::TimeLimit, "Wall-time budget exhausted");
            }

            const auto fingerprint = callFingerprint(call, result.finalRevision);
            auto& identicalCount = identicalCallCounts[fingerprint];
            ++identicalCount;
            if (identicalCount > effectiveDefinition.budget.maxIdenticalCallsAtRevision) {
                completeUnexecutedCalls(callIndex, "repeated_tool_call",
                                        "Repeated tool call was not executed");
                result.trace.push_back(std::move(trace));
                return stop(TerminalReason::RepeatedToolCall,
                            "Repeated tool call at the same project revision: " + call.name);
            }

            ToolResult toolResult;
            const auto* tool = findTool(effectiveDefinition, call.name);
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
                if (isMutation && result.mutations >= effectiveDefinition.budget.maxMutations) {
                    completeUnexecutedCalls(callIndex, "mutation_limit",
                                            "Mutation budget exhausted before execution");
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
                        completeUnexecutedCalls(callIndex, "cancelled",
                                                "Run cancelled before execution");
                        result.trace.push_back(std::move(trace));
                        return stop(TerminalReason::Cancelled, "Run cancelled");
                    }
                    if (timedOut()) {
                        completeUnexecutedCalls(callIndex, "time_limit",
                                                "Wall-time budget exhausted before execution");
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

        if (isDirectPlanStep) {
            const bool succeeded =
                !trace.toolResults.empty() &&
                std::all_of(trace.toolResults.begin(), trace.toolResults.end(),
                            [](const ToolResult& toolResult) { return toolResult.success; });
            result.trace.push_back(std::move(trace));
            if (succeeded) {
                result.finalText = "OK";
                return stop(TerminalReason::Completed);
            }
            result.fastInference.fallbackReason =
                "Direct plan execution failed; delegated to the general agent";
            continue;
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
