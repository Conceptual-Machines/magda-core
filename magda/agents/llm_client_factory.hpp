#pragma once

#include <juce_llm/juce_llm.h>

#include "../daw/core/LLMClientProvider.hpp"
#include "llama_local_client.hpp"
#include "llama_model_manager.hpp"

namespace magda {

inline void registerLocalLLMClientProvider() {
    setLLMClientProvider([](const Config::AgentLLMConfig& config,
                            const std::string& agentName) -> std::unique_ptr<llm::LLMClient> {
        if (config.provider == provider::LLAMA_LOCAL && LlamaModelManager::getInstance().isLoaded())
            return std::make_unique<LlamaLocalClient>();

        return llm::LLMClientFactory::create(toLLMProviderConfig(config, agentName));
    });
    setLLMProviderShutdownHandler([] { LlamaModelManager::getInstance().unloadModel(); });
}

}  // namespace magda
