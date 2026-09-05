#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct llama_model;
struct llama_context;

namespace magda {

#ifdef MAGDA_ENABLE_TEST_HOOKS
struct LlamaModelManagerTestAccess;
#endif

class LlamaModelManager {
  public:
    LlamaModelManager(const LlamaModelManager&) = delete;
    LlamaModelManager& operator=(const LlamaModelManager&) = delete;

    static LlamaModelManager& getInstance();

    struct Config {
        std::string modelPath;
        int contextSize = 4096;
        int gpuLayers = -1;  // -1 = all layers on GPU (Metal)
    };

    bool loadModel(const Config& config, std::string* errorMessage = nullptr);
    void unloadModel();
    bool isLoaded() const noexcept;
    bool isLoading() const noexcept;
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
#ifdef MAGDA_ENABLE_TEST_HOOKS
    friend struct LlamaModelManagerTestAccess;
#endif

    LlamaModelManager() = default;
    ~LlamaModelManager();

    static std::string cpuCompatibilityError(bool isIntel, bool hasSSE42, bool hasAVX);
    static std::string currentCpuCompatibilityError();
    std::string applyTemplate(const std::string& systemPrompt, const std::string& userMessage);

    mutable std::mutex mutex_;
    std::atomic<bool> loading_{false};
    // Lock-free loaded-status snapshot for message-thread reads: non-null means a
    // model is loaded, and the pointee is its path. Single source of truth so the
    // flag and the path can never disagree. Accessed via the std::atomic_load/
    // atomic_store free functions (deprecated in C++20) because Apple's libc++
    // does not ship std::atomic<std::shared_ptr>.
    std::shared_ptr<const std::string> loadedPathSnapshot_;
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    Config config_;
};

}  // namespace magda
