#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

namespace magda {

namespace ScannerIPC {
constexpr const char* MSG_SCAN_ONE = "SCNO";
constexpr const char* MSG_PROGRESS = "PROG";
constexpr const char* MSG_PLUGIN_FOUND = "PLUG";
constexpr const char* MSG_SCAN_COMPLETE = "DONE";
constexpr const char* MSG_ERROR = "ERR";
constexpr const char* MSG_QUIT = "QUIT";
}  // namespace ScannerIPC

class ScanWorker : private juce::ChildProcessCoordinator {
  public:
    struct Result {
        juce::String pluginPath;
        bool success = false;
        juce::Array<juce::PluginDescription> foundPlugins;
        juce::String errorMessage;
    };

    using ResultCallback = std::function<void(int workerIndex, const Result& result)>;

    ScanWorker(int index, const juce::File& scannerExe, ResultCallback callback);
    ~ScanWorker() override;

    void scanPlugin(const juce::String& formatName, const juce::String& pluginPath);
    bool isBusy() const {
        return busy_;
    }
    void abort();

  private:
    void handleMessageFromWorker(const juce::MemoryBlock& message) override;
    void handleConnectionLost() override;

    bool launchSubprocess();
    void sendScanOneCommand(const juce::String& formatName, const juce::String& pluginPath);
    void sendQuit();
    void reportResultAsync(bool success, const juce::String& error = {});

    int workerIndex_;
    juce::File scannerExe_;
    ResultCallback resultCallback_;

    juce::String currentFormat_;
    juce::String currentPlugin_;
    std::atomic<bool> busy_{false};
    bool receivedDone_ = false;
    Result currentResult_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScanWorker)
};

}  // namespace magda
