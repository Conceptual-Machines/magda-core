#pragma once

#include <functional>
#include <mutex>
#include <string>

struct llama_model;
struct llama_context;

namespace magda {

class LlamaModelManager {
  public:
    static LlamaModelManager& getInstance();

    struct Config {
        std::string modelPath;
        int contextSize = 4096;
        int gpuLayers = -1;  // -1 = all layers on GPU (Metal)
    };

    bool loadModel(const Config& config);
    void unloadModel();
    bool isLoaded() const;
    std::string getLoadedModelPath() const;

    struct InferenceRequest {
        std::string systemPrompt;
        std::string userMessage;
        float temperature = 0.1f;
        int maxTokens = 512;
    };

    struct InferenceResult {
        std::string text;
        bool success = false;
        std::string error;
        double wallSeconds = 0.0;
    };

    using TokenCallback = std::function<bool(const std::string& token)>;

    InferenceResult infer(const InferenceRequest& req, TokenCallback onToken = nullptr);

  private:
    LlamaModelManager() = default;
    ~LlamaModelManager();

    LlamaModelManager(const LlamaModelManager&) = delete;
    LlamaModelManager& operator=(const LlamaModelManager&) = delete;

    std::string applyTemplate(const std::string& systemPrompt, const std::string& userMessage);

    mutable std::mutex mutex_;
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    std::string loadedPath_;
    Config config_;
};

}  // namespace magda
