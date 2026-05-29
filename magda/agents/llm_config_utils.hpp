#pragma once

#include <juce_llm/juce_llm.h>

#include "../daw/core/Config.hpp"
#include "llm_presets.hpp"
#include "version.hpp"

namespace magda {

/** Map provider string to llm::Provider enum.
    "deepseek" and "openrouter" are OpenAI-compatible services with their own
    credentials and base URLs — they map to the same OpenAIChat wire format. */
inline llm::Provider providerFromString(const std::string& s) {
    if (s == provider::OPENAI_RESPONSES)
        return llm::Provider::OpenAIResponses;
    if (s == provider::ANTHROPIC)
        return llm::Provider::Anthropic;
    if (s == provider::GEMINI)
        return llm::Provider::Gemini;
    // deepseek, openrouter, openai_chat, ollama all use the OpenAI Chat Completions format
    return llm::Provider::OpenAIChat;
}

/** Default base URL for a provider string. */
inline juce::String defaultBaseUrl(const std::string& providerStr) {
    if (providerStr == provider::DEEPSEEK)
        return "https://api.deepseek.com";
    if (providerStr == provider::OPENROUTER)
        return "https://openrouter.ai/api/v1";
    if (providerStr == provider::ANTHROPIC)
        return "https://api.anthropic.com/v1";
    if (providerStr == provider::GEMINI)
        return "https://generativelanguage.googleapis.com";
    if (providerStr == provider::OLLAMA)
        return DEFAULT_OLLAMA_BASE_URL;
    // openai_chat and openai_responses share the same base URL
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

    // Ollama: the "credential" slot stores the base URL override (not a key).
    // Per-agent baseUrl wins; otherwise fall back to the credential string;
    // otherwise the default localhost URL. Inject a placeholder bearer token so
    // the Authorization header is well-formed (Ollama ignores it).
    if (config.provider == provider::OLLAMA) {
        if (config.baseUrl.empty()) {
            auto urlOverride = Config::getInstance().getAICredential(provider::OLLAMA);
            if (!urlOverride.empty())
                pc.baseUrl = juce::String(urlOverride);
        }
        // Normalise: the OpenAI-Chat client appends "/chat/completions"
        // verbatim. Accept both http://host:port and http://host:port/v1
        // from the user, always send to http://host:port/v1/chat/completions.
        if (pc.baseUrl.endsWithChar('/'))
            pc.baseUrl = pc.baseUrl.dropLastCharacters(1);
        if (!pc.baseUrl.endsWith("/v1"))
            pc.baseUrl += "/v1";
        // User-picked model override (set from the Cloud tab Model dropdown)
        auto modelOverride = Config::getInstance().getOllamaModel();
        if (!modelOverride.empty())
            pc.model = juce::String(modelOverride);
        if (pc.apiKey.isEmpty()) {
            // OpenAI-compat servers like GPUStack require a real key; vanilla
            // Ollama doesn't care. Use the user-supplied key when set,
            // otherwise a harmless placeholder so the Authorization header
            // is well-formed.
            auto k = Config::getInstance().getOllamaApiKey();
            pc.apiKey = k.empty() ? juce::String("ollama") : juce::String(k);
        }
        pc.userAgent = juce::String("MAGDA/") + MAGDA_VERSION;
        if (!agentName.empty())
            pc.userAgent += " (" + juce::String(agentName) + ")";
        pc.appUrl = "https://magda.dev";
        return pc;
    }

    // API key: per-agent value first, then per-provider credential, then env var
    if (!config.apiKey.empty()) {
        pc.apiKey = juce::String(config.apiKey);
    } else {
        auto credential = Config::getInstance().getAICredential(config.provider);

        // openai_responses shares credentials with openai_chat
        if (credential.empty() && config.provider == provider::OPENAI_RESPONSES)
            credential = Config::getInstance().getAICredential(provider::OPENAI_CHAT);

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

    // GPT-5 does not support temperature, uses reasoning effort instead.
    // All agents use "low" effort — keeps latency down; quality is steered
    // by model choice (nano/mini/5/5.4) rather than reasoning depth.
    if (pc.model.startsWith("gpt-5")) {
        pc.noTemperature = true;
        if (pc.reasoningEffort.isEmpty())
            pc.reasoningEffort = "low";
    }

    // Application identity headers
    pc.userAgent = juce::String("MAGDA/") + MAGDA_VERSION;
    if (!agentName.empty())
        pc.userAgent += " (" + juce::String(agentName) + ")";
    pc.appUrl = "https://magda.dev";

    return pc;
}

/** CFG grammar support is currently wired only for the GPT-5 Responses path. */
inline bool supportsOpenAICFG(const Config::AgentLLMConfig& config) {
    auto pc = toLLMProviderConfig(config);
    return pc.provider == llm::Provider::OpenAIResponses && pc.model.startsWith("gpt-5");
}

}  // namespace magda
