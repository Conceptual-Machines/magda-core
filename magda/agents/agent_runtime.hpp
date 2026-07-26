#pragma once

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace magda::agent {

enum class ToolAccess { Read, Mutation };

struct ToolDefinition {
    juce::String name;
    juce::String description;
    juce::var inputSchema;
    ToolAccess access = ToolAccess::Read;
};

struct ToolCall {
    juce::String id;
    juce::String name;
    juce::var arguments;
};

struct ToolError {
    juce::String code;
    juce::String message;
    juce::var details;
};

struct ToolResult {
    juce::String callId;
    juce::String toolName;
    bool success = false;
    juce::var content;
    std::optional<ToolError> error;
    std::uint64_t projectRevision = 0;
    bool mutated = false;
};

enum class ConversationRole { User, Assistant, Tool };

struct ConversationTurn {
    ConversationRole role = ConversationRole::User;
    juce::String text;
    std::vector<ToolCall> toolCalls;
    std::optional<ToolResult> toolResult;
};

struct RunBudget {
    std::size_t maxSteps = 8;
    std::size_t maxTokens = 32'000;
    std::size_t maxMutations = 8;
    std::size_t maxIdenticalCallsAtRevision = 2;
    std::chrono::milliseconds maxWallTime = std::chrono::minutes(2);
};

struct AgentDefinition {
    juce::String id;
    juce::String systemPrompt;
    juce::var baselineContext;
    std::vector<ToolDefinition> tools;
    RunBudget budget;
};

struct ModelRequest {
    juce::String agentId;
    juce::String systemPrompt;
    juce::var baselineContext;
    std::vector<ToolDefinition> tools;
    std::vector<ConversationTurn> conversation;
    std::uint64_t projectRevision = 0;
    std::size_t remainingSteps = 0;
    std::size_t remainingTokens = 0;
    std::size_t remainingMutations = 0;
};

struct ModelResponse {
    bool success = false;
    juce::String error;
    juce::String text;
    std::vector<ToolCall> toolCalls;
    std::size_t tokensUsed = 0;
};

struct PermissionContext {
    juce::String principal;
    std::vector<juce::String> scopes;
};

class CancellationToken {
  public:
    using Check = std::function<bool()>;

    CancellationToken() = default;
    explicit CancellationToken(Check check) : check_(std::move(check)) {}

    bool isCancellationRequested() const {
        return check_ && check_();
    }

  private:
    Check check_;
};

class Model {
  public:
    virtual ~Model() = default;
    virtual ModelResponse generate(const ModelRequest& request,
                                   const CancellationToken& cancellation) = 0;
};

struct ToolExecutionRequest {
    ToolCall call;
    std::uint64_t expectedRevision = 0;
    PermissionContext permissions;
    std::chrono::steady_clock::time_point deadline;
};

class ToolExecutor {
  public:
    virtual ~ToolExecutor() = default;
    virtual ToolResult execute(const ToolExecutionRequest& request,
                               const CancellationToken& cancellation) = 0;
};

struct ApprovalRequest {
    ToolDefinition tool;
    ToolCall call;
    std::uint64_t projectRevision = 0;
    PermissionContext permissions;
};

struct ApprovalDecision {
    bool approved = true;
    juce::String reason;
};

using ApprovalHook = std::function<ApprovalDecision(const ApprovalRequest&)>;

struct AgentRunInput {
    juce::String userMessage;
    std::vector<ConversationTurn> conversation;
    std::uint64_t projectRevision = 0;
    PermissionContext permissions;
};

enum class RunState { Ready, Running, Completed, Stopped };

enum class TerminalReason {
    Completed,
    Cancelled,
    StepLimit,
    TokenLimit,
    MutationLimit,
    TimeLimit,
    RepeatedToolCall,
    ModelError,
    InvalidModelResponse,
};

struct StepTrace {
    std::size_t index = 0;
    std::uint64_t revisionBefore = 0;
    std::uint64_t revisionAfter = 0;
    std::size_t tokensUsed = 0;
    juce::String assistantText;
    std::vector<ToolCall> toolCalls;
    std::vector<ToolResult> toolResults;
};

struct RunResult {
    RunState state = RunState::Ready;
    TerminalReason reason = TerminalReason::InvalidModelResponse;
    juce::String finalText;
    juce::String detail;
    std::uint64_t finalRevision = 0;
    std::size_t steps = 0;
    std::size_t tokensUsed = 0;
    std::size_t mutations = 0;
    std::vector<ConversationTurn> conversation;
    std::vector<StepTrace> trace;
};

class AgentRuntime {
  public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;

    AgentRuntime(Model& model, ToolExecutor& executor, ApprovalHook approvalHook = {},
                 Now now = Clock::now);

    RunResult run(const AgentDefinition& definition, AgentRunInput input,
                  CancellationToken cancellation = {});

  private:
    Model& model_;
    ToolExecutor& executor_;
    ApprovalHook approvalHook_;
    Now now_;
};

const char* toString(TerminalReason reason);

}  // namespace magda::agent
