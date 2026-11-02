// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file llama_model_manager.cpp
 * @brief The llama-free half of LlamaModelManager, compiled in every build (#3235).
 */

// See llama_model_manager_llama.cpp: Apple's libc++ lacks std::atomic<std::shared_ptr>.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING

#include "llama_model_manager.hpp"

#include <juce_core/juce_core.h>

namespace magda {

LlamaModelManager& LlamaModelManager::getInstance() {
    static LlamaModelManager instance;
    return instance;
}

LlamaModelManager::~LlamaModelManager() {
    unloadModel();
}

std::string LlamaModelManager::cpuCompatibilityError(bool isIntel, bool hasSSE42, bool hasAVX) {
    if (!isIntel || (hasSSE42 && hasAVX))
        return {};

    return "Embedded local AI requires a CPU with SSE 4.2 and AVX support. "
           "Use a cloud provider or local server instead.";
}

std::string LlamaModelManager::currentCpuCompatibilityError() {
#if JUCE_INTEL && !JUCE_MAC
    return cpuCompatibilityError(true, juce::SystemStats::hasSSE42(), juce::SystemStats::hasAVX());
#else
    return {};
#endif
}

bool LlamaModelManager::isLoaded() const noexcept {
    return std::atomic_load_explicit(&loadedPathSnapshot_, std::memory_order_acquire) != nullptr;
}

bool LlamaModelManager::isLoading() const noexcept {
    return loading_.load(std::memory_order_acquire);
}

std::string LlamaModelManager::getLoadedModelPath() const {
    auto path = std::atomic_load_explicit(&loadedPathSnapshot_, std::memory_order_acquire);
    return path != nullptr ? *path : std::string{};
}

}  // namespace magda
