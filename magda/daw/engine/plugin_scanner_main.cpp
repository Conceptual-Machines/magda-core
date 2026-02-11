/**
 * @file plugin_scanner_main.cpp
 * @brief Out-of-process plugin scanner executable
 *
 * This executable is launched by the main MAGDA application to scan
 * plugins in a separate process. Each instance scans a single plugin
 * file, then exits. If a plugin crashes during scanning, only this
 * process dies and the main app continues with the next plugin.
 */

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <fstream>
#include <iostream>

// Global log file for debugging - scanner stdout isn't visible when run as child process
static std::ofstream g_logFile;

static void initLog() {
    // Use a unique temp file per instance (random suffix)
    auto suffix = juce::String(juce::Random::getSystemRandom().nextInt64());
    auto path = "/tmp/magda_scanner_" + suffix.toStdString() + ".log";
    g_logFile.open(path, std::ios::out | std::ios::trunc);
}

static void log(const std::string& msg) {
    if (g_logFile.is_open()) {
        g_logFile << msg << std::endl;
        g_logFile.flush();
    }
    std::cout << msg << std::endl;
    std::cout.flush();
}

namespace ScannerIPC {
constexpr const char* MSG_SCAN_ONE = "SCNO";
constexpr const char* MSG_PROGRESS = "PROG";
constexpr const char* MSG_PLUGIN_FOUND = "PLUG";
constexpr const char* MSG_SCAN_COMPLETE = "DONE";
constexpr const char* MSG_ERROR = "ERR";
constexpr const char* MSG_QUIT = "QUIT";
}  // namespace ScannerIPC

class PluginScannerWorker : public juce::ChildProcessWorker {
  public:
    PluginScannerWorker() {
        log("[Scanner] PluginScannerWorker constructor starting...");

#if JUCE_PLUGINHOST_VST3
        log("[Scanner] About to register VST3 format...");
        formatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
        log("[Scanner] Registered VST3 format");
#endif
#if JUCE_PLUGINHOST_AU && JUCE_MAC
        log("[Scanner] About to register AudioUnit format...");
        formatManager_.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
        log("[Scanner] Registered AudioUnit format");
#endif
        log("[Scanner] PluginScannerWorker constructor complete");
    }

    void handleMessageFromCoordinator(const juce::MemoryBlock& message) override {
        try {
            log("[Scanner] Received message from coordinator");
            juce::MemoryInputStream stream(message, false);
            juce::String msgType = stream.readString();
            log("[Scanner] Message type: " + msgType.toStdString());

            if (msgType == ScannerIPC::MSG_QUIT) {
                log("[Scanner] Received QUIT message, exiting gracefully");
                juce::JUCEApplicationBase::quit();
                return;
            } else if (msgType == ScannerIPC::MSG_SCAN_ONE) {
                juce::String formatName = stream.readString();
                juce::String pluginPath = stream.readString();

                log("[Scanner] Scanning single plugin: " + pluginPath.toStdString() +
                    " (format: " + formatName.toStdString() + ")");

                // Dispatch to the message thread — many plugins (especially VST3)
                // expect to be loaded on the message thread and will crash if
                // their factory code is called from the IPC thread.
                juce::MessageManager::callAsync([this, formatName, pluginPath]() {
                    try {
                        scanOnePlugin(formatName, pluginPath);
                    } catch (const std::exception& e) {
                        log(std::string("[Scanner] Message thread exception: ") + e.what());
                        sendError(pluginPath,
                                  juce::String("Message thread exception: ") + e.what());
                        sendComplete();
                    } catch (...) {
                        log("[Scanner] Message thread unknown exception");
                        sendError(pluginPath, "Unknown exception on message thread");
                        sendComplete();
                    }
                });
            }
        } catch (const std::exception& e) {
            log(std::string("[Scanner] EXCEPTION: ") + e.what());
        } catch (...) {
            log("[Scanner] UNKNOWN EXCEPTION");
        }
    }

    void handleConnectionMade() override {
        log("[Scanner] Connected to main application");
    }

    void handleConnectionLost() override {
        log("[Scanner] Connection lost, exiting");
        juce::JUCEApplicationBase::quit();
    }

  private:
    juce::AudioPluginFormatManager formatManager_;

    void scanOnePlugin(const juce::String& formatName, const juce::String& pluginPath) {
        try {
            // Find the format
            juce::AudioPluginFormat* format = nullptr;
            for (int i = 0; i < formatManager_.getNumFormats(); ++i) {
                auto* fmt = formatManager_.getFormat(i);
                if (fmt && fmt->getName() == formatName) {
                    format = fmt;
                    break;
                }
            }

            if (!format) {
                log("[Scanner] Format not found: " + formatName.toStdString());
                sendError(pluginPath, "Format not found: " + formatName);
                sendComplete();
                return;
            }

            // Scan single file
            juce::OwnedArray<juce::PluginDescription> results;
            format->findAllTypesForFile(results, pluginPath);

            log("[Scanner] Found " + std::to_string(results.size()) + " plugins in " +
                pluginPath.toStdString());

            // Report results
            for (auto* desc : results) {
                sendPluginFound(*desc);
            }

            if (results.isEmpty()) {
                sendError(pluginPath, "No plugins found in file");
            }

            sendComplete();
            log("[Scanner] DONE sent, waiting for QUIT");
        } catch (const std::exception& e) {
            log(std::string("[Scanner] scanOnePlugin EXCEPTION: ") + e.what());
            sendError(pluginPath, juce::String("Exception: ") + e.what());
            sendComplete();
        } catch (...) {
            log("[Scanner] scanOnePlugin UNKNOWN EXCEPTION");
            sendError(pluginPath, "Unknown exception");
            sendComplete();
        }
    }

    void sendPluginFound(const juce::PluginDescription& desc) {
        juce::MemoryBlock msg;
        juce::MemoryOutputStream stream(msg, false);
        stream.writeString(ScannerIPC::MSG_PLUGIN_FOUND);
        stream.writeString(desc.name);
        stream.writeString(desc.pluginFormatName);
        stream.writeString(desc.manufacturerName);
        stream.writeString(desc.version);
        stream.writeString(desc.fileOrIdentifier);
        stream.writeInt(desc.uniqueId);
        stream.writeBool(desc.isInstrument);
        stream.writeString(desc.category);
        sendMessageToCoordinator(msg);
    }

    void sendError(const juce::String& plugin, const juce::String& error) {
        juce::MemoryBlock msg;
        juce::MemoryOutputStream stream(msg, false);
        stream.writeString(ScannerIPC::MSG_ERROR);
        stream.writeString(plugin);
        stream.writeString(error);
        sendMessageToCoordinator(msg);
    }

    void sendComplete() {
        juce::MemoryBlock msg;
        juce::MemoryOutputStream stream(msg, false);
        stream.writeString(ScannerIPC::MSG_SCAN_COMPLETE);
        sendMessageToCoordinator(msg);
    }
};

//==============================================================================
class PluginScannerApplication : public juce::JUCEApplicationBase {
  public:
    const juce::String getApplicationName() override {
        return "MAGDA Plugin Scanner";
    }
    const juce::String getApplicationVersion() override {
        return "1.0.0";
    }
    bool moreThanOneInstanceAllowed() override {
        return true;
    }

    void initialise(const juce::String& commandLine) override {
        initLog();
        log("[Scanner] Starting with args: " + commandLine.toStdString());

        worker_ = std::make_unique<PluginScannerWorker>();

        if (!worker_->initialiseFromCommandLine(commandLine, "magda-plugin-scanner")) {
            log("[Scanner] Failed to initialize from command line");
            setApplicationReturnValue(1);
            quit();
            return;
        }

        log("[Scanner] Initialized successfully, waiting for commands");
    }

    void shutdown() override {
        log("[Scanner] Shutting down");
        worker_.reset();
    }

    void systemRequestedQuit() override {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void unhandledException(const std::exception*, const juce::String&, int) override {
        log("[Scanner] Unhandled exception - exiting");
    }

  private:
    std::unique_ptr<PluginScannerWorker> worker_;
};

//==============================================================================
START_JUCE_APPLICATION(PluginScannerApplication)
