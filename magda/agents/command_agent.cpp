#include "command_agent.hpp"

#include "../daw/core/Config.hpp"
#include "dsl_grammar.hpp"
#include "dsl_interpreter.hpp"
#include "llm_client_factory.hpp"

namespace magda {

const char* CommandAgent::getSystemPrompt() {
    return dsl::getToolDescription();
}

/** Check if provider supports CFG grammar (OpenAI Responses API). */
static bool usesCFG(const Config::AgentLLMConfig& config) {
    // OpenAI direct (no custom baseUrl) — use Responses API with CFG
    return config.provider == "openai_chat" && config.baseUrl.empty();
}

/** Build an LLM client for the command agent, routing OpenAI to Responses API. */
static std::unique_ptr<llm::LLMClient> createCommandClient(const Config::AgentLLMConfig& config) {
    if (usesCFG(config)) {
        // Route to OpenAI Responses API for CFG support
        auto pc = toLLMProviderConfig(config);
        pc.provider = llm::Provider::OpenAIResponses;
        return llm::LLMClientFactory::create(pc);
    }
    return createLLMClient(config);
}

/** Build the LLM request, adding CFG grammar when supported. */
static llm::Request buildRequest(const std::string& message, bool cfg) {
    auto stateJson = dsl::Interpreter::buildStateSnapshot();
    auto systemPrompt = juce::String(CommandAgent::getSystemPrompt());
    if (stateJson.isNotEmpty())
        systemPrompt += "\n\nCurrent DAW state:\n" + stateJson;

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String(message);
    request.temperature = 0.1f;

    if (cfg) {
        request.grammar = juce::String(dsl::getGrammar());
        request.grammarToolName = "magda_dsl";
        request.grammarToolDescription = systemPrompt;
    }

    return request;
}

CommandAgent::GenerateResult CommandAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig("command");

    if (agentConfig.provider != "llama_local") {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Command agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    bool cfg = usesCFG(agentConfig);
    auto client = createCommandClient(agentConfig);
    auto request = buildRequest(message, cfg);

    auto response = client->sendRequest(request);

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.dslOutput = response.text.trim().toStdString();

    DBG("MAGDA CommandAgent (" + client->getName() + ", " + juce::String(response.wallSeconds, 2) +
        "s): " + juce::String(result.dslOutput));

    return result;
}

CommandAgent::GenerateResult CommandAgent::generateStreaming(const std::string& message,
                                                             TokenCallback onToken) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig("command");

    if (agentConfig.provider != "llama_local") {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Command agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    bool cfg = usesCFG(agentConfig);
    auto client = createCommandClient(agentConfig);
    auto request = buildRequest(message, cfg);

    // CFG via Responses API doesn't support streaming — fall back to sync
    if (cfg) {
        auto response = client->sendRequest(request);
        if (!response.success) {
            result.error = response.error.toStdString();
            result.hasError = true;
            return result;
        }
        result.dslOutput = response.text.trim().toStdString();
        DBG("MAGDA CommandAgent CFG (" + client->getName() + ", " +
            juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.dslOutput));
        return result;
    }

    auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
        if (shouldStop_.load())
            return false;
        if (onToken)
            return onToken(token);
        return true;
    });

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.dslOutput = response.text.trim().toStdString();

    DBG("MAGDA CommandAgent stream (" + client->getName() + ", " +
        juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.dslOutput));

    return result;
}

}  // namespace magda
