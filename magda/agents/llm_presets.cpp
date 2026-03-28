#include "llm_presets.hpp"

namespace magda {

using AC = Config::AgentLLMConfig;

const std::vector<LLMPreset>& getBuiltInPresets() {
    static const std::vector<LLMPreset> presets = {
        {
            "local_embedded",
            "Local (Embedded)",
            {
                {"router", {"llama_local", "", "", ""}},
                {"command", {"llama_local", "", "", ""}},
                {"music", {"llama_local", "", "", ""}},
            },
        },
        {
            "cloud_openai",
            "Cloud (OpenAI)",
            {
                {"router", {"openai_chat", "", "", "gpt-4.1"}},
                {"command", {"openai_chat", "", "", "gpt-5"}},
                {"music", {"openai_chat", "", "", "gpt-5"}},
            },
        },
        {
            "cloud_anthropic",
            "Cloud (Anthropic)",
            {
                {"router", {"anthropic", "", "", "claude-haiku-4-5-20251001"}},
                {"command", {"anthropic", "", "", "claude-sonnet-4-6"}},
                {"music", {"anthropic", "", "", "claude-opus-4-6"}},
            },
        },
        {
            "cloud_gemini",
            "Cloud (Gemini)",
            {
                {"router", {"gemini", "", "", "gemini-2.0-flash"}},
                {"command", {"gemini", "", "", "gemini-2.0-flash"}},
                {"music", {"gemini", "", "", "gemini-2.5-pro"}},
            },
        },
        {
            "cloud_deepseek",
            "Cloud (DeepSeek)",
            {
                {"router", {"deepseek", "", "", "deepseek-chat"}},
                {"command", {"deepseek", "", "", "deepseek-chat"}},
                {"music", {"deepseek", "", "", "deepseek-reasoner"}},
            },
        },
        {
            "cloud_openrouter",
            "Cloud (OpenRouter)",
            {
                {"router", {"openrouter", "", "", "meta-llama/llama-3.3-70b-instruct"}},
                {"command", {"openrouter", "", "", "meta-llama/llama-3.3-70b-instruct"}},
                {"music", {"openrouter", "", "", "meta-llama/llama-3.3-70b-instruct"}},
            },
        },
        {
            "hybrid_cost",
            "Hybrid - Optimize for Cost",
            {
                {"router", {"llama_local", "", "", ""}},
                {"command", {"llama_local", "", "", ""}},
                {"music", {"openai_chat", "", "", "gpt-5"}},
            },
        },
        {
            "hybrid_speed",
            "Hybrid - Optimize for Speed",
            {
                {"router", {"llama_local", "", "", ""}},
                {"command", {"openai_chat", "", "", "gpt-5"}},
                {"music", {"openai_chat", "", "", "gpt-5"}},
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
