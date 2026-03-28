#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace magda {

/**
 * Manages a local llama-server child process.
 * Singleton — one server at a time, shared across the app.
 */
class LlamaServerManager {
  public:
    static LlamaServerManager& getInstance();

    enum class Status { Stopped, Starting, Running, Error };

    struct Config {
        std::string binaryPath;  // path to llama-server (empty = search PATH)
        std::string modelPath;   // path to .gguf model file
        int port = 8080;         // listen port
        int gpuLayers = -1;      // -1 = auto (all), 0 = CPU only
        int contextSize = 4096;  // context window
    };

    /** Start the server with given config. Non-blocking — polls /health. */
    void start(const Config& config);

    /** Stop the running server. */
    void stop();

    /** Current status. */
    Status getStatus() const {
        return status_.load();
    }

    /** Human-readable status / error message. */
    juce::String getStatusMessage() const;

    /** True if process is alive. */
    bool isRunning() const;

    /** The base URL for the running server (e.g. http://127.0.0.1:8080/v1). */
    std::string getBaseUrl() const;

    /** Optional callback when status changes (called on message thread if available). */
    std::function<void(Status)> onStatusChanged;

    ~LlamaServerManager();

  private:
    LlamaServerManager() = default;

    void setStatus(Status s, const juce::String& msg = {});
    juce::String findBinary(const std::string& configured) const;

    std::unique_ptr<juce::ChildProcess> process_;
    Config config_;
    std::atomic<Status> status_{Status::Stopped};
    juce::String statusMessage_{"Stopped"};
    std::unique_ptr<juce::Thread> healthThread_;
    juce::CriticalSection messageLock_;
};

}  // namespace magda
