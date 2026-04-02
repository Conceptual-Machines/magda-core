#pragma once

#include <map>
#include <string>
#include <vector>

#include "../daw/core/Config.hpp"

namespace magda {

// --- Provider IDs (wire format / credential keys) ---
namespace provider {
inline constexpr const char* OPENAI = "openai_chat";
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
inline constexpr const char* HYBRID_COST = "hybrid_cost";
inline constexpr const char* HYBRID_SPEED = "hybrid_speed";
}  // namespace preset

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
