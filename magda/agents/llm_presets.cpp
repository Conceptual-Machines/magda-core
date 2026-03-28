#include "llm_presets.hpp"

namespace magda {

using AC = Config::AgentLLMConfig;

const std::vector<LLMPreset>& getBuiltInPresets() {
    static const std::vector<LLMPreset> presets = {
        {
            "local",
            "Local (llama-server)",
            {
                {"router", {"openai_chat", "http://127.0.0.1:8080/v1", "", "local"}},
                {"command", {"openai_chat", "http://127.0.0.1:8080/v1", "", "local"}},
                {"music", {"openai_chat", "http://127.0.0.1:8080/v1", "", "local"}},
            },
        },
        {
            "cloud_openai",
            "Cloud (OpenAI)",
            {
                {"router", {"openai_chat", "", "", "gpt-4.1-mini"}},
                {"command", {"openai_chat", "", "", "gpt-4.1"}},
                {"music", {"openai_chat", "", "", "gpt-5"}},
            },
        },
        {
            "cloud_anthropic",
            "Cloud (Anthropic)",
            {
                {"router", {"anthropic", "", "", "claude-haiku-4-5-20251001"}},
                {"command", {"anthropic", "", "", "claude-haiku-4-5-20251001"}},
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
                {"router", {"openai_chat", "https://api.deepseek.com", "", "deepseek-chat"}},
                {"command", {"openai_chat", "https://api.deepseek.com", "", "deepseek-chat"}},
                {"music", {"openai_chat", "https://api.deepseek.com", "", "deepseek-reasoner"}},
            },
        },
        {
            "cloud_openrouter",
            "Cloud (OpenRouter)",
            {
                {"router",
                 {"openai_chat", "https://openrouter.ai/api/v1", "",
                  "meta-llama/llama-3.3-70b-instruct"}},
                {"command",
                 {"openai_chat", "https://openrouter.ai/api/v1", "",
                  "meta-llama/llama-3.3-70b-instruct"}},
                {"music",
                 {"openai_chat", "https://openrouter.ai/api/v1", "",
                  "meta-llama/llama-3.3-70b-instruct"}},
            },
        },
        {
            "hybrid",
            "Hybrid (local router+command, cloud music)",
            {
                {"router", {"openai_chat", "http://127.0.0.1:8080/v1", "", "local"}},
                {"command", {"openai_chat", "http://127.0.0.1:8080/v1", "", "local"}},
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
