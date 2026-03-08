#include "../dialogs/ExportAudioDialog.hpp"
#include "MainWindow.hpp"
#include "audio/AudioBridge.hpp"
#include "core/Config.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"

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
                         const juce::File& outputFile,
                         tracktion::engine::TransportControl& transport,
                         std::function<void()> onComplete, double prerollSeconds = 0.0,
                         double leadInSilence = 0.0)
        : ThreadWithProgressWindow("Exporting Audio...", true, true),
          params_(params),
          outputFile_(outputFile),
          reallocationInhibitor_(transport),
          onComplete_(std::move(onComplete)),
          prerollSeconds_(prerollSeconds),
          leadInSilence_(leadInSilence) {
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
                    if (prerollSeconds_ > 0.0) {
                        setStatusMessage("Trimming preroll...");
                        if (!trimPreroll()) {
                            success_ = false;
                            errorMessage_ = "Render succeeded but failed to trim preroll.";
                            break;
                        }
                    }
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
        // Capture state before delete. We must defer alert window creation to a
        // separate message-loop iteration because threadComplete() is called from
        // a JUCE timer callback, and creating a top-level window (AlertWindow)
        // inside a timer callback triggers a macOS CVDisplayLink refresh that
        // crashes (EXC_BREAKPOINT in CVDisplayLinkStop).
        auto success = success_;
        auto errorMessage = errorMessage_;
        auto outputFile = outputFile_;
        auto onComplete = std::move(onComplete_);

        // Delete self first — safe because we've captured everything we need
        // and JUCE guarantees no further callbacks after threadComplete().
        delete this;

        juce::MessageManager::callAsync([=]() {
            if (onComplete)
                onComplete();
            if (userPressedCancel) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                       "Export Cancelled", "Export was cancelled.");
            } else if (success) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, "Export Complete",
                    "Audio exported successfully to:\n" + outputFile.getFullPathName());
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Export Failed",
                    errorMessage.isEmpty() ? "Unknown error occurred during export" : errorMessage);
            }
        });
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
    // Trims the warmup preroll from the start, keeping any portion
    // that overlaps with the user-requested lead-in silence.
    bool trimPreroll() {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(outputFile_));
        if (!reader)
            return false;

        // Keep lead-in silence from the preroll (don't trim it)
        auto effectiveTrim = std::max(0.0, prerollSeconds_ - leadInSilence_);
        auto samplesToSkip = (juce::int64)(effectiveTrim * reader->sampleRate);
        auto samplesToKeep = reader->lengthInSamples - samplesToSkip;
        if (samplesToKeep <= 0)
            return false;

        auto tempFile = outputFile_.getSiblingFile(outputFile_.getFileNameWithoutExtension() +
                                                   "_tmp" + outputFile_.getFileExtension());

        std::unique_ptr<juce::AudioFormat> format;
        if (outputFile_.hasFileExtension(".flac"))
            format = std::make_unique<juce::FlacAudioFormat>();
        else
            format = std::make_unique<juce::WavAudioFormat>();

        std::unique_ptr<juce::OutputStream> outputStream =
            std::make_unique<juce::FileOutputStream>(tempFile);
        auto writerOptions = juce::AudioFormatWriterOptions()
                                 .withSampleRate(reader->sampleRate)
                                 .withNumChannels((int)reader->numChannels)
                                 .withBitsPerSample((int)reader->bitsPerSample);
        auto writer = format->createWriterFor(outputStream, writerOptions);
        if (!writer)
            return false;

        writer->writeFromAudioReader(*reader, samplesToSkip, samplesToKeep);
        writer.reset();
        reader.reset();

        outputFile_.deleteFile();
        return tempFile.moveFileTo(outputFile_);
    }

    tracktion::Renderer::Parameters params_;
    juce::File outputFile_;
    tracktion::engine::TransportControl::ReallocationInhibitor reallocationInhibitor_;
    std::function<void()> onComplete_;
    std::unique_ptr<tracktion::Renderer::RenderTask> renderTask_;
    double prerollSeconds_ = 0.0;
    double leadInSilence_ = 0.0;
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

    // Build default output path from render preferences
    auto& config = Config::getInstance();
    juce::File defaultDir;
    auto renderFolder = config.getRenderFolder();
    if (!renderFolder.empty())
        defaultDir = juce::File(renderFolder);
    else
        defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    // Expand file naming pattern for default filename
    juce::String pattern(config.getRenderFilePattern());
    if (pattern.isEmpty())
        pattern = "<project-name>_<date-time>";

    juce::String projName = ProjectManager::getInstance().getProjectName();
    if (projName.isEmpty())
        projName = "untitled";
    projName = projName.replaceCharacters(" /\\:", "____");
    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");

    pattern = pattern.replace("<project-name>", projName);
    pattern = pattern.replace("<date-time>", timestamp);
    pattern = pattern.replace("<clip-name>", projName);   // no clip context in export
    pattern = pattern.replace("<track-name>", "master");  // export is full mix
    pattern = pattern.replaceCharacters("/\\:", "___");

    juce::File defaultFile = defaultDir.getChildFile(pattern + extension);

    // Launch file chooser
    fileChooser_ =
        std::make_unique<juce::FileChooser>("Export Audio", defaultFile, "*" + extension, true);

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

            te::TransportControl::ReallocationInhibitor setupInhibitor(transport);

            te::freePlaybackContextIfNotRecording(transport);

            // Enable all plugins for offline rendering
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

            // Match live playback block size so LFO phase reset timing
            // (which has a one-block delay via pendingNoteOnResync_) behaves
            // identically to live playback.
            params.blockSizeForAudio = 512;
            params.realTimeRender = settings.realTimeRender;

            // Set time range based on export range setting
            te::TimeRange requestedRange;
            using ExportRange = ExportAudioDialog::ExportRange;
            switch (settings.exportRange) {
                case ExportRange::TimeSelection:
                    // TODO: Get actual time selection from SelectionManager when implemented
                    requestedRange = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                                   te::TimePosition() + edit->getLength());
                    break;

                case ExportRange::LoopRegion:
                    requestedRange = edit->getTransport().getLoopRange();
                    break;

                case ExportRange::EntireSong:
                default:
                    requestedRange = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                                   te::TimePosition() + edit->getLength());
                    break;
            }

            // Add preroll for offline renders to let plugins settle.
            // Even with the default 512 block size, some plugins need extra
            // warmup time. The preroll is rendered then trimmed off.
            constexpr double prerollSeconds = 2.0;
            double actualPreroll = 0.0;
            if (!settings.realTimeRender) {
                actualPreroll = prerollSeconds;
                params.time = te::TimeRange(requestedRange.getStart() -
                                                te::TimeDuration::fromSeconds(actualPreroll),
                                            requestedRange.getEnd());
            } else {
                params.time = requestedRange;
            }

            // Enable MIDI-triggered LFO modulation for offline rendering.
            // During live playback, the message-thread timer gates these LFOs
            // based on held notes, but it can't keep up with non-real-time rendering.
            auto* audioBridge = engine->getAudioBridge();
            if (audioBridge)
                audioBridge->getPluginManager().prepareForRendering();

            // Launch progress window with background rendering (non-blocking)
            // The window will delete itself via threadComplete() callback.
            // ExportProgressWindow holds a ReallocationInhibitor to prevent
            // edit.restartPlayback() from recreating the playback context during render.
            auto* progressWindow = new ExportProgressWindow(
                params, file, transport,
                [audioBridge]() {
                    if (audioBridge)
                        audioBridge->getPluginManager().restoreAfterRendering();
                },
                actualPreroll, settings.leadInSilence);
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
