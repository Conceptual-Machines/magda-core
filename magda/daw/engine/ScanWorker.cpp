#include "ScanWorker.hpp"

namespace magda {

ScanWorker::ScanWorker(int index, const juce::File& scannerExe, ResultCallback callback)
    : workerIndex_(index), scannerExe_(scannerExe), resultCallback_(std::move(callback)) {}

ScanWorker::~ScanWorker() {
    busy_ = false;
    killWorkerProcess();
}

void ScanWorker::scanPlugin(const juce::String& formatName, const juce::String& pluginPath) {
    jassert(!busy_);

    // Clean up any previous subprocess state before launching a new one
    killWorkerProcess();

    busy_ = true;
    receivedDone_ = false;
    currentFormat_ = formatName;
    currentPlugin_ = pluginPath;
    currentResult_ = {};
    currentResult_.pluginPath = pluginPath;

    if (!launchSubprocess()) {
        juce::Logger::writeToLog("[ScanWorker " + juce::String(workerIndex_) +
                                 "] Failed to launch subprocess for: " + pluginPath);
        reportResultAsync(false, "Failed to launch subprocess");
        return;
    }

    sendScanOneCommand(formatName, pluginPath);
}

void ScanWorker::abort() {
    if (busy_) {
        busy_ = false;
        receivedDone_ = false;
        killWorkerProcess();
    }
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
    return launchWorkerProcess(scannerExe_, "magda-plugin-scanner", 10000, 0);
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
        sendQuit();
        // Success if the scan completed without errors (0 plugins is valid)
        bool scanOk = currentResult_.errorMessage.isEmpty();
        // Defer the result callback so we fully exit the ChildProcessCoordinator's
        // IPC callback before the coordinator tries to launch a new subprocess on
        // this same worker. Without this, launchWorkerProcess is called re-entrantly
        // from within handleMessageFromWorker, causing thread assertion failures.
        reportResultAsync(scanOk);
    }
}

void ScanWorker::handleConnectionLost() {
    if (!busy_) {
        return;
    }

    if (receivedDone_) {
        // Clean exit after we sent QUIT - already reported in handleMessageFromWorker
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
