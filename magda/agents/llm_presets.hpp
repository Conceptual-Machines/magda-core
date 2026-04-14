#pragma once

#include <map>
#include <string>
#include <vector>

#include "../daw/core/Config.hpp"

namespace magda {

// --- Provider IDs (wire format / credential keys) ---
namespace provider {
inline constexpr const char* OPENAI_CHAT = "openai_chat";
inline constexpr const char* OPENAI_RESPONSES = "openai_responses";
inline constexpr const char* ANTHROPIC = "anthropic";
inline constexpr const char* GEMINI = "gemini";
inline constexpr const char* DEEPSEEK = "deepseek";
inline constexpr const char* OPENROUTER = "openrouter";
inline constexpr const char* LLAMA_LOCAL = "llama_local";
}  // namespace provider

// --- Preset IDs ---
namespace preset {
inline constexpr const char* LOCAL_EMBEDDED = "local_embedded";
inline constexpr const char* CLOUD_OPENAI = "cloud_openai";
inline constexpr const char* CLOUD_ANTHROPIC = "cloud_anthropic";
inline constexpr const char* CLOUD_GEMINI = "cloud_gemini";
inline constexpr const char* CLOUD_DEEPSEEK = "cloud_deepseek";
inline constexpr const char* CLOUD_OPENROUTER = "cloud_openrouter";
inline constexpr const char* HYBRID_SPEED = "hybrid_speed";
inline constexpr const char* HYBRID_QUALITY = "hybrid_quality";
}  // namespace preset

// --- Model IDs ---
namespace model {
// OpenAI
inline constexpr const char* GPT_4_1 = "gpt-4.1";
inline constexpr const char* GPT_4_1_MINI = "gpt-4.1-mini";
inline constexpr const char* GPT_5 = "gpt-5";
inline constexpr const char* GPT_5_MINI = "gpt-5-mini";
inline constexpr const char* GPT_5_NANO = "gpt-5-nano";
inline constexpr const char* GPT_5_4 = "gpt-5.4";
// Anthropic
inline constexpr const char* CLAUDE_OPUS = "claude-opus-4-6";
inline constexpr const char* CLAUDE_SONNET = "claude-sonnet-4-6";
inline constexpr const char* CLAUDE_HAIKU = "claude-haiku-4-5-20251001";
// Gemini
inline constexpr const char* GEMINI_FLASH = "gemini-2.0-flash";
inline constexpr const char* GEMINI_PRO = "gemini-2.5-pro";
// DeepSeek
inline constexpr const char* DEEPSEEK_CHAT = "deepseek-chat";
inline constexpr const char* DEEPSEEK_REASONER = "deepseek-reasoner";
// OpenRouter
inline constexpr const char* LLAMA_70B = "meta-llama/llama-3.3-70b-instruct";
}  // namespace model

// --- Agent roles ---
namespace role {
inline constexpr const char* ROUTER = "router";
inline constexpr const char* COMMAND = "command";
inline constexpr const char* MUSIC = "music";
}  // namespace role

struct LLMPreset {
    std::string id;
    std::string displayName;
    std::map<std::string, Config::AgentLLMConfig> agents;
};

/** Built-in preset definitions. */
const std::vector<LLMPreset>& getBuiltInPresets();

/** Find a preset by id. Returns nullptr if not found. */
const LLMPreset* findPreset(const std::string& id);

}  // namespace magda
