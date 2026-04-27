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
                {role::CONTROLLER, {provider::LLAMA_LOCAL, "", "", ""}},
            },
        },
        {
            preset::CLOUD_OPENAI,
            "Cloud (OpenAI)",
            {
                {role::ROUTER, {provider::OPENAI_CHAT, "", "", model::GPT_4_1}},
                {role::COMMAND, {provider::OPENAI_RESPONSES, "", "", model::GPT_5}},
                {role::MUSIC, {provider::OPENAI_RESPONSES, "", "", model::GPT_5_5}},
                {role::CONTROLLER, {provider::OPENAI_RESPONSES, "", "", model::GPT_5}},
            },
        },
        {
            preset::CLOUD_ANTHROPIC,
            "Cloud (Anthropic)",
            {
                {role::ROUTER, {provider::ANTHROPIC, "", "", model::CLAUDE_HAIKU}},
                {role::COMMAND, {provider::ANTHROPIC, "", "", model::CLAUDE_SONNET}},
                {role::MUSIC, {provider::ANTHROPIC, "", "", model::CLAUDE_OPUS}},
                {role::CONTROLLER, {provider::ANTHROPIC, "", "", model::CLAUDE_SONNET}},
            },
        },
        {
            preset::CLOUD_GEMINI,
            "Cloud (Gemini)",
            {
                {role::ROUTER, {provider::GEMINI, "", "", model::GEMINI_FLASH}},
                {role::COMMAND, {provider::GEMINI, "", "", model::GEMINI_FLASH}},
                {role::MUSIC, {provider::GEMINI, "", "", model::GEMINI_PRO}},
                {role::CONTROLLER, {provider::GEMINI, "", "", model::GEMINI_PRO}},
            },
        },
        {
            preset::CLOUD_DEEPSEEK,
            "Cloud (DeepSeek)",
            {
                {role::ROUTER, {provider::DEEPSEEK, "", "", model::DEEPSEEK_CHAT}},
                {role::COMMAND, {provider::DEEPSEEK, "", "", model::DEEPSEEK_CHAT}},
                {role::MUSIC, {provider::DEEPSEEK, "", "", model::DEEPSEEK_REASONER}},
                {role::CONTROLLER, {provider::DEEPSEEK, "", "", model::DEEPSEEK_CHAT}},
            },
        },
        {
            preset::CLOUD_OPENROUTER,
            "Cloud (OpenRouter)",
            {
                {role::ROUTER, {provider::OPENROUTER, "", "", model::LLAMA_70B}},
                {role::COMMAND, {provider::OPENROUTER, "", "", model::LLAMA_70B}},
                {role::MUSIC, {provider::OPENROUTER, "", "", model::LLAMA_70B}},
                {role::CONTROLLER, {provider::OPENROUTER, "", "", model::LLAMA_70B}},
            },
        },
        {
            preset::HYBRID_SPEED,
            "Hybrid - Optimize for Speed",
            {
                {role::ROUTER, {provider::LLAMA_LOCAL, "", "", ""}},
                {role::COMMAND, {provider::OPENAI_RESPONSES, "", "", model::GPT_5_NANO}},
                {role::MUSIC, {provider::OPENAI_RESPONSES, "", "", model::GPT_5_MINI}},
                {role::CONTROLLER, {provider::OPENAI_RESPONSES, "", "", model::GPT_5_MINI}},
            },
        },
        {
            preset::HYBRID_QUALITY,
            "Hybrid - Optimize for Quality",
            {
                {role::ROUTER, {provider::LLAMA_LOCAL, "", "", ""}},
                {role::COMMAND, {provider::OPENAI_RESPONSES, "", "", model::GPT_5_MINI}},
                {role::MUSIC, {provider::OPENAI_RESPONSES, "", "", model::GPT_5_4}},
                {role::CONTROLLER, {provider::OPENAI_RESPONSES, "", "", model::GPT_5_MINI}},
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
