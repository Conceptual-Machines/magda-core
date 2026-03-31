#include "ScanWorker.hpp"

namespace magda {

ScanWorker::ScanWorker(int index, const juce::File& scannerExe, ResultCallback callback)
    : workerIndex_(index), scannerExe_(scannerExe), resultCallback_(std::move(callback)) {}

ScanWorker::~ScanWorker() {
    busy_ = false;
    connected_ = false;
    killWorkerProcess();
}

void ScanWorker::scanPlugin(const juce::String& formatName, const juce::String& pluginPath) {
    jassert(!busy_);

#if !JUCE_WINDOWS
    // On Mac/Linux, always start a fresh subprocess per plugin to avoid
    // accumulated state from DLL load/unload and to keep process count clean.
    // IMPORTANT: Kill BEFORE setting busy_/receivedDone_, because
    // killWorkerProcess() synchronously triggers handleConnectionLost(),
    // which would report a false crash if busy_ were already true.
    if (connected_) {
        killWorkerProcess();
        connected_ = false;
    }
#endif

    busy_ = true;
    receivedDone_ = false;
    currentFormat_ = formatName;
    currentPlugin_ = pluginPath;
    currentResult_ = {};
    currentResult_.pluginPath = pluginPath;

#if JUCE_WINDOWS
    // Reuse existing subprocess if still connected; only launch a new one
    // when this is the first scan or after a crash killed the previous process.
    // On Windows, CreateProcess is expensive so we keep the subprocess alive.
    if (!connected_) {
#else
    {
#endif
        if (!launchSubprocess()) {
            juce::Logger::writeToLog("[ScanWorker " + juce::String(workerIndex_) +
                                     "] Failed to launch subprocess for: " + pluginPath);
            reportResultAsync(false, "Failed to launch subprocess");
            return;
        }
    }

    sendScanOneCommand(formatName, pluginPath);
}

void ScanWorker::abort() {
    if (busy_) {
        busy_ = false;
        receivedDone_ = false;
    }
    connected_ = false;
    killWorkerProcess();
}

bool ScanWorker::launchSubprocess() {
    if (!scannerExe_.existsAsFile()) {
        DBG("[ScanWorker " << workerIndex_ << "] Scanner executable not found");
        return false;
    }

    // The unique ID is just a command-line marker prefix; each launch creates its
    // own random pipe, so the same ID is safe for parallel workers.
    // streamFlags = 0: do NOT capture stdout/stderr — the scanner writes log lines
    // to stdout, and if the pipe buffer fills up the subprocess blocks, deadlocking
    // both processes (same fix as Tracktion Engine's own scanner).
    connected_ = launchWorkerProcess(scannerExe_, "magda-plugin-scanner", 10000, 0);
    return connected_;
}

void ScanWorker::sendScanOneCommand(const juce::String& formatName,
                                    const juce::String& pluginPath) {
    juce::MemoryBlock msg;
    juce::MemoryOutputStream stream(msg, false);
    stream.writeString(ScannerIPC::MSG_SCAN_ONE);
    stream.writeString(formatName);
    stream.writeString(pluginPath);
    sendMessageToWorker(msg);
}

void ScanWorker::sendQuit() {
    juce::MemoryBlock msg;
    juce::MemoryOutputStream stream(msg, false);
    stream.writeString(ScannerIPC::MSG_QUIT);
    sendMessageToWorker(msg);
}

void ScanWorker::handleMessageFromWorker(const juce::MemoryBlock& message) {
    juce::MemoryInputStream stream(message, false);
    juce::String msgType = stream.readString();

    if (msgType == ScannerIPC::MSG_PLUGIN_FOUND) {
        juce::PluginDescription desc;
        desc.name = stream.readString();
        desc.pluginFormatName = stream.readString();
        desc.manufacturerName = stream.readString();
        desc.version = stream.readString();
        desc.fileOrIdentifier = stream.readString();
        desc.uniqueId = stream.readInt();
        desc.isInstrument = stream.readBool();
        desc.category = stream.readString();

        currentResult_.foundPlugins.add(desc);
        DBG("[ScanWorker " << workerIndex_ << "] Found: " << desc.name << " ("
                           << desc.pluginFormatName << ")");
    } else if (msgType == ScannerIPC::MSG_ERROR) {
        juce::String plugin = stream.readString();
        juce::String error = stream.readString();
        currentResult_.errorMessage = error;
        DBG("[ScanWorker " << workerIndex_ << "] Error: " << plugin << " - " << error);
    } else if (msgType == ScannerIPC::MSG_SCAN_COMPLETE) {
        receivedDone_ = true;
        bool scanOk = currentResult_.errorMessage.isEmpty();
        // Defer the result callback so we fully exit the ChildProcessCoordinator's
        // IPC callback before the coordinator tries to send the next scan command
        // on this same worker.
        reportResultAsync(scanOk);
    }
}

void ScanWorker::handleConnectionLost() {
    connected_ = false;

    if (!busy_) {
        return;
    }

    if (receivedDone_) {
        // Subprocess exited after scan completed — not an error
        return;
    }

    // Subprocess crashed before sending DONE
    juce::Logger::writeToLog("[ScanWorker " + juce::String(workerIndex_) +
                             "] Subprocess CRASHED while scanning: " + currentPlugin_);
    // Also defer crash results to avoid re-entrant issues
    reportResultAsync(false, "crash");
}

void ScanWorker::reportResultAsync(bool success, const juce::String& error) {
    // Capture result state now, then deliver asynchronously
    currentResult_.success = success;
    if (error.isNotEmpty())
        currentResult_.errorMessage = error;

    busy_ = false;

    auto callback = resultCallback_;
    auto index = workerIndex_;
    auto result = currentResult_;

    juce::MessageManager::callAsync([callback, index, result]() {
        if (callback)
            callback(index, result);
    });
}

}  // namespace magda
