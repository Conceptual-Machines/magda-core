#pragma once

#include <juce_llm/juce_llm.h>

#include "llama_local_client.hpp"
#include "llama_model_manager.hpp"
#include "llm_config_utils.hpp"

namespace magda {

/** Create an LLM client based on agent config.
    Uses embedded model when config says llama_local and model is loaded. */
inline std::unique_ptr<llm::LLMClient> createLLMClient(const Config::AgentLLMConfig& config,
                                                       const std::string& agentName = {}) {
    if (config.provider == provider::LLAMA_LOCAL && LlamaModelManager::getInstance().isLoaded())
        return std::make_unique<LlamaLocalClient>();

    return llm::LLMClientFactory::create(toLLMProviderConfig(config, agentName));
}

}  // namespace magda
