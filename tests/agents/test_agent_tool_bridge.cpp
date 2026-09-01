#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <vector>

#include "MockMagdaApi.hpp"
#include "RemoteTestScopes.hpp"
#include "magda/agents/agent_tool_bridge.hpp"
#include "magda/daw/api/remote_service.hpp"

using namespace magda;
using namespace magda::agent;
using magda::test::MockMagdaApi;

namespace {

/// The bridge reads live model state through the service, which asserts the
/// message thread; the Catch2 runner has none, the same accommodation the
/// remote service tests make.
struct MessageThreadRelaxation {
    remote::ScopedMessageThreadAssertionDisabler disabler;
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

ToolExecutionRequest requestFor(juce::String id, juce::String name, juce::var arguments,
                                std::uint64_t expectedRevision = 0) {
    return {
        .call = {.id = std::move(id), .name = std::move(name), .arguments = std::move(arguments)},
        .expectedRevision = expectedRevision,
        .permissions = {},
        .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5)};
}

class ScriptedModel final : public Model {
  public:
    std::function<ModelResponse(const ModelRequest&, const CancellationToken&)> handler;

    ModelResponse generate(const ModelRequest& request,
                           const CancellationToken& cancellation) override {
        return handler(request, cancellation);
    }
};

}  // namespace

// ===========================================================================
// Surface tool sets
// ===========================================================================

TEST_CASE("Every surface allowlist resolves completely against the registry",
          "[agent-bridge][surfaces]") {
    // The production form of `invalidSurfaceTools`: a surface naming an
    // operation the registry does not carry would hand its model a tool
    // nothing can execute, and this is where that becomes a red build instead
    // of a runtime log line.
    for (const auto& surface : registeredAgentSurfaces()) {
        INFO("surface: " << surface.name);
        REQUIRE(agentToolsForSurface(surface).size() == surface.toolAllowlist.size());
    }

    std::vector<std::string> operationNames;
    for (const auto& operation : remote::OperationRegistry::instance().operations())
        operationNames.push_back(operation.name.toStdString());
    REQUIRE(invalidSurfaceTools(operationNames).empty());
}

TEST_CASE("Tool definitions carry the registry's contract", "[agent-bridge][surfaces]") {
    const auto tools = agentToolsForSurface(agentSurface(AgentSurfaceId::Arrangement));

    const auto find = [&](const char* name) -> const ToolDefinition* {
        for (const auto& tool : tools)
            if (tool.name == name)
                return &tool;
        return nullptr;
    };

    const auto* list = find("tracks.list");
    REQUIRE(list != nullptr);
    REQUIRE(list->access == ToolAccess::Read);
    REQUIRE(list->description.isNotEmpty());

    const auto* create = find("tracks.create");
    REQUIRE(create != nullptr);
    REQUIRE(create->access == ToolAccess::Mutation);
    // The model sees the same input schema an MCP client is validated against.
    REQUIRE(create->inputSchema["properties"]["name"].isObject());
}

TEST_CASE("A surface is granted only the scopes its allowlist needs", "[agent-bridge][surfaces]") {
    const auto pianoRoll = agentScopesForSurface(agentSurface(AgentSurfaceId::PianoRoll));
    REQUIRE(pianoRoll.has(remote::Scope::Read));
    REQUIRE(pianoRoll.has(remote::Scope::Edit));
    REQUIRE_FALSE(pianoRoll.has(remote::Scope::Session));
    REQUIRE_FALSE(pianoRoll.has(remote::Scope::Transport));
    REQUIRE_FALSE(pianoRoll.has(remote::Scope::HardwareMidi));

    const auto session = agentScopesForSurface(agentSurface(AgentSurfaceId::Session));
    REQUIRE(session.has(remote::Scope::Session));
    REQUIRE(session.has(remote::Scope::Transport));
    REQUIRE_FALSE(session.has(remote::Scope::HardwareMidi));
}

// ===========================================================================
// RemoteAgentToolExecutor
// ===========================================================================

TEST_CASE("The executor runs an allowlisted read through the service", "[agent-bridge][executor]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.createTrack("Drums", TrackType::Media);
    remote::RemoteApiService service(api);
    RemoteAgentToolExecutor executor(service, agentSurface(AgentSurfaceId::Arrangement));

    const auto result = executor.execute(requestFor("call-1", "tracks.list", object({})), {});

    REQUIRE(result.success);
    REQUIRE(result.callId == "call-1");
    REQUIRE_FALSE(result.mutated);
    REQUIRE(result.projectRevision == remote::INITIAL_REVISION);
    const auto* items = result.content.getArray();
    REQUIRE(items != nullptr);
    REQUIRE(items->size() == 1);
}

TEST_CASE("The executor's write mutates and reports the new revision", "[agent-bridge][executor]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    remote::RemoteApiService service(api);
    RemoteAgentToolExecutor executor(service, agentSurface(AgentSurfaceId::Arrangement));

    const auto result =
        executor.execute(requestFor("call-1", "project.setTempo", object({{"tempo", 132.0}})), {});

    REQUIRE(result.success);
    REQUIRE(result.mutated);
    REQUIRE(result.projectRevision == remote::INITIAL_REVISION + 1);
    REQUIRE(api.project_.info.tempo == 132.0);
}

TEST_CASE("A reused tool-call id in a later run is not answered from cache",
          "[agent-bridge][executor]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    remote::RemoteApiService service(api);
    const auto& surface = agentSurface(AgentSurfaceId::Arrangement);

    // Providers number tool calls per response, so two runs both open with
    // "call_0". The service caches writes by clientId + requestId, and the
    // surface's clientId never changes — only the executor's per-run namespace
    // keeps the second write from replaying the first one's cached response.
    RemoteAgentToolExecutor firstRun(service, surface);
    const auto first =
        firstRun.execute(requestFor("call_0", "project.setTempo", object({{"tempo", 120.0}})), {});
    REQUIRE(first.success);
    REQUIRE(api.project_.info.tempo == 120.0);

    RemoteAgentToolExecutor secondRun(service, surface);
    const auto second =
        secondRun.execute(requestFor("call_0", "project.setTempo", object({{"tempo", 132.0}})), {});
    REQUIRE(second.success);
    REQUIRE(api.project_.info.tempo == 132.0);
    REQUIRE(second.projectRevision == remote::INITIAL_REVISION + 2);
}

TEST_CASE("The executor refuses a call outside the surface allowlist", "[agent-bridge][executor]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    remote::RemoteApiService service(api);
    // The piano-roll surface carries no device operations at all.
    RemoteAgentToolExecutor executor(service, agentSurface(AgentSurfaceId::PianoRoll));

    const auto result =
        executor.execute(requestFor("call-1", "devices.setParameter", object({})), {});

    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.has_value());
    REQUIRE(result.error->code == "tool_not_allowed");
    REQUIRE(service.currentRevision() == remote::INITIAL_REVISION);
}

TEST_CASE("A service refusal comes back as the dispatcher's error code",
          "[agent-bridge][executor]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    remote::RemoteApiService service(api);
    RemoteAgentToolExecutor executor(service, agentSurface(AgentSurfaceId::Arrangement));

    const auto result =
        executor.execute(requestFor("call-1", "tracks.get", object({{"trackId", 9999}})), {});

    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.has_value());
    REQUIRE(result.error->code == "not_found");
    // Failures still report the current revision; anything older reads to the
    // runtime as a broken executor.
    REQUIRE(result.projectRevision == service.currentRevision());
}

// ===========================================================================
// The loop end to end: runtime + bridge + service
// ===========================================================================

TEST_CASE("An agent run reads, writes, and completes through the service",
          "[agent-bridge][runtime]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    remote::RemoteApiService service(api);
    const auto& surface = agentSurface(AgentSurfaceId::Arrangement);
    RemoteAgentToolExecutor executor(service, surface);

    ScriptedModel model;
    int step = 0;
    model.handler = [&](const ModelRequest&, const CancellationToken&) -> ModelResponse {
        ++step;
        if (step == 1)
            return {.success = true,
                    .toolCalls = {{.id = "c1",
                                   .name = "project.setTempo",
                                   .arguments = object({{"tempo", 132.0}})}}};
        return {.success = true, .text = "Tempo set to 132."};
    };

    AgentDefinition definition{.id = "console-arrangement",
                               .systemPrompt = "Act via tools.",
                               .tools = agentToolsForSurface(surface)};
    definition.budget.maxSteps = 4;

    AgentRuntime runtime(model, executor);
    const auto result = runtime.run(definition, {.userMessage = "set the tempo to 132"});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE(result.finalText == "Tempo set to 132.");
    REQUIRE(result.mutations == 1);
    REQUIRE(result.finalRevision == remote::INITIAL_REVISION + 1);
    REQUIRE(api.project_.info.tempo == 132.0);
}

TEST_CASE("A model calling past its definition is refused without touching the service",
          "[agent-bridge][runtime]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    remote::RemoteApiService service(api);
    const auto& surface = agentSurface(AgentSurfaceId::PianoRoll);
    RemoteAgentToolExecutor executor(service, surface);

    ScriptedModel model;
    int step = 0;
    model.handler = [&](const ModelRequest& request, const CancellationToken&) -> ModelResponse {
        ++step;
        if (step == 1)
            return {.success = true,
                    .toolCalls = {
                        {.id = "c1", .name = "devices.setParameter", .arguments = object({})}}};
        // The refusal reaches the model as a tool error it can read.
        const auto& lastTurn = request.conversation.back();
        REQUIRE(lastTurn.role == ConversationRole::Tool);
        REQUIRE(lastTurn.toolResult.has_value());
        REQUIRE(lastTurn.toolResult->error.has_value());
        REQUIRE(lastTurn.toolResult->error->code == "tool_not_allowed");
        return {.success = true, .text = "That tool is not available here."};
    };

    AgentDefinition definition{.id = "console-piano-roll",
                               .systemPrompt = "Act via tools.",
                               .tools = agentToolsForSurface(surface)};
    definition.budget.maxSteps = 4;

    AgentRuntime runtime(model, executor);
    const auto result = runtime.run(definition, {.userMessage = "tweak the filter"});

    REQUIRE(result.state == RunState::Completed);
    REQUIRE(api.devices_.parameterWrites.empty());
    REQUIRE(service.currentRevision() == remote::INITIAL_REVISION);
}

// ===========================================================================
// LlmToolModel: runtime types <-> juce_llm types
// ===========================================================================

#include "magda/agents/llm_tool_model.hpp"

namespace {

/// Captures the mapped llm::Request and answers with a scripted llm::Response.
class CapturingLlmClient final : public llm::LLMClient {
  public:
    CapturingLlmClient() : llm::LLMClient(llm::ProviderConfig{}) {}

    mutable std::vector<llm::Request> requests;
    llm::Response scripted;

    llm::Response sendRequest(const llm::Request& request) const override {
        requests.push_back(request);
        return scripted;
    }

    juce::String getName() const override {
        return "capturing";
    }
    juce::String buildRequestBody(const llm::Request&) const override {
        return {};
    }
    juce::String getEndpointUrl() const override {
        return {};
    }
    juce::StringPairArray getHeaders() const override {
        return {};
    }
    llm::Response parseResponseBody(const juce::String&) const override {
        return {};
    }
};

}  // namespace

TEST_CASE("LlmToolModel maps the runtime conversation onto the provider surface",
          "[agent-bridge][llm-model]") {
    CapturingLlmClient client;
    client.scripted.success = true;
    client.scripted.text = "done";
    llm::ToolCall scriptedCall;
    scriptedCall.id = "c9";
    scriptedCall.name = "tracks.list";
    scriptedCall.arguments = object({});
    client.scripted.toolCalls.push_back(scriptedCall);
    client.scripted.totalTokens = 321;

    LlmToolModel model(client);

    ModelRequest request;
    request.systemPrompt = "Be the arrangement agent.";
    request.tools = {{.name = "tracks.list",
                      .description = "List tracks",
                      .inputSchema = object({}),
                      .access = ToolAccess::Read}};
    // A completed earlier iteration: assistant tool call, its result, then the
    // current user turn the provider wants separated out.
    ConversationTurn assistant{.role = ConversationRole::Assistant, .text = "checking"};
    assistant.toolCalls.push_back({.id = "c1", .name = "tracks.list", .arguments = object({})});
    ConversationTurn toolTurn{.role = ConversationRole::Tool};
    toolTurn.toolResult = ToolResult{.callId = "c1",
                                     .toolName = "tracks.list",
                                     .success = true,
                                     .content = object({{"ok", true}})};
    request.conversation = {ConversationTurn{.role = ConversationRole::User, .text = "hi"},
                            assistant, toolTurn,
                            ConversationTurn{.role = ConversationRole::User, .text = "and now?"}};

    const auto response = model.generate(request, {});

    REQUIRE(client.requests.size() == 1);
    const auto& mapped = client.requests.front();
    REQUIRE(mapped.systemPrompt == "Be the arrangement agent.");
    REQUIRE(mapped.tools.size() == 1);
    REQUIRE(mapped.tools.front().name == "tracks.list");
    REQUIRE(static_cast<bool>(mapped.tools.front().annotations["readOnlyHint"]));
    // The trailing user turn became the current message; history kept the rest.
    REQUIRE(mapped.userMessage == "and now?");
    REQUIRE(mapped.messages.size() == 3);
    REQUIRE(mapped.messages[0].role == "user");
    REQUIRE(mapped.messages[1].role == "assistant");
    REQUIRE(mapped.messages[1].toolCalls.size() == 1);
    REQUIRE(mapped.messages[2].role == "tool");
    REQUIRE(mapped.messages[2].toolResults.size() == 1);
    REQUIRE(mapped.messages[2].toolResults.front().callId == "c1");
    REQUIRE_FALSE(mapped.messages[2].toolResults.front().isError);

    // The provider's answer came back in runtime terms.
    REQUIRE(response.success);
    REQUIRE(response.text == "done");
    REQUIRE(response.toolCalls.size() == 1);
    REQUIRE(response.toolCalls.front().id == "c9");
    REQUIRE(response.tokensUsed == 321);
}

TEST_CASE("LlmToolModel surfaces tool errors as error results", "[agent-bridge][llm-model]") {
    CapturingLlmClient client;
    client.scripted.success = true;
    client.scripted.text = "understood";

    LlmToolModel model(client);

    ModelRequest request;
    ConversationTurn toolTurn{.role = ConversationRole::Tool};
    toolTurn.toolResult =
        ToolResult{.callId = "c1",
                   .toolName = "tracks.get",
                   .success = false,
                   .error = ToolError{.code = "not_found", .message = "track 9 not found"}};
    request.conversation = {toolTurn};

    model.generate(request, {});

    REQUIRE(client.requests.size() == 1);
    const auto& mapped = client.requests.front();
    REQUIRE(mapped.userMessage.isEmpty());
    REQUIRE(mapped.messages.size() == 1);
    REQUIRE(mapped.messages[0].toolResults.size() == 1);
    REQUIRE(mapped.messages[0].toolResults.front().isError);
    REQUIRE(mapped.messages[0].toolResults.front().error == "track 9 not found");
}
