#include "MainWindow.hpp"

#include "../dialogs/ExportAudioDialog.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda {

// ============================================================================
// Export Audio Implementation
// ============================================================================

namespace {

/**
 * Progress window for audio export that runs Tracktion Renderer in background thread
 */
class ExportProgressWindow : public juce::ThreadWithProgressWindow {
  public:
    ExportProgressWindow(const tracktion::Renderer::Parameters& params,
                         const juce::File& outputFile)
        : ThreadWithProgressWindow("Exporting Audio...", true, true),
          params_(params),
          outputFile_(outputFile) {
        setStatusMessage("Preparing to export...");
    }

    void run() override {
        std::atomic<float> progress{0.0f};
        renderTask_ = std::make_unique<tracktion::Renderer::RenderTask>("Export", params_,
                                                                        &progress, nullptr);

        setStatusMessage("Rendering: " + outputFile_.getFileName());

        while (!threadShouldExit()) {
            auto status = renderTask_->runJob();

            // Update progress bar (0.0 to 1.0)
            setProgress(progress.load());

            if (status == juce::ThreadPoolJob::jobHasFinished) {
                // Verify the file was actually created
                if (outputFile_.existsAsFile()) {
                    success_ = true;
                    setStatusMessage("Export complete!");
                    setProgress(1.0);
                } else {
                    success_ = false;
                    errorMessage_ = "Render completed but file was not created. The project may be "
                                    "empty or contain no audio.";
                    setStatusMessage("Export failed");
                }
                break;
            }

            if (status == juce::ThreadPoolJob::jobNeedsRunningAgain) {
                // Brief yield to avoid busy-waiting while keeping render fast
                juce::Thread::sleep(1);
                continue;
            }

            // Error occurred
            errorMessage_ = "Render job failed";
            setStatusMessage("Export failed");
            break;
        }

        if (threadShouldExit() && !success_) {
            errorMessage_ = "Export cancelled by user";
        }
    }

    void threadComplete(bool userPressedCancel) override {
        if (userPressedCancel) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export Cancelled",
                                                   "Export was cancelled.");
        } else if (success_) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export Complete",
                                                   "Audio exported successfully to:\n" +
                                                       outputFile_.getFullPathName());
        } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Export Failed",
                errorMessage_.isEmpty() ? "Unknown error occurred during export" : errorMessage_);
        }

        // ExportProgressWindow uses self-owned lifecycle pattern:
        // Created with 'new', manages itself, and deletes with 'delete this' in threadComplete().
        // This is safe because: 1) threadComplete() is the final callback, 2) JUCE guarantees
        // no further virtual method calls after this, 3) no external code retains ownership.
        delete this;
    }

    bool wasSuccessful() const {
        return success_;
    }
    juce::String getErrorMessage() const {
        return errorMessage_;
    }
    juce::File getOutputFile() const {
        return outputFile_;
    }

  private:
    tracktion::Renderer::Parameters params_;
    juce::File outputFile_;
    std::unique_ptr<tracktion::Renderer::RenderTask> renderTask_;
    bool success_ = false;
    juce::String errorMessage_;
};

}  // namespace

void MainWindow::performExport(const ExportAudioDialog::Settings& settings,
                               TracktionEngineWrapper* engine) {
    namespace te = tracktion;

    if (!engine || !engine->getEdit()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Audio",
                                               "Cannot export: no Edit loaded");
        return;
    }

    auto* edit = engine->getEdit();

    // Determine file extension
    juce::String extension = getFileExtensionForFormat(settings.format);

    // Launch file chooser
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Audio", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*" + extension, true);

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                 juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(
        flags, [this, settings, engine, edit, extension](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File()) {
                fileChooser_.reset();
                return;
            }

            // Ensure correct extension
            if (!file.hasFileExtension(extension)) {
                file = file.withFileExtension(extension);
            }

            // CRITICAL: Stop transport AND free playback context before offline rendering
            // Tracktion Engine asserts that play context is not active during export
            // (assertion in tracktion_NodeRenderContext.cpp:182)
            auto& transport = edit->getTransport();
            if (transport.isPlaying()) {
                transport.stop(false, false);  // Stop immediately without fading
            }

            // Free the playback context if not recording
            // This is essential - the assertion checks isPlayContextActive() which
            // returns (playbackContext != nullptr), so we must free it
            te::freePlaybackContextIfNotRecording(transport);

            // CRITICAL: Enable all plugins for offline rendering
            // When transport stops, AudioBridge bypasses generator plugins (like test tone)
            // but we need them enabled for export to work properly
            for (auto track : te::getAudioTracks(*edit)) {
                for (auto plugin : track->pluginList) {
                    if (!plugin->isEnabled()) {
                        plugin->setEnabled(true);
                    }
                }
            }

            // Create Renderer::Parameters
            te::Renderer::Parameters params(*edit);
            params.destFile = file;

            // Set audio format
            auto& formatManager = engine->getEngine()->getAudioFileFormatManager();
            if (settings.format.startsWith("WAV")) {
                params.audioFormat = formatManager.getWavFormat();
            } else if (settings.format == "FLAC") {
                params.audioFormat = formatManager.getFlacFormat();
            } else {
                params.audioFormat = formatManager.getWavFormat();  // Default
            }

            params.bitDepth = getBitDepthForFormat(settings.format);
            params.sampleRateForAudio = settings.sampleRate;
            params.shouldNormalise = settings.normalize;
            params.normaliseToLevelDb = 0.0f;
            params.useMasterPlugins = true;
            params.usePlugins = true;

            // Allow export even when there are no clips (generator devices can still produce audio)
            params.checkNodesForAudio = false;

            // Optimize for faster-than-realtime offline rendering
            params.blockSizeForAudio = 8192;  // Much larger than default 512 for faster rendering
            params.realTimeRender = false;  // Disable real-time simulation (default, but explicit)

            // Set time range based on export range setting
            using ExportRange = ExportAudioDialog::ExportRange;
            switch (settings.exportRange) {
                case ExportRange::TimeSelection:
                    // TODO: Get actual time selection from SelectionManager when implemented
                    // For now, export entire song (TimeSelection option is disabled in UI until
                    // implemented)
                    params.time = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                                te::TimePosition() + edit->getLength());
                    break;

                case ExportRange::LoopRegion:
                    params.time = edit->getTransport().getLoopRange();
                    break;

                case ExportRange::EntireSong:
                default:
                    // Export entire arrangement
                    params.time = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                                te::TimePosition() + edit->getLength());
                    break;
            }

            // Launch progress window with background rendering (non-blocking)
            // The window will delete itself via threadComplete() callback
            auto* progressWindow = new ExportProgressWindow(params, file);
            progressWindow->launchThread();

            fileChooser_.reset();
        });
}

juce::String MainWindow::getFileExtensionForFormat(const juce::String& format) const {
    if (format.startsWith("WAV"))
        return ".wav";
    else if (format == "FLAC")
        return ".flac";
    return ".wav";  // Default
}

int MainWindow::getBitDepthForFormat(const juce::String& format) const {
    if (format == "WAV16")
        return 16;
    if (format == "WAV24")
        return 24;
    if (format == "WAV32")
        return 32;
    if (format == "FLAC")
        return 24;  // FLAC default
    return 16;      // Default
}

}  // namespace magda
