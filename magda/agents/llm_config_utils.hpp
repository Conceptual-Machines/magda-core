#pragma once

#include <juce_llm/juce_llm.h>

#include "../daw/core/Config.hpp"
#include "version.hpp"

namespace magda {

/** Map provider string to llm::Provider enum.
    "deepseek" and "openrouter" are OpenAI-compatible services with their own
    credentials and base URLs — they map to the same OpenAIChat wire format. */
inline llm::Provider providerFromString(const std::string& s) {
    if (s == "openai_responses")
        return llm::Provider::OpenAIResponses;
    if (s == "anthropic")
        return llm::Provider::Anthropic;
    if (s == "gemini")
        return llm::Provider::Gemini;
    // deepseek, openrouter, openai_chat all use the OpenAI Chat Completions format
    return llm::Provider::OpenAIChat;
}

/** Default base URL for a provider string. */
inline juce::String defaultBaseUrl(const std::string& providerStr) {
    if (providerStr == "deepseek")
        return "https://api.deepseek.com";
    if (providerStr == "openrouter")
        return "https://openrouter.ai/api/v1";
    if (providerStr == "anthropic")
        return "https://api.anthropic.com/v1";
    if (providerStr == "gemini")
        return "https://generativelanguage.googleapis.com";
    return "https://api.openai.com/v1";
}

/** Convert AgentLLMConfig to juce-llm ProviderConfig.
    agentName is included in User-Agent (e.g. "MAGDA/0.3.0 (command)"). */
inline llm::ProviderConfig toLLMProviderConfig(const Config::AgentLLMConfig& config,
                                               const std::string& agentName = {}) {
    auto provider = providerFromString(config.provider);

    llm::ProviderConfig pc;
    pc.provider = provider;
    pc.model = juce::String(config.model);
    pc.baseUrl =
        config.baseUrl.empty() ? defaultBaseUrl(config.provider) : juce::String(config.baseUrl);

    // API key: per-agent value first, then per-provider credential, then env var
    if (!config.apiKey.empty()) {
        pc.apiKey = juce::String(config.apiKey);
    } else {
        auto credential = Config::getInstance().getAICredential(config.provider);

        if (!credential.empty()) {
            pc.apiKey = juce::String(credential);
        } else {
            // Env var fallback by provider
            const char* envVar = nullptr;
            if (provider == llm::Provider::OpenAIChat || provider == llm::Provider::OpenAIResponses)
                envVar = std::getenv("OPENAI_API_KEY");
            else if (provider == llm::Provider::Anthropic)
                envVar = std::getenv("ANTHROPIC_API_KEY");
            else if (provider == llm::Provider::Gemini)
                envVar = std::getenv("GEMINI_API_KEY");
            if (envVar)
                pc.apiKey = juce::String(envVar);
        }
    }

    // GPT-5 does not support temperature, uses reasoning effort instead
    if (pc.model.startsWith("gpt-5")) {
        pc.noTemperature = true;
        if (pc.reasoningEffort.isEmpty()) {
            if (agentName == "router")
                pc.reasoningEffort = "low";
            else
                pc.reasoningEffort = "medium";
        }
    }

    // Anthropic output effort — router needs speed, others default
    if (provider == llm::Provider::Anthropic && pc.reasoningEffort.isEmpty()) {
        if (agentName == "router")
            pc.reasoningEffort = "low";
    }

    // Application identity headers
    pc.userAgent = juce::String("MAGDA/") + MAGDA_VERSION;
    if (!agentName.empty())
        pc.userAgent += " (" + juce::String(agentName) + ")";
    pc.appUrl = "https://magda.dev";

    return pc;
}

}  // namespace magda
