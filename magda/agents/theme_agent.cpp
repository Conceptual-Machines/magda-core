#include "theme_agent.hpp"

#include <juce_core/juce_core.h>

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

ThemeAgent::Result ThemeAgent::generate(const std::string& systemPrompt,
                                        const std::string& userMessage, const juce::var& schema) {
    Result result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    // Reuse the MUSIC role: the user already has it configured for creative
    // generation, and a dedicated theme provider isn't warranted yet.
    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::THEME);
    auto providerConfig = toLLMProviderConfig(agentConfig, "theme");

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Theme agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "theme");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(systemPrompt.c_str());
    request.userMessage = juce::String::fromUTF8(userMessage.c_str());
    request.schema = schema;  // structured output when the caller supplies one
    // A little warmer than sound design: palettes benefit from creative hue
    // choices, but still structured enough to stay coherent.
    request.temperature = 0.5f;

    auto response = client->sendRequest(request);
    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.text = response.text.trim().toStdString();
    return result;
}

ThemeAgent::Result ThemeAgent::generateStreaming(const std::string& systemPrompt,
                                                 const std::string& userMessage,
                                                 const juce::var& schema, TokenCallback onToken) {
    Result result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::THEME);
    auto providerConfig = toLLMProviderConfig(agentConfig, "theme");

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Theme agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "theme");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(systemPrompt.c_str());
    request.userMessage = juce::String::fromUTF8(userMessage.c_str());
    request.schema = schema;
    request.temperature = 0.5f;

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

    result.text = response.text.trim().toStdString();
    return result;
}

}  // namespace magda
