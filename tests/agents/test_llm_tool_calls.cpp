#include <juce_llm/juce_llm.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/agents/llama_local_client.hpp"

namespace {
llm::ProviderConfig configFor(llm::Provider provider) {
    llm::ProviderConfig config;
    config.provider = provider;
    config.baseUrl = provider == llm::Provider::Gemini ? "https://generativelanguage.googleapis.com"
                                                       : "https://example.test/v1";
    config.model = "test-model";
    return config;
}

llm::ToolDefinition weatherTool() {
    llm::ToolDefinition tool;
    tool.name = "get_weather";
    tool.description = "Get weather for a city";
    tool.inputSchema = llm::Schema::object({{"city", llm::Schema::string()}});
    return tool;
}

llm::ToolCall weatherCall(juce::String id = "call-1") {
    llm::ToolCall call;
    call.id = std::move(id);
    call.name = "get_weather";
    call.rawArguments = R"({"city":"London"})";
    call.arguments = juce::JSON::parse(call.rawArguments);
    return call;
}

llm::ToolResult weatherResult(juce::String id = "call-1") {
    llm::ToolResult result;
    result.callId = std::move(id);
    result.name = "get_weather";
    result.content = juce::JSON::parse(R"({"temperature":18})");
    return result;
}

void addDeltas(llm::StreamAccumulator& accumulator, const llm::LLMClient& client,
               std::initializer_list<juce::String> chunks) {
    for (const auto& chunk : chunks)
        for (const auto& delta : client.parseStreamDeltas(chunk))
            accumulator.add(delta);
}
}  // namespace

TEST_CASE("LLM tool conversation round-trips calls, results, and provider data",
          "[llm][tools][conversation]") {
    auto definition = weatherTool();
    definition.annotations = juce::JSON::parse(R"({"readOnlyHint":true,"idempotentHint":true})");
    auto restoredDefinition = llm::ToolDefinition::fromVar(definition.toVar());
    CHECK(restoredDefinition.name == "get_weather");
    CHECK(static_cast<bool>(restoredDefinition.annotations["readOnlyHint"]));
    CHECK(static_cast<bool>(restoredDefinition.annotations["idempotentHint"]));

    llm::Conversation original;
    original.messages.emplace_back("user", "Weather?");

    llm::Message assistant("assistant", "Checking.");
    auto call = weatherCall();
    call.providerData = juce::JSON::parse(R"({"thoughtSignature":"opaque"})");
    assistant.toolCalls.push_back(call);
    original.messages.push_back(std::move(assistant));

    auto result = weatherResult();
    result.isError = true;
    result.error = "sensor unavailable";
    original.addToolResult(result);
    original.lastResponseId = "resp-1";

    auto restored = llm::Conversation::fromVar(original.toVar());
    REQUIRE(restored.lastResponseId == "resp-1");
    REQUIRE(restored.messages.size() == 3);
    REQUIRE(restored.messages[1].toolCalls.size() == 1);
    CHECK(restored.messages[1].toolCalls[0].id == "call-1");
    CHECK(restored.messages[1].toolCalls[0].arguments["city"].toString() == "London");
    CHECK(restored.messages[1].toolCalls[0].providerData["thoughtSignature"].toString() ==
          "opaque");
    REQUIRE(restored.messages[2].toolResults.size() == 1);
    CHECK(restored.messages[2].toolResults[0].isError);
    CHECK(restored.messages[2].toolResults[0].error == "sensor unavailable");
}

TEST_CASE("OpenAI Chat maps executable tools and tool conversation turns",
          "[llm][tools][openai-chat]") {
    auto client = llm::LLMClientFactory::create(configFor(llm::Provider::OpenAIChat));
    llm::Request request;
    request.systemPrompt = "Use tools.";
    request.tools = {weatherTool()};
    request.toolChoice = {llm::ToolChoiceMode::Specific, "get_weather"};

    llm::Message assistant("assistant", {});
    assistant.toolCalls.push_back(weatherCall());
    request.messages.push_back(std::move(assistant));
    llm::Message toolTurn;
    toolTurn.role = "tool";
    toolTurn.toolResults.push_back(weatherResult());
    request.messages.push_back(std::move(toolTurn));

    auto body = juce::JSON::parse(client->buildRequestBody(request));
    REQUIRE(body["tools"].getArray()->size() == 1);
    CHECK(body["tools"][0]["function"]["name"].toString() == "get_weather");
    CHECK(body["tools"][0]["function"]["parameters"]["type"].toString() == "object");
    CHECK(body["tool_choice"]["function"]["name"].toString() == "get_weather");
    REQUIRE(body["messages"].getArray()->size() == 3);
    CHECK(body["messages"][1]["tool_calls"][0]["id"].toString() == "call-1");
    CHECK(body["messages"][2]["tool_call_id"].toString() == "call-1");

    auto response = client->parseResponseBody(
        R"({"choices":[{"message":{"content":"Checking","tool_calls":[{"id":"call-1","type":"function","function":{"name":"get_weather","arguments":"{\"city\":\"London\"}"}}]}}]})");
    REQUIRE(response.success);
    CHECK(response.text == "Checking");
    REQUIRE(response.toolCalls.size() == 1);
    CHECK(response.toolCalls[0].arguments["city"].toString() == "London");
}

TEST_CASE("OpenAI Responses keeps CFG custom output distinct from executable functions",
          "[llm][tools][openai-responses]") {
    auto client = llm::LLMClientFactory::create(configFor(llm::Provider::OpenAIResponses));
    llm::Request request;
    request.systemPrompt = "Use tools.";
    request.grammar = "start: /.+/";
    request.grammarToolName = "dsl_output";
    request.tools = {weatherTool()};

    auto body = juce::JSON::parse(client->buildRequestBody(request));
    REQUIRE(body["tools"].getArray()->size() == 2);
    CHECK(body["tools"][0]["type"].toString() == "custom");
    CHECK(body["tools"][1]["type"].toString() == "function");

    auto response = client->parseResponseBody(
        R"({"id":"resp-1","output":[{"type":"custom_tool_call","name":"dsl_output","input":"add track"},{"type":"function_call","call_id":"call-1","name":"get_weather","arguments":"{\"city\":\"London\"}"}]})");
    REQUIRE(response.success);
    CHECK(response.text == "add track");
    REQUIRE(response.toolCalls.size() == 1);
    CHECK(response.toolCalls[0].name == "get_weather");

    request.grammar = {};
    request.previousResponseId = "resp-1";
    llm::Message assistant("assistant", {});
    assistant.toolCalls.push_back(weatherCall());
    request.messages.push_back(std::move(assistant));
    llm::Message toolTurn;
    toolTurn.role = "tool";
    toolTurn.toolResults.push_back(weatherResult());
    request.messages.push_back(std::move(toolTurn));
    body = juce::JSON::parse(client->buildRequestBody(request));
    CHECK(body["previous_response_id"].toString() == "resp-1");
    REQUIRE(body["input"].getArray()->size() == 1);
    CHECK(body["input"][0]["type"].toString() == "function_call_output");
    CHECK(body["input"][0]["call_id"].toString() == "call-1");
}

TEST_CASE("Anthropic maps tool definitions, mixed assistant blocks, and results",
          "[llm][tools][anthropic]") {
    auto client = llm::LLMClientFactory::create(configFor(llm::Provider::Anthropic));
    llm::Request request;
    request.tools = {weatherTool()};
    request.toolChoice = {llm::ToolChoiceMode::Required, {}};

    llm::Message assistant("assistant", "Checking");
    assistant.toolCalls.push_back(weatherCall("toolu-1"));
    request.messages.push_back(std::move(assistant));
    llm::Message toolTurn;
    toolTurn.role = "tool";
    toolTurn.toolResults.push_back(weatherResult("toolu-1"));
    request.messages.push_back(std::move(toolTurn));

    auto body = juce::JSON::parse(client->buildRequestBody(request));
    CHECK(body["tools"][0]["input_schema"]["type"].toString() == "object");
    CHECK(body["tool_choice"]["type"].toString() == "any");
    CHECK(body["messages"][0]["content"][1]["type"].toString() == "tool_use");
    CHECK(body["messages"][1]["content"][0]["type"].toString() == "tool_result");

    auto response = client->parseResponseBody(
        R"({"content":[{"type":"text","text":"Checking"},{"type":"tool_use","id":"toolu-1","name":"get_weather","input":{"city":"London"}}]})");
    REQUIRE(response.success);
    CHECK(response.text == "Checking");
    REQUIRE(response.toolCalls.size() == 1);
    CHECK(response.toolCalls[0].id == "toolu-1");
}

TEST_CASE("Gemini maps function declarations, call IDs, results, and thought signatures",
          "[llm][tools][gemini]") {
    auto client = llm::LLMClientFactory::create(configFor(llm::Provider::Gemini));
    llm::Request request;
    request.tools = {weatherTool()};
    request.toolChoice = {llm::ToolChoiceMode::Specific, "get_weather"};

    auto body = juce::JSON::parse(client->buildRequestBody(request));
    CHECK(body["tools"][0]["functionDeclarations"][0]["name"].toString() == "get_weather");
    CHECK(body["toolConfig"]["functionCallingConfig"]["mode"].toString() == "ANY");
    CHECK(body["toolConfig"]["functionCallingConfig"]["allowedFunctionNames"][0].toString() ==
          "get_weather");

    auto response = client->parseResponseBody(
        R"({"responseId":"gem-resp","candidates":[{"content":{"parts":[{"text":"Checking"},{"functionCall":{"id":"call-1","name":"get_weather","args":{"city":"London"}},"thoughtSignature":"opaque"}]}}]})");
    REQUIRE(response.success);
    REQUIRE(response.toolCalls.size() == 1);
    CHECK(response.toolCalls[0].id == "call-1");
    CHECK(response.toolCalls[0].providerData["thoughtSignature"].toString() == "opaque");

    llm::Message assistant("assistant", response.text);
    assistant.toolCalls = response.toolCalls;
    request.messages.push_back(std::move(assistant));
    llm::Message toolTurn;
    toolTurn.role = "tool";
    toolTurn.toolResults.push_back(weatherResult());
    request.messages.push_back(std::move(toolTurn));
    body = juce::JSON::parse(client->buildRequestBody(request));
    CHECK(body["contents"][0]["parts"][1]["thoughtSignature"].toString() == "opaque");
    CHECK(body["contents"][1]["parts"][0]["functionResponse"]["id"].toString() == "call-1");
}

TEST_CASE("Provider stream deltas assemble partial and parallel tool calls",
          "[llm][tools][streaming]") {
    SECTION("OpenAI Chat interleaves two calls") {
        auto client = llm::LLMClientFactory::create(configFor(llm::Provider::OpenAIChat));
        llm::StreamAccumulator accumulator;
        addDeltas(
            accumulator, *client,
            {R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"a","function":{"name":"one","arguments":"{\"x\":"}},{"index":1,"id":"b","function":{"name":"two","arguments":"{\"y\":"}}]}}]})",
             R"({"choices":[{"delta":{"tool_calls":[{"index":1,"function":{"arguments":"2}"}},{"index":0,"function":{"arguments":"1}"}}]}}]})"});
        auto response = accumulator.finish();
        REQUIRE(response.success);
        REQUIRE(response.toolCalls.size() == 2);
        CHECK(static_cast<int>(response.toolCalls[0].arguments["x"]) == 1);
        CHECK(static_cast<int>(response.toolCalls[1].arguments["y"]) == 2);
    }

    SECTION("OpenAI Responses accepts sparse output indexes") {
        auto client = llm::LLMClientFactory::create(configFor(llm::Provider::OpenAIResponses));
        llm::StreamAccumulator accumulator;
        addDeltas(
            accumulator, *client,
            {R"({"type":"response.output_item.added","output_index":3,"item":{"type":"function_call","call_id":"call-1","name":"get_weather"}})",
             R"({"type":"response.function_call_arguments.delta","output_index":3,"delta":"{\"city\":"})",
             R"({"type":"response.function_call_arguments.delta","output_index":3,"delta":"\"London\"}"})"});
        auto response = accumulator.finish();
        REQUIRE(response.toolCalls.size() == 1);
        CHECK(response.toolCalls[0].id == "call-1");
        CHECK(response.toolCalls[0].arguments["city"].toString() == "London");
    }

    SECTION("Anthropic assembles input_json_delta after a text block") {
        auto client = llm::LLMClientFactory::create(configFor(llm::Provider::Anthropic));
        llm::StreamAccumulator accumulator;
        addDeltas(
            accumulator, *client,
            {R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu-1","name":"get_weather","input":{}}})",
             R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"city\":"}})",
             R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\"London\"}"}})"});
        auto response = accumulator.finish();
        REQUIRE(response.toolCalls.size() == 1);
        CHECK(response.toolCalls[0].arguments["city"].toString() == "London");
    }

    SECTION("Gemini preserves native streamed function call IDs") {
        auto client = llm::LLMClientFactory::create(configFor(llm::Provider::Gemini));
        llm::StreamAccumulator accumulator;
        addDeltas(
            accumulator, *client,
            {R"({"responseId":"gem-resp","candidates":[{"content":{"parts":[{"functionCall":{"id":"gem-call-1","name":"get_weather","args":{"city":"London"}},"thoughtSignature":"stream-opaque"}]}}]})"});
        auto response = accumulator.finish();
        REQUIRE(response.toolCalls.size() == 1);
        CHECK(response.toolCalls[0].id == "gem-call-1");
        CHECK(response.toolCalls[0].arguments["city"].toString() == "London");
        CHECK(response.toolCalls[0].providerData["thoughtSignature"].toString() == "stream-opaque");
    }

    SECTION("Malformed arguments remain a structured tool-call failure") {
        llm::StreamAccumulator accumulator;
        accumulator.add({llm::StreamDeltaType::ToolCallStart, 0, {}, "bad", "broken_tool", {}, {}});
        accumulator.add({llm::StreamDeltaType::ToolCallArguments, 0, {}, {}, {}, "{not-json", {}});
        auto response = accumulator.finish();
        REQUIRE(response.success);
        REQUIRE(response.toolCalls.size() == 1);
        CHECK_FALSE(response.toolCalls[0].isValid());
        CHECK(response.toolCalls[0].error.contains("Malformed tool arguments"));
        CHECK(response.text.isEmpty());
    }
}

TEST_CASE("Embedded local backend rejects native tools explicitly", "[llm][tools][local]") {
    magda::LlamaLocalClient client;
    llm::Request request;
    request.tools = {weatherTool()};

    auto response = client.sendRequest(request);
    CHECK_FALSE(response.success);
    CHECK(response.error.contains("not supported"));

    response = client.sendStreamingRequest(request, {});
    CHECK_FALSE(response.success);
    CHECK(response.error.contains("not supported"));
}
