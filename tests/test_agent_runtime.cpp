#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <stdexcept>
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

const ToolResult& lastToolResult(const ModelRequest& request) {
    for (auto it = request.conversation.rbegin(); it != request.conversation.rend(); ++it)
        if (it->toolResult.has_value())
            return *it->toolResult;
    throw std::logic_error("No tool result");
}

}  // namespace

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
    model.handler = [](const ModelRequest&, const CancellationToken&) {
        return ModelResponse{.success = true,
                             .toolCalls = {call("retry", "track.get", object({{"trackId", 404}}))}};
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
