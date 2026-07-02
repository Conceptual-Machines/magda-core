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
}  // namespace preset

namespace model {
inline constexpr const char* GPT_4_1 = "gpt-4.1";
inline constexpr const char* GPT_4_1_MINI = "gpt-4.1-mini";
inline constexpr const char* GPT_5 = "gpt-5";
inline constexpr const char* GPT_5_MINI = "gpt-5-mini";
inline constexpr const char* GPT_5_NANO = "gpt-5-nano";
inline constexpr const char* GPT_5_4 = "gpt-5.4";
inline constexpr const char* GPT_5_5 = "gpt-5.5";
inline constexpr const char* CLAUDE_OPUS_4_7 = "claude-opus-4-7";
inline constexpr const char* CLAUDE_OPUS_4_8 = "claude-opus-4-8";
inline constexpr const char* CLAUDE_OPUS = CLAUDE_OPUS_4_8;
inline constexpr const char* CLAUDE_SONNET = "claude-sonnet-4-6";
inline constexpr const char* CLAUDE_HAIKU = "claude-haiku-4-5-20251001";
inline constexpr const char* GEMINI_FLASH = "gemini-2.0-flash";
inline constexpr const char* GEMINI_PRO = "gemini-2.5-pro";
inline constexpr const char* DEEPSEEK_CHAT = "deepseek-chat";
inline constexpr const char* DEEPSEEK_REASONER = "deepseek-reasoner";
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
