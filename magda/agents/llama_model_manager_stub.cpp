// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file llama_model_manager_stub.cpp
 * @brief LlamaModelManager backend for MAGDA_LOCAL_LLM=OFF builds, where nothing loads (#3235).
 */

#include "llama_model_manager.hpp"

namespace magda {
namespace {
constexpr const char* kNoLocalInference =
    "This build of MAGDA was built without local inference, so Local (Embedded) models "
    "cannot be loaded. Use a cloud provider or local server instead.";
}  // namespace

bool LlamaModelManager::loadModel(const Config&, std::string* errorMessage) {
    if (errorMessage != nullptr)
        *errorMessage = kNoLocalInference;
    return false;
}

void LlamaModelManager::unloadModel() {}

LlamaModelManager::InferenceResult LlamaModelManager::infer(const InferenceRequest&,
                                                            const TokenCallback&) {
    InferenceResult result;
    result.error = kNoLocalInference;
    return result;
}

}  // namespace magda
