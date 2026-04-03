#pragma once

#include <juce_llm/juce_llm.h>

#include "../daw/core/Config.hpp"
#include "llm_presets.hpp"
#include "openai_codex_auth.hpp"
#include "version.hpp"

namespace magda {

/** Map provider string to llm::Provider enum.
    "deepseek" and "openrouter" are OpenAI-compatible services with their own
    credentials and base URLs — they map to the same OpenAIChat wire format. */
inline llm::Provider providerFromString(const std::string& s) {
    if (s == "openai_responses")
        return llm::Provider::OpenAIResponses;
    if (s == provider::OPENAI_CODEX)
        return llm::Provider::OpenAIResponses;
    if (s == provider::ANTHROPIC)
        return llm::Provider::Anthropic;
    if (s == provider::GEMINI)
        return llm::Provider::Gemini;
    // deepseek, openrouter, openai_chat all use the OpenAI Chat Completions format
    return llm::Provider::OpenAIChat;
}

/** Default base URL for a provider string. */
inline juce::String defaultBaseUrl(const std::string& providerStr) {
    if (providerStr == provider::OPENAI_CODEX)
        return "https://chatgpt.com/backend-api/codex";
    if (providerStr == provider::DEEPSEEK)
        return "https://api.deepseek.com";
    if (providerStr == provider::OPENROUTER)
        return "https://openrouter.ai/api/v1";
    if (providerStr == provider::ANTHROPIC)
        return "https://api.anthropic.com/v1";
    if (providerStr == provider::GEMINI)
        return "https://generativelanguage.googleapis.com";
    return "https://api.openai.com/v1";
}

inline juce::String normalizeCodexModel(juce::String model) {
    if (model == "codex/gpt-5-mini" || model == "gpt-5-mini")
        return model::CODEX_GPT_5_CODEX;
    if (model == "codex/gpt-4o-mini" || model == "gpt-4o-mini")
        return model::CODEX_O4_MINI;
    return model;
}

/** Convert AgentLLMConfig to juce-llm ProviderConfig.
    agentName is included in User-Agent (e.g. "MAGDA/0.3.0 (command)"). */
inline llm::ProviderConfig toLLMProviderConfig(const Config::AgentLLMConfig& config,
                                               const std::string& agentName = {}) {
    auto provider = providerFromString(config.provider);
    auto providerSettings = Config::getInstance().getAIProviderConfig(config.provider);

    if (config.provider == provider::OPENAI_CODEX) {
        auto refreshedSettings = providerSettings;
        if (auto refreshResult = codex_auth::ensureValidProviderConfig(refreshedSettings);
            refreshResult.wasOk() && refreshedSettings.apiKey != providerSettings.apiKey) {
            Config::getInstance().setAIProviderConfig(config.provider, refreshedSettings);
            Config::getInstance().save();
            providerSettings = refreshedSettings;
        }
    }

    llm::ProviderConfig pc;
    pc.provider = provider;
    pc.model = juce::String(config.model);
    pc.baseUrl =
        config.baseUrl.empty() ? defaultBaseUrl(config.provider) : juce::String(config.baseUrl);
    pc.useCodexBackend = config.provider == provider::OPENAI_CODEX;
    if (pc.useCodexBackend)
        pc.model = normalizeCodexModel(pc.model);
    if (pc.useCodexBackend)
        pc.codexAccountId = juce::String(providerSettings.accountId);

    // API key: per-agent value first, then per-provider credential, then env var
    if (!config.apiKey.empty()) {
        pc.apiKey = juce::String(config.apiKey);
    } else {
        auto credential = providerSettings.apiKey;

        if (!credential.empty()) {
            pc.apiKey = juce::String(credential);
        } else {
            // Env var fallback by provider
            const char* envVar = nullptr;
            if (config.provider == provider::OPENAI_CODEX)
                envVar = std::getenv("OPENAI_CODEX_API_KEY");
            else if (provider == llm::Provider::OpenAIChat ||
                     provider == llm::Provider::OpenAIResponses)
                envVar = std::getenv("OPENAI_API_KEY");
            else if (provider == llm::Provider::Anthropic)
                envVar = std::getenv("ANTHROPIC_API_KEY");
            else if (provider == llm::Provider::Gemini)
                envVar = std::getenv("GEMINI_API_KEY");
            if (envVar)
                pc.apiKey = juce::String(envVar);
        }
    }

    auto normalizedModel = pc.model;
    if (normalizedModel.startsWith("codex/"))
        normalizedModel = normalizedModel.fromFirstOccurrenceOf("/", false, false);

    // GPT-5 does not support temperature, uses reasoning effort instead
    if (normalizedModel.startsWith("gpt-5")) {
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
