#include "llm_presets.hpp"

namespace magda {

using AC = Config::AgentLLMConfig;

const std::vector<LLMPreset>& getBuiltInPresets() {
    static const std::vector<LLMPreset> presets = {
        {
            preset::LOCAL_EMBEDDED,
            "Local (Embedded)",
            {
                {role::ROUTER, {provider::LLAMA_LOCAL, "", "", ""}},
                {role::COMMAND, {provider::LLAMA_LOCAL, "", "", ""}},
                {role::MUSIC, {provider::LLAMA_LOCAL, "", "", ""}},
            },
        },
        {
            preset::CLOUD_OPENAI,
            "Cloud (OpenAI)",
            {
                {role::ROUTER, {provider::OPENAI, "", "", "gpt-4.1"}},
                {role::COMMAND, {provider::OPENAI, "", "", "gpt-5"}},
                {role::MUSIC, {provider::OPENAI, "", "", "gpt-5"}},
            },
        },
        {
            preset::CLOUD_ANTHROPIC,
            "Cloud (Anthropic)",
            {
                {role::ROUTER, {provider::ANTHROPIC, "", "", "claude-haiku-4-5-20251001"}},
                {role::COMMAND, {provider::ANTHROPIC, "", "", "claude-sonnet-4-6"}},
                {role::MUSIC, {provider::ANTHROPIC, "", "", "claude-opus-4-6"}},
            },
        },
        {
            preset::CLOUD_GEMINI,
            "Cloud (Gemini)",
            {
                {role::ROUTER, {provider::GEMINI, "", "", "gemini-2.0-flash"}},
                {role::COMMAND, {provider::GEMINI, "", "", "gemini-2.0-flash"}},
                {role::MUSIC, {provider::GEMINI, "", "", "gemini-2.5-pro"}},
            },
        },
        {
            preset::CLOUD_DEEPSEEK,
            "Cloud (DeepSeek)",
            {
                {role::ROUTER, {provider::DEEPSEEK, "", "", "deepseek-chat"}},
                {role::COMMAND, {provider::DEEPSEEK, "", "", "deepseek-chat"}},
                {role::MUSIC, {provider::DEEPSEEK, "", "", "deepseek-reasoner"}},
            },
        },
        {
            preset::CLOUD_OPENROUTER,
            "Cloud (OpenRouter)",
            {
                {role::ROUTER, {provider::OPENROUTER, "", "", "meta-llama/llama-3.3-70b-instruct"}},
                {role::COMMAND,
                 {provider::OPENROUTER, "", "", "meta-llama/llama-3.3-70b-instruct"}},
                {role::MUSIC, {provider::OPENROUTER, "", "", "meta-llama/llama-3.3-70b-instruct"}},
            },
        },
        {
            preset::HYBRID_COST,
            "Hybrid - Optimize for Cost",
            {
                {role::ROUTER, {provider::LLAMA_LOCAL, "", "", ""}},
                {role::COMMAND, {provider::LLAMA_LOCAL, "", "", ""}},
                {role::MUSIC, {provider::OPENAI, "", "", "gpt-5"}},
            },
        },
        {
            preset::HYBRID_SPEED,
            "Hybrid - Optimize for Speed",
            {
                {role::ROUTER, {provider::LLAMA_LOCAL, "", "", ""}},
                {role::COMMAND, {provider::OPENAI, "", "", "gpt-5"}},
                {role::MUSIC, {provider::OPENAI, "", "", "gpt-5"}},
            },
        },
    };
    return presets;
}

const LLMPreset* findPreset(const std::string& id) {
    for (const auto& p : getBuiltInPresets())
        if (p.id == id)
            return &p;
    return nullptr;
}

}  // namespace magda
