#include "llama_server_manager.hpp"

namespace magda {

namespace {

class HealthPollerThread : public juce::Thread {
  public:
    HealthPollerThread(int port, juce::ChildProcess& proc,
                       std::function<void(bool, const juce::String&)> onResult)
        : juce::Thread("LlamaHealthPoller"),
          port_(port),
          process_(proc),
          onResult_(std::move(onResult)) {}

    void run() override {
        // Poll /health every 500ms, max 60 tries (30 seconds)
        for (int attempt = 0; attempt < 60 && !threadShouldExit(); ++attempt) {
            if (!process_.isRunning()) {
                onResult_(false, "Server process exited unexpectedly");
                return;
            }

            auto healthUrl = juce::URL("http://127.0.0.1:" + juce::String(port_) + "/health");
            int statusCode = 0;
            auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                               .withConnectionTimeoutMs(1000)
                               .withStatusCode(&statusCode);
            auto stream = healthUrl.createInputStream(options);

            if (stream != nullptr && statusCode == 200) {
                onResult_(true, "Server ready on port " + juce::String(port_));
                return;
            }

            wait(500);
        }

        if (!threadShouldExit())
            onResult_(false, "Server failed to become ready within 30s");
    }

  private:
    int port_;
    juce::ChildProcess& process_;
    std::function<void(bool, const juce::String&)> onResult_;
};

}  // namespace

LlamaServerManager& LlamaServerManager::getInstance() {
    static LlamaServerManager instance;
    return instance;
}

LlamaServerManager::~LlamaServerManager() {
    stop();
}

std::string LlamaServerManager::getBaseUrl() const {
    return "http://127.0.0.1:" + std::to_string(config_.port) + "/v1";
}

juce::String LlamaServerManager::getStatusMessage() const {
    juce::ScopedLock sl(messageLock_);
    return statusMessage_;
}

bool LlamaServerManager::isRunning() const {
    auto s = status_.load();
    return s == Status::Running || s == Status::Starting;
}

void LlamaServerManager::start(const Config& config) {
    stop();

    config_ = config;

    // Validate model path
    juce::File modelFile(config_.modelPath);
    if (!modelFile.existsAsFile()) {
        setStatus(Status::Error, "Model file not found: " + juce::String(config_.modelPath));
        return;
    }

    // Find binary
    auto binary = findBinary(config_.binaryPath);
    if (binary.isEmpty()) {
        setStatus(Status::Error, "llama-server not found. Install via: brew install llama.cpp");
        return;
    }

    // Build command line
    juce::StringArray args;
    args.add(binary);
    args.add("-m");
    args.add(juce::String(config_.modelPath));
    args.add("--port");
    args.add(juce::String(config_.port));
    args.add("-c");
    args.add(juce::String(config_.contextSize));

    if (config_.gpuLayers >= 0) {
        args.add("-ngl");
        args.add(juce::String(config_.gpuLayers));
    }

    DBG("LlamaServerManager: starting " + args.joinIntoString(" "));

    process_ = std::make_unique<juce::ChildProcess>();
    if (!process_->start(args)) {
        setStatus(Status::Error, "Failed to start llama-server process");
        process_.reset();
        return;
    }

    setStatus(Status::Starting, "Starting server...");

    // Launch health poller thread
    auto poller = std::make_unique<HealthPollerThread>(
        config_.port, *process_, [this](bool ok, const juce::String& msg) {
            setStatus(ok ? Status::Running : Status::Error, msg);
        });
    poller->startThread();
    healthThread_ = std::move(poller);
}

void LlamaServerManager::stop() {
    if (healthThread_) {
        healthThread_->signalThreadShouldExit();
        healthThread_->waitForThreadToExit(2000);
        healthThread_.reset();
    }

    if (process_ && process_->isRunning()) {
        DBG("LlamaServerManager: stopping server...");
        process_->kill();
    }
    process_.reset();

    if (status_.load() != Status::Stopped)
        setStatus(Status::Stopped, "Stopped");
}

void LlamaServerManager::setStatus(Status s, const juce::String& msg) {
    {
        juce::ScopedLock sl(messageLock_);
        statusMessage_ = msg.isNotEmpty() ? msg : statusMessage_;
    }
    status_.store(s);

    DBG("LlamaServerManager: " + msg);

    if (onStatusChanged) {
        auto cb = onStatusChanged;
        auto status = s;
        // Fire callback — caller (UI) should handle thread safety
        cb(status);
    }
}

juce::String LlamaServerManager::findBinary(const std::string& configured) const {
    if (!configured.empty()) {
        juce::File f(configured);
        if (f.existsAsFile())
            return f.getFullPathName();
    }

    const char* candidates[] = {
        "/opt/homebrew/bin/llama-server",
        "/usr/local/bin/llama-server",
        "/usr/bin/llama-server",
    };

    for (auto* path : candidates) {
        juce::File f(path);
        if (f.existsAsFile())
            return f.getFullPathName();
    }

    juce::ChildProcess which;
    if (which.start("which llama-server")) {
        which.waitForProcessToFinish(2000);
        auto result = which.readAllProcessOutput().trim();
        if (result.isNotEmpty() && juce::File(result).existsAsFile())
            return result;
    }

    return {};
}

}  // namespace magda
