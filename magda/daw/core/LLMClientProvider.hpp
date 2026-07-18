#pragma once

#include <juce_llm/juce_llm.h>

#include <functional>
#include <memory>
#include <string>

#include "Config.hpp"

namespace magda {

namespace provider {
inline constexpr const char* OPENAI_CHAT = "openai_chat";
inline constexpr const char* OPENAI_RESPONSES = "openai_responses";
inline constexpr const char* ANTHROPIC = "anthropic";
inline constexpr const char* GEMINI = "gemini";
inline constexpr const char* DEEPSEEK = "deepseek";
inline constexpr const char* OPENROUTER = "openrouter";
inline constexpr const char* LLAMA_LOCAL = "llama_local";
inline constexpr const char* LOCAL_SERVER = "local_server";
}  // namespace provider

inline constexpr const char* DEFAULT_LOCAL_SERVER_URL = "http://localhost:11434/v1";

namespace preset {
inline constexpr const char* LOCAL_EMBEDDED = "local_embedded";
inline constexpr const char* LOCAL_SERVER = "local_server";
inline constexpr const char* CLOUD_OPENAI = "cloud_openai";
inline constexpr const char* CLOUD_ANTHROPIC = "cloud_anthropic";
inline constexpr const char* CLOUD_GEMINI = "cloud_gemini";
inline constexpr const char* CLOUD_DEEPSEEK = "cloud_deepseek";
inline constexpr const char* CLOUD_OPENROUTER = "cloud_openrouter";
inline constexpr const char* HYBRID_SPEED = "hybrid_speed";
inline constexpr const char* HYBRID_QUALITY = "hybrid_quality";
// Sentinel: per-agent provider/model mapping set from the Advanced config
// panel. Not a built-in preset — agent configs are persisted directly.
inline constexpr const char* ADVANCED = "advanced";
}  // namespace preset

namespace model {
// OpenAI (Chat Completions + Responses API). gpt-5* and the o-series route
// through the Responses API; gpt-4.1* use Chat Completions.
inline constexpr const char* GPT_4_1 = "gpt-4.1";
inline constexpr const char* GPT_4_1_MINI = "gpt-4.1-mini";
inline constexpr const char* GPT_5 = "gpt-5";
inline constexpr const char* GPT_5_MINI = "gpt-5-mini";
inline constexpr const char* GPT_5_NANO = "gpt-5-nano";
inline constexpr const char* GPT_5_1 = "gpt-5.1";
inline constexpr const char* GPT_5_2 = "gpt-5.2";
inline constexpr const char* GPT_5_4 = "gpt-5.4";
inline constexpr const char* GPT_5_4_MINI = "gpt-5.4-mini";
inline constexpr const char* GPT_5_4_NANO = "gpt-5.4-nano";
inline constexpr const char* GPT_5_4_PRO = "gpt-5.4-pro";
inline constexpr const char* GPT_5_5 = "gpt-5.5";
inline constexpr const char* GPT_5_5_PRO = "gpt-5.5-pro";
inline constexpr const char* GPT_5_6_SOL = "gpt-5.6-sol";
inline constexpr const char* GPT_5_6_TERRA = "gpt-5.6-terra";
inline constexpr const char* GPT_5_6_LUNA = "gpt-5.6-luna";
inline constexpr const char* O3 = "o3";
inline constexpr const char* O3_PRO = "o3-pro";

// Anthropic (Claude). Dateless ids are pinned snapshots (4.6 generation onward).
inline constexpr const char* CLAUDE_FABLE_5 = "claude-fable-5";
inline constexpr const char* CLAUDE_OPUS_4_7 = "claude-opus-4-7";
inline constexpr const char* CLAUDE_OPUS_4_8 = "claude-opus-4-8";
inline constexpr const char* CLAUDE_OPUS = CLAUDE_OPUS_4_8;
inline constexpr const char* CLAUDE_SONNET_5 = "claude-sonnet-5";
inline constexpr const char* CLAUDE_SONNET_4_6 = "claude-sonnet-4-6";
inline constexpr const char* CLAUDE_SONNET = CLAUDE_SONNET_4_6;  // preset default
inline constexpr const char* CLAUDE_HAIKU = "claude-haiku-4-5-20251001";

// Google Gemini. gemini-2.0-flash was shut down 2026-06-01, so GEMINI_FLASH
// now aliases the 2.5 flash baseline.
inline constexpr const char* GEMINI_3_5_FLASH = "gemini-3.5-flash";
inline constexpr const char* GEMINI_3_1_PRO = "gemini-3.1-pro";
inline constexpr const char* GEMINI_3_1_FLASH = "gemini-3.1-flash";
inline constexpr const char* GEMINI_3_1_FLASH_LITE = "gemini-3.1-flash-lite";
inline constexpr const char* GEMINI_2_5_PRO = "gemini-2.5-pro";
inline constexpr const char* GEMINI_2_5_FLASH = "gemini-2.5-flash";
inline constexpr const char* GEMINI_FLASH = GEMINI_2_5_FLASH;
inline constexpr const char* GEMINI_PRO = GEMINI_2_5_PRO;

// DeepSeek. The legacy deepseek-chat / deepseek-reasoner ids are deprecated
// (2026-07-24); DEEPSEEK_CHAT / DEEPSEEK_REASONER now alias the V4 ids.
inline constexpr const char* DEEPSEEK_V4_FLASH = "deepseek-v4-flash";
inline constexpr const char* DEEPSEEK_V4_PRO = "deepseek-v4-pro";
inline constexpr const char* DEEPSEEK_CHAT = DEEPSEEK_V4_FLASH;
inline constexpr const char* DEEPSEEK_REASONER = DEEPSEEK_V4_PRO;

// OpenRouter (routes to many hosted models; the model field is free-text).
inline constexpr const char* LLAMA_70B = "meta-llama/llama-3.3-70b-instruct";
}  // namespace model

namespace role {
inline constexpr const char* ROUTER = "router";
inline constexpr const char* COMMAND = "command";
inline constexpr const char* MUSIC = "music";
inline constexpr const char* CONTROLLER = "controller";
}  // namespace role

std::string normalizeOpenAIBaseUrl(std::string url);
llm::Provider providerFromString(const std::string& s);
juce::String defaultBaseUrl(const std::string& providerStr);
llm::ProviderConfig toLLMProviderConfig(const Config::AgentLLMConfig& config,
                                        const std::string& agentName = {});
bool supportsOpenAICFG(const Config::AgentLLMConfig& config);

using LLMClientProvider = std::function<std::unique_ptr<llm::LLMClient>(
    const Config::AgentLLMConfig&, const std::string&)>;
using LLMProviderShutdownHandler = std::function<void()>;

void setLLMClientProvider(LLMClientProvider provider);
void setLLMProviderShutdownHandler(LLMProviderShutdownHandler handler);
std::unique_ptr<llm::LLMClient> createLLMClient(const Config::AgentLLMConfig& config,
                                                const std::string& agentName = {});
void shutdownLLMClientProvider();

}  // namespace magda
