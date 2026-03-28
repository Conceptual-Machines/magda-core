#include "llama_model_manager.hpp"

#include <llama.h>

#include <chrono>
#include <vector>

namespace magda {

LlamaModelManager& LlamaModelManager::getInstance() {
    static LlamaModelManager instance;
    return instance;
}

LlamaModelManager::~LlamaModelManager() {
    unloadModel();
}

bool LlamaModelManager::loadModel(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Unload existing model if any
    if (model_ != nullptr) {
        if (ctx_ != nullptr) {
            llama_free(ctx_);
            ctx_ = nullptr;
        }
        llama_model_free(model_);
        model_ = nullptr;
        loadedPath_.clear();
    }

    // Load model
    auto modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = config.gpuLayers;

    model_ = llama_model_load_from_file(config.modelPath.c_str(), modelParams);
    if (model_ == nullptr)
        return false;

    // Create context
    auto ctxParams = llama_context_default_params();
    ctxParams.n_ctx = static_cast<uint32_t>(config.contextSize);
    ctxParams.n_batch = static_cast<uint32_t>(config.contextSize);

    ctx_ = llama_init_from_model(model_, ctxParams);
    if (ctx_ == nullptr) {
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    loadedPath_ = config.modelPath;
    config_ = config;
    return true;
}

void LlamaModelManager::unloadModel() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ctx_ != nullptr) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_ != nullptr) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    loadedPath_.clear();
}

bool LlamaModelManager::isLoaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_ != nullptr && ctx_ != nullptr;
}

std::string LlamaModelManager::getLoadedModelPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loadedPath_;
}

std::string LlamaModelManager::applyTemplate(const std::string& systemPrompt,
                                             const std::string& userMessage) {
    // Get the chat template from model metadata
    const char* tmpl = llama_model_chat_template(model_, nullptr);

    llama_chat_message messages[2] = {
        {"system", systemPrompt.c_str()},
        {"user", userMessage.c_str()},
    };

    // First call to get required buffer size
    int32_t len = llama_chat_apply_template(tmpl, messages, 2, true, nullptr, 0);
    if (len < 0)
        return {};

    std::vector<char> buf(static_cast<size_t>(len) + 1);
    llama_chat_apply_template(tmpl, messages, 2, true, buf.data(),
                              static_cast<int32_t>(buf.size()));
    return std::string(buf.data(), static_cast<size_t>(len));
}

LlamaModelManager::InferenceResult LlamaModelManager::infer(const InferenceRequest& req,
                                                            TokenCallback onToken) {
    std::lock_guard<std::mutex> lock(mutex_);
    InferenceResult result;

    if (model_ == nullptr || ctx_ == nullptr) {
        result.error = "No model loaded";
        return result;
    }

    auto startTime = std::chrono::steady_clock::now();

    // Clear KV cache for fresh inference
    llama_memory_clear(llama_get_memory(ctx_), true);

    // Apply chat template
    auto prompt = applyTemplate(req.systemPrompt, req.userMessage);
    if (prompt.empty()) {
        result.error = "Failed to apply chat template";
        return result;
    }

    // Tokenize
    const auto* vocab = llama_model_get_vocab(model_);
    int32_t maxTokens = static_cast<int32_t>(prompt.size()) + 32;
    std::vector<llama_token> tokens(static_cast<size_t>(maxTokens));

    int32_t nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                     tokens.data(), maxTokens, true, true);
    if (nTokens < 0) {
        // Buffer too small, resize and retry
        tokens.resize(static_cast<size_t>(-nTokens));
        nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                 tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
        if (nTokens < 0) {
            result.error = "Tokenization failed";
            return result;
        }
    }
    tokens.resize(static_cast<size_t>(nTokens));

    // Create sampler chain
    auto samplerParams = llama_sampler_chain_default_params();
    auto* sampler = llama_sampler_chain_init(samplerParams);

    if (req.temperature <= 0.0f) {
        llama_sampler_chain_add(sampler, llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(req.temperature));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    }

    // Process prompt
    auto batch = llama_batch_get_one(tokens.data(), nTokens);
    if (llama_decode(ctx_, batch) != 0) {
        llama_sampler_free(sampler);
        result.error = "Failed to process prompt";
        return result;
    }

    // Generate tokens
    llama_token eosToken = llama_vocab_eos(vocab);
    std::string generated;
    char tokenBuf[256];

    for (int i = 0; i < req.maxTokens; ++i) {
        llama_token newToken = llama_sampler_sample(sampler, ctx_, -1);

        if (newToken == eosToken)
            break;

        int32_t pieceLen =
            llama_token_to_piece(vocab, newToken, tokenBuf, sizeof(tokenBuf), 0, true);
        if (pieceLen > 0) {
            std::string piece(tokenBuf, static_cast<size_t>(pieceLen));
            generated += piece;

            if (onToken && !onToken(piece)) {
                result.error = "Cancelled";
                break;
            }
        }

        // Decode the new token
        auto nextBatch = llama_batch_get_one(&newToken, 1);
        if (llama_decode(ctx_, nextBatch) != 0) {
            result.error = "Decode failed during generation";
            break;
        }
    }

    llama_sampler_free(sampler);

    auto endTime = std::chrono::steady_clock::now();
    result.text = generated;
    result.success = result.error.empty();
    result.wallSeconds = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

}  // namespace magda
