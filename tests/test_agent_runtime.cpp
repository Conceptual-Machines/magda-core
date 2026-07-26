#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "magda/agents/agent_runtime.hpp"

namespace {

using namespace magda::agent;

juce::var object(std::initializer_list<std::pair<juce::Identifier, juce::var>> properties) {
    auto* value = new juce::DynamicObject();
    for (const auto& [name, property] : properties)
        value->setProperty(name, property);
    return value;
}

ToolCall call(juce::String id, juce::String name, juce::var arguments = object({})) {
    return {.id = std::move(id), .name = std::move(name), .arguments = std::move(arguments)};
}

ToolDefinition readTool(juce::String name) {
    return {.name = std::move(name), .description = "read", .inputSchema = object({})};
}

ToolDefinition mutationTool(juce::String name) {
    return {.name = std::move(name),
            .description = "write",
            .inputSchema = object({}),
            .access = ToolAccess::Mutation};
}

AgentDefinition definition(std::vector<ToolDefinition> tools) {
    return {.id = "arrangement",
            .systemPrompt = "Use tools and finish.",
            .baselineContext = object({{"view", "arrangement"}}),
            .tools = std::move(tools)};
}

class FakeModel final : public Model {
  public:
    std::function<ModelResponse(const ModelRequest&, const CancellationToken&)> handler;
    std::vector<ModelRequest> requests;

    ModelResponse generate(const ModelRequest& request,
                           const CancellationToken& cancellation) override {
        requests.push_back(request);
        return handler(request, cancellation);
    }
};

class FakeExecutor final : public ToolExecutor {
  public:
    std::function<ToolResult(const ToolExecutionRequest&, const CancellationToken&)> handler;
    std::vector<ToolExecutionRequest> requests;

    ToolResult execute(const ToolExecutionRequest& request,
                       const CancellationToken& cancellation) override {
        requests.push_back(request);
        return handler(request, cancellation);
    }
};

class FakeFastInferencePolicy final : public FastInferencePolicy {
  public:
    std::function<FastInferenceResult(const FastInferenceRequest&, const CancellationToken&)>
        handler;
    std::vector<FastInferenceRequest> requests;

    FastInferenceResult evaluate(const FastInferenceRequest& request,
                                 const CancellationToken& cancellation) override {
        requests.push_back(request);
        return handler(request, cancellation);
    }
};

const ToolResult& lastToolResult(const ModelRequest& request) {
    for (auto it = request.conversation.rbegin(); it != request.conversation.rend(); ++it)
        if (it->toolResult.has_value())
            return *it->toolResult;
    throw std::logic_error("No tool result");
}

}  // namespace

TEST_CASE("AgentRuntime can narrow tools with advisory fast inference", "[agent-runtime][fast]") {
    FakeModel model;
    FakeExecutor executor;
    FakeFastInferencePolicy policy;

    policy.handler = [](const FastInferenceRequest& request, const CancellationToken&) {
        REQUIRE(request.userMessage == "Turn down the bass");
        REQUIRE(request.agentId == "arrangement");
        REQUIRE(request.contextFeatures["selectedEntityType"].toString() == "track");
        REQUIRE(request.availableOperations ==
                std::vector<juce::String>{"track.read", "track.gain.set", "clip.move"});
        return FastInferenceResult{
            .decision = FastInferenceDecision::NarrowAgentTools,
            .confidence = 0.91f,
            .operations = {{.operationId = "clip.move", .confidence = 0.40f},
                           {.operationId = "track.gain.set", .confidence = 0.96f},
                           {.operationId = "not.available", .confidence = 1.0f}}};
    };
    model.handler = [](const ModelRequest& request, const CancellationToken&) {
        REQUIRE(request.tools.size() == 1);
        REQUIRE(request.tools.front().name == "track.gain.set");
        return ModelResponse{.success = true, .text = "I would turn down the bass."};
    };

    auto def = definition(
        {readTool("track.read"), mutationTool("track.gain.set"), mutationTool("clip.move")});
    def.fastInference = {.enabled = true,
                         .allowToolNarrowing = true,
                         .maxNarrowedTools = 1,
                         .minimumNarrowingConfidence = 0.5f};

    AgentRuntime runtime(model, executor, {}, AgentRuntime::Clock::now, &policy);
    const auto result = runtime.run(
        def, {.userMessage = "Turn down the bass",
              .fastInferenceContext = object({{"selectedEntityType", juce::var("track")}})});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE(policy.requests.size() == 1);
    REQUIRE(result.fastInference.evaluated);
    REQUIRE(result.fastInference.predictedDecision == FastInferenceDecision::NarrowAgentTools);
    REQUIRE(result.fastInference.appliedDecision == FastInferenceDecision::NarrowAgentTools);
    REQUIRE(result.fastInference.selectedTools == std::vector<juce::String>{"track.gain.set"});
}

TEST_CASE("AgentRuntime executes calibrated direct plans through normal safety boundaries",
          "[agent-runtime][fast]") {
    FakeModel model;
    FakeExecutor executor;
    FakeFastInferencePolicy policy;
    int modelCalls = 0;
    int approvalCalls = 0;

    model.handler = [&](const ModelRequest&, const CancellationToken&) {
        ++modelCalls;
        return ModelResponse{.success = true, .text = "Unexpected model call"};
    };
    policy.handler = [](const FastInferenceRequest&, const CancellationToken&) {
        return FastInferenceResult{
            .decision = FastInferenceDecision::DirectPlan,
            .confidence = 0.98f,
            .operations = {{.operationId = "track.mute.set",
                            .arguments = object({{"trackId", 7}, {"muted", true}}),
                            .confidence = 0.97f}}};
    };
    executor.handler = [](const ToolExecutionRequest& request, const CancellationToken&) {
        REQUIRE(request.call.id == "fast-inference-1");
        REQUIRE(request.call.name == "track.mute.set");
        REQUIRE(static_cast<int>(request.call.arguments["trackId"]) == 7);
        REQUIRE(request.expectedRevision == 22);
        REQUIRE(request.permissions.principal == "console");
        return ToolResult{.success = true, .projectRevision = 23, .mutated = true};
    };

    auto def = definition({mutationTool("track.mute.set")});
    def.fastInference.enabled = true;
    def.fastInference.directPlanConfidenceByOperation["track.mute.set"] = 0.95f;

    AgentRuntime runtime(
        model, executor,
        [&](const ApprovalRequest& request) {
            ++approvalCalls;
            REQUIRE(request.call.name == "track.mute.set");
            REQUIRE(request.projectRevision == 22);
            return ApprovalDecision{.approved = true};
        },
        AgentRuntime::Clock::now, &policy);
    const auto result =
        runtime.run(def, {.userMessage = "Mute the bass",
                          .projectRevision = 22,
                          .permissions = {.principal = "console", .scopes = {"project.write"}}});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE(result.finalText == "OK");
    REQUIRE(result.finalRevision == 23);
    REQUIRE(result.mutations == 1);
    REQUIRE(result.steps == 1);
    REQUIRE(modelCalls == 0);
    REQUIRE(approvalCalls == 1);
    REQUIRE(executor.requests.size() == 1);
    REQUIRE(result.fastInference.appliedDecision == FastInferenceDecision::DirectPlan);
}

TEST_CASE("Denied fast direct plans fall back without bypassing approval",
          "[agent-runtime][fast]") {
    FakeModel model;
    FakeExecutor executor;
    FakeFastInferencePolicy policy;

    policy.handler = [](const FastInferenceRequest&, const CancellationToken&) {
        return FastInferenceResult{.decision = FastInferenceDecision::DirectPlan,
                                   .confidence = 0.99f,
                                   .operations = {{.operationId = "track.delete",
                                                   .arguments = object({{"trackId", 7}}),
                                                   .confidence = 0.99f}}};
    };
    model.handler = [](const ModelRequest& request, const CancellationToken&) {
        REQUIRE_FALSE(lastToolResult(request).success);
        REQUIRE(lastToolResult(request).error->code == "approval_denied");
        return ModelResponse{.success = true, .text = "The deletion was not approved."};
    };
    executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) {
        FAIL("A denied direct plan must not reach the executor");
        return ToolResult{};
    };

    auto def = definition({mutationTool("track.delete")});
    def.fastInference.enabled = true;
    def.fastInference.directPlanConfidenceByOperation["track.delete"] = 0.98f;

    AgentRuntime runtime(
        model, executor,
        [](const ApprovalRequest&) {
            return ApprovalDecision{.approved = false, .reason = "Destructive operation"};
        },
        AgentRuntime::Clock::now, &policy);
    const auto result = runtime.run(def, {.userMessage = "Delete the bass track"});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE(result.finalText == "The deletion was not approved.");
    REQUIRE(result.steps == 2);
    REQUIRE(result.mutations == 0);
    REQUIRE(executor.requests.empty());
    REQUIRE(result.fastInference.fallbackReason.contains("execution failed"));
}

TEST_CASE("AgentRuntime falls back when a direct prediction is not calibrated",
          "[agent-runtime][fast]") {
    FakeModel model;
    FakeExecutor executor;
    FakeFastInferencePolicy policy;

    policy.handler = [](const FastInferenceRequest&, const CancellationToken&) {
        return FastInferenceResult{
            .decision = FastInferenceDecision::DirectPlan,
            .confidence = 0.90f,
            .operations = {{.operationId = "project.delete", .confidence = 1.0f}}};
    };
    model.handler = [](const ModelRequest& request, const CancellationToken&) {
        REQUIRE(request.tools.size() == 1);
        REQUIRE(request.tools.front().name == "project.delete");
        return ModelResponse{.success = true, .text = "This needs the general path."};
    };

    auto def = definition({mutationTool("project.delete")});
    def.fastInference.enabled = true;  // No per-operation direct threshold: never direct.

    AgentRuntime runtime(model, executor, {}, AgentRuntime::Clock::now, &policy);
    const auto result = runtime.run(def, {.userMessage = "Delete the project"});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE(result.finalText == "This needs the general path.");
    REQUIRE(executor.requests.empty());
    REQUIRE(result.fastInference.appliedDecision == FastInferenceDecision::NeedsGeneralAgent);
    REQUIRE(result.fastInference.fallbackReason.contains("confidence policy"));
}

TEST_CASE("Disabled fast inference leaves AgentRuntime behavior unchanged",
          "[agent-runtime][fast]") {
    FakeModel model;
    FakeExecutor executor;
    FakeFastInferencePolicy policy;

    policy.handler = [](const FastInferenceRequest&, const CancellationToken&) {
        FAIL("Disabled policy must not be evaluated");
        return FastInferenceResult{};
    };
    model.handler = [](const ModelRequest& request, const CancellationToken&) {
        REQUIRE(request.tools.size() == 2);
        return ModelResponse{.success = true, .text = "General result"};
    };

    auto def = definition({readTool("track.read"), mutationTool("track.rename")});
    AgentRuntime runtime(model, executor, {}, AgentRuntime::Clock::now, &policy);
    const auto result = runtime.run(def, {.userMessage = "What is selected?"});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE_FALSE(result.fastInference.evaluated);
    REQUIRE(policy.requests.empty());
}

TEST_CASE("AgentRuntime performs dependent actions with refreshed revisions", "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    int modelStep = 0;

    model.handler = [&](const ModelRequest& request, const CancellationToken&) {
        ++modelStep;
        if (modelStep == 1) {
            REQUIRE(request.projectRevision == 10);
            REQUIRE(request.baselineContext["view"].toString() == "arrangement");
            return ModelResponse{
                .success = true,
                .toolCalls = {call("create-1", "track.create", object({{"name", "Bass"}}))},
                .tokensUsed = 12};
        }
        if (modelStep == 2) {
            REQUIRE(request.projectRevision == 11);
            REQUIRE(static_cast<int>(lastToolResult(request).content["trackId"]) == 42);
            return ModelResponse{
                .success = true,
                .toolCalls = {call("clip-1", "clip.create", object({{"trackId", 42}}))},
                .tokensUsed = 9};
        }
        REQUIRE(request.projectRevision == 12);
        REQUIRE(static_cast<int>(lastToolResult(request).content["clipId"]) == 99);
        return ModelResponse{
            .success = true, .text = "Created a bass track and clip.", .tokensUsed = 5};
    };

    executor.handler = [](const ToolExecutionRequest& request, const CancellationToken&) {
        if (request.call.name == "track.create") {
            REQUIRE(request.expectedRevision == 10);
            REQUIRE(request.permissions.principal == "console");
            REQUIRE(request.permissions.scopes == std::vector<juce::String>{"project.write"});
            return ToolResult{.success = true,
                              .content = object({{"trackId", 42}}),
                              .projectRevision = 11,
                              .mutated = true};
        }
        REQUIRE(request.call.name == "clip.create");
        REQUIRE(request.expectedRevision == 11);
        return ToolResult{.success = true,
                          .content = object({{"clipId", 99}}),
                          .projectRevision = 12,
                          .mutated = true};
    };

    AgentRuntime runtime(model, executor);
    auto result =
        runtime.run(definition({mutationTool("track.create"), mutationTool("clip.create")}),
                    {.userMessage = "Create a bass track with a clip.",
                     .projectRevision = 10,
                     .permissions = {.principal = "console", .scopes = {"project.write"}}});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE(result.reason == TerminalReason::Completed);
    REQUIRE(result.finalText == "Created a bass track and clip.");
    REQUIRE(result.finalRevision == 12);
    REQUIRE(result.steps == 3);
    REQUIRE(result.mutations == 2);
    REQUIRE(result.tokensUsed == 26);
    REQUIRE(result.trace.size() == 3);
    REQUIRE(result.trace[0].revisionBefore == 10);
    REQUIRE(result.trace[0].revisionAfter == 11);
    REQUIRE(result.trace[1].revisionAfter == 12);
}

TEST_CASE("AgentRuntime returns execution errors to the model for repair", "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    int modelStep = 0;

    model.handler = [&](const ModelRequest& request, const CancellationToken&) {
        ++modelStep;
        if (modelStep == 1)
            return ModelResponse{
                .success = true,
                .toolCalls = {call("rename-1", "track.rename", object({{"trackId", 999}}))}};
        if (modelStep == 2) {
            REQUIRE_FALSE(lastToolResult(request).success);
            REQUIRE(lastToolResult(request).error->code == "not_found");
            return ModelResponse{
                .success = true,
                .toolCalls = {call("rename-2", "track.rename", object({{"trackId", 7}}))}};
        }
        REQUIRE(lastToolResult(request).success);
        REQUIRE(request.projectRevision == 4);
        return ModelResponse{.success = true, .text = "Renamed the selected track."};
    };

    executor.handler = [](const ToolExecutionRequest& request, const CancellationToken&) {
        if (static_cast<int>(request.call.arguments["trackId"]) == 999)
            return ToolResult{.success = false,
                              .error =
                                  ToolError{.code = "not_found", .message = "Track does not exist"},
                              .projectRevision = request.expectedRevision};
        return ToolResult{.success = true,
                          .content = object({{"name", "Bass"}}),
                          .projectRevision = 4,
                          .mutated = true};
    };

    AgentRuntime runtime(model, executor);
    const auto result =
        runtime.run(definition({mutationTool("track.rename")}),
                    {.userMessage = "Rename the selected track.", .projectRevision = 3});

    REQUIRE(result.reason == TerminalReason::Completed);
    REQUIRE(result.steps == 3);
    REQUIRE(result.mutations == 2);
    REQUIRE(executor.requests.size() == 2);
}

TEST_CASE("AgentRuntime blocks tools outside the surface allowlist", "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    int modelStep = 0;

    model.handler = [&](const ModelRequest& request, const CancellationToken&) {
        ++modelStep;
        if (modelStep == 1)
            return ModelResponse{.success = true,
                                 .toolCalls = {call("delete-1", "project.delete")}};
        REQUIRE(lastToolResult(request).error->code == "tool_not_allowed");
        return ModelResponse{.success = true, .text = "I cannot delete the project."};
    };
    executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) -> ToolResult {
        FAIL("Disallowed tool reached the executor");
        return {};
    };

    AgentRuntime runtime(model, executor);
    const auto result = runtime.run(definition({readTool("project.get")}),
                                    {.userMessage = "Delete everything.", .projectRevision = 2});

    REQUIRE(result.reason == TerminalReason::Completed);
    REQUIRE(executor.requests.empty());
}

TEST_CASE("AgentRuntime returns denied mutations to the model", "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    int modelStep = 0;

    model.handler = [&](const ModelRequest& request, const CancellationToken&) {
        ++modelStep;
        if (modelStep == 1)
            return ModelResponse{
                .success = true,
                .toolCalls = {call("delete-1", "track.delete", object({{"trackId", 7}}))}};
        REQUIRE(lastToolResult(request).error->code == "approval_denied");
        REQUIRE(request.remainingMutations == 8);
        return ModelResponse{.success = true, .text = "Deletion was not approved."};
    };
    executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) -> ToolResult {
        FAIL("Denied mutation reached the executor");
        return {};
    };

    AgentRuntime runtime(model, executor, [](const ApprovalRequest& request) {
        REQUIRE(request.tool.name == "track.delete");
        REQUIRE(request.projectRevision == 5);
        REQUIRE(request.permissions.principal == "console");
        return ApprovalDecision{.approved = false, .reason = "User approval required"};
    });
    const auto result = runtime.run(definition({mutationTool("track.delete")}),
                                    {.userMessage = "Delete the track.",
                                     .projectRevision = 5,
                                     .permissions = {.principal = "console"}});

    REQUIRE(result.reason == TerminalReason::Completed);
    REQUIRE(result.mutations == 0);
    REQUIRE(executor.requests.empty());
}

TEST_CASE("AgentRuntime terminates repeated failing calls at one revision", "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    int callNumber = 0;
    model.handler = [&callNumber](const ModelRequest&, const CancellationToken&) {
        ++callNumber;
        return ModelResponse{.success = true,
                             .toolCalls = {call("retry-" + juce::String(callNumber), "track.get",
                                                object({{"trackId", 404}}))}};
    };
    executor.handler = [](const ToolExecutionRequest& request, const CancellationToken&) {
        return ToolResult{.success = false,
                          .error = ToolError{.code = "not_found", .message = "Missing"},
                          .projectRevision = request.expectedRevision};
    };

    auto agent = definition({readTool("track.get")});
    agent.budget.maxIdenticalCallsAtRevision = 2;
    AgentRuntime runtime(model, executor);
    const auto result =
        runtime.run(agent, {.userMessage = "Find the track.", .projectRevision = 8});

    REQUIRE(result.reason == TerminalReason::RepeatedToolCall);
    REQUIRE(result.steps == 3);
    REQUIRE(executor.requests.size() == 2);
    REQUIRE(result.finalRevision == 8);
    REQUIRE(result.conversation.back().role == ConversationRole::Tool);
    REQUIRE(result.conversation.back().toolResult->error->code == "repeated_tool_call");
}

TEST_CASE("AgentRuntime enforces mutation and token budgets", "[agent-runtime]") {
    SECTION("mutation budget") {
        FakeModel model;
        FakeExecutor executor;
        int callNumber = 0;
        model.handler = [&](const ModelRequest&, const CancellationToken&) {
            ++callNumber;
            return ModelResponse{.success = true,
                                 .toolCalls = {call(juce::String(callNumber), "track.create",
                                                    object({{"name", juce::String(callNumber)}}))}};
        };
        executor.handler = [](const ToolExecutionRequest& request, const CancellationToken&) {
            return ToolResult{
                .success = true, .projectRevision = request.expectedRevision + 1, .mutated = true};
        };

        auto agent = definition({mutationTool("track.create")});
        agent.budget.maxMutations = 1;
        AgentRuntime runtime(model, executor);
        const auto result =
            runtime.run(agent, {.userMessage = "Create tracks.", .projectRevision = 1});

        REQUIRE(result.reason == TerminalReason::MutationLimit);
        REQUIRE(result.mutations == 1);
        REQUIRE(executor.requests.size() == 1);
        REQUIRE(result.conversation.back().role == ConversationRole::Tool);
        REQUIRE(result.conversation.back().toolResult->error->code == "mutation_limit");
    }

    SECTION("token budget") {
        FakeModel model;
        FakeExecutor executor;
        model.handler = [](const ModelRequest&, const CancellationToken&) {
            return ModelResponse{.success = true, .text = "Done", .tokensUsed = 11};
        };
        executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) {
            return ToolResult{};
        };

        auto agent = definition({});
        agent.budget.maxTokens = 10;
        AgentRuntime runtime(model, executor);
        const auto result = runtime.run(agent, {.userMessage = "Do it."});

        REQUIRE(result.reason == TerminalReason::TokenLimit);
        REQUIRE(result.finalText.isEmpty());
    }
}

TEST_CASE("AgentRuntime propagates cancellation before model execution", "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    model.handler = [](const ModelRequest&, const CancellationToken&) -> ModelResponse {
        FAIL("Cancelled run reached the model");
        return {};
    };
    executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) -> ToolResult {
        FAIL("Cancelled run reached the executor");
        return {};
    };

    bool cancelled = true;
    AgentRuntime runtime(model, executor);
    const auto result = runtime.run(definition({}), {.userMessage = "Do it."},
                                    CancellationToken([&cancelled] { return cancelled; }));

    REQUIRE(result.reason == TerminalReason::Cancelled);
    REQUIRE(result.steps == 0);
    REQUIRE(model.requests.empty());
}

TEST_CASE("AgentRuntime observes cancellation and time limits after model execution",
          "[agent-runtime]") {
    SECTION("cancelled during model call") {
        FakeModel model;
        FakeExecutor executor;
        bool cancelled = false;
        model.handler = [&cancelled](const ModelRequest&, const CancellationToken&) {
            cancelled = true;
            return ModelResponse{.success = true, .text = "Too late"};
        };
        executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) {
            return ToolResult{};
        };

        AgentRuntime runtime(model, executor);
        const auto result = runtime.run(definition({}), {.userMessage = "Do it."},
                                        CancellationToken([&cancelled] { return cancelled; }));

        REQUIRE(result.reason == TerminalReason::Cancelled);
        REQUIRE(result.steps == 1);
        REQUIRE(result.finalText.isEmpty());
    }

    SECTION("wall time expires during model call") {
        FakeModel model;
        FakeExecutor executor;
        auto now = AgentRuntime::Clock::time_point{};
        model.handler = [&now](const ModelRequest&, const CancellationToken&) {
            now += std::chrono::seconds(2);
            return ModelResponse{.success = true, .text = "Too late"};
        };
        executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) {
            return ToolResult{};
        };

        auto agent = definition({});
        agent.budget.maxWallTime = std::chrono::seconds(1);
        AgentRuntime runtime(model, executor, {}, [&now] { return now; });
        const auto result = runtime.run(agent, {.userMessage = "Do it."});

        REQUIRE(result.reason == TerminalReason::TimeLimit);
        REQUIRE(result.steps == 1);
        REQUIRE(result.trace.size() == 1);
    }
}

TEST_CASE("AgentRuntime executes declared sequential batches with a fresh revision per operation",
          "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    int modelStep = 0;

    model.handler = [&](const ModelRequest& request, const CancellationToken&) {
        ++modelStep;
        if (modelStep == 1) {
            return ModelResponse{
                .success = true,
                .toolCalls = {call("mute-1", "track.mute", object({{"trackId", 1}})),
                              call("mute-2", "track.mute", object({{"trackId", 2}}))}};
        }
        REQUIRE(request.projectRevision == 22);
        REQUIRE(lastToolResult(request).projectRevision == 22);
        return ModelResponse{.success = true, .text = "Muted both tracks."};
    };
    executor.handler = [](const ToolExecutionRequest& request, const CancellationToken&) {
        return ToolResult{
            .success = true, .projectRevision = request.expectedRevision + 1, .mutated = true};
    };

    AgentRuntime runtime(model, executor);
    const auto result = runtime.run(definition({mutationTool("track.mute")}),
                                    {.userMessage = "Mute both tracks.", .projectRevision = 20});

    REQUIRE(result.reason == TerminalReason::Completed);
    REQUIRE(executor.requests.size() == 2);
    REQUIRE(executor.requests[0].expectedRevision == 20);
    REQUIRE(executor.requests[1].expectedRevision == 21);
    REQUIRE(result.finalRevision == 22);
    REQUIRE(result.trace[0].revisionBefore == 20);
    REQUIRE(result.trace[0].revisionAfter == 22);
}

TEST_CASE("AgentRuntime rejects parallel mutation batches before dispatch", "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    model.handler = [](const ModelRequest&, const CancellationToken&) {
        return ModelResponse{.success = true,
                             .toolCalls = {call("mute-1", "track.mute", object({{"trackId", 1}})),
                                           call("read-1", "track.get", object({{"trackId", 1}}))},
                             .toolCallExecution = ModelResponse::ToolCallExecution::Parallel};
    };
    executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) -> ToolResult {
        FAIL("Unsafe parallel mutation batch reached the executor");
        return {};
    };

    AgentRuntime runtime(model, executor);
    const auto result =
        runtime.run(definition({mutationTool("track.mute"), readTool("track.get")}),
                    {.userMessage = "Mute and inspect the track.", .projectRevision = 3});

    REQUIRE(result.reason == TerminalReason::UnsafeParallelMutation);
    REQUIRE(result.detail.contains("retry sequentially"));
    REQUIRE(std::string_view{toString(result.reason)} == "unsafe_parallel_mutation");
    REQUIRE(result.steps == 1);
    REQUIRE(result.mutations == 0);
    REQUIRE(executor.requests.empty());
}

TEST_CASE("AgentRuntime returns duplicate tool-call IDs to the model for repair",
          "[agent-runtime]") {
    FakeModel model;
    FakeExecutor executor;
    int modelStep = 0;

    model.handler = [&](const ModelRequest& request, const CancellationToken&) {
        ++modelStep;
        if (modelStep == 1)
            return ModelResponse{
                .success = true,
                .toolCalls = {call("read-1", "track.get", object({{"trackId", 1}}))}};
        if (modelStep == 2)
            return ModelResponse{
                .success = true,
                .toolCalls = {call("read-1", "track.get", object({{"trackId", 2}}))}};
        REQUIRE(lastToolResult(request).error->code == "duplicate_tool_call_id");
        return ModelResponse{.success = true, .text = "Kept the first result."};
    };
    executor.handler = [](const ToolExecutionRequest& request, const CancellationToken&) {
        return ToolResult{.success = true,
                          .content = object({{"trackId", 1}}),
                          .projectRevision = request.expectedRevision};
    };

    AgentRuntime runtime(model, executor);
    const auto result = runtime.run(definition({readTool("track.get")}),
                                    {.userMessage = "Inspect tracks.", .projectRevision = 7});

    REQUIRE(result.reason == TerminalReason::Completed);
    REQUIRE(executor.requests.size() == 1);
    REQUIRE(result.trace[1].toolResults[0].error->code == "duplicate_tool_call_id");
}

TEST_CASE("AgentRuntime makes approval failures observable and rechecks cancellation",
          "[agent-runtime]") {
    SECTION("approval hook exception") {
        FakeModel model;
        FakeExecutor executor;
        int modelStep = 0;
        model.handler = [&](const ModelRequest& request, const CancellationToken&) {
            ++modelStep;
            if (modelStep == 1)
                return ModelResponse{
                    .success = true,
                    .toolCalls = {call("delete-1", "track.delete", object({{"trackId", 1}}))}};
            REQUIRE(lastToolResult(request).error->code == "approval_error");
            REQUIRE(lastToolResult(request).error->message == "approval unavailable");
            return ModelResponse{.success = true, .text = "Approval could not be requested."};
        };
        executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) -> ToolResult {
            FAIL("Mutation with a failed approval hook reached the executor");
            return {};
        };

        AgentRuntime runtime(model, executor, [](const ApprovalRequest&) -> ApprovalDecision {
            throw std::runtime_error("approval unavailable");
        });
        const auto result = runtime.run(definition({mutationTool("track.delete")}),
                                        {.userMessage = "Delete the track.", .projectRevision = 9});

        REQUIRE(result.reason == TerminalReason::Completed);
        REQUIRE(result.mutations == 0);
        REQUIRE(executor.requests.empty());
    }

    SECTION("cancelled by approval owner before dispatch") {
        FakeModel model;
        FakeExecutor executor;
        bool cancelled = false;
        model.handler = [](const ModelRequest&, const CancellationToken&) {
            return ModelResponse{
                .success = true,
                .toolCalls = {call("delete-1", "track.delete", object({{"trackId", 1}}))}};
        };
        executor.handler = [](const ToolExecutionRequest&, const CancellationToken&) -> ToolResult {
            FAIL("Cancelled mutation reached the executor");
            return {};
        };

        AgentRuntime runtime(model, executor, [&](const ApprovalRequest&) {
            cancelled = true;
            return ApprovalDecision{.approved = true};
        });
        const auto result = runtime.run(definition({mutationTool("track.delete")}),
                                        {.userMessage = "Delete the track.", .projectRevision = 9},
                                        CancellationToken([&cancelled] { return cancelled; }));

        REQUIRE(result.reason == TerminalReason::Cancelled);
        REQUIRE(result.mutations == 0);
        REQUIRE(executor.requests.empty());
    }
}
