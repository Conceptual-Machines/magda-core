#include <juce_audio_basics/juce_audio_basics.h>

#include "../dialogs/ExportAudioDialog.hpp"
#include "../dialogs/ExportMidiDialog.hpp"
#include "MainWindow.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/insert_capture/InsertRenderCaptureService.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

// ============================================================================
// Export Audio Implementation
// ============================================================================

namespace {

double timelineStartBeats(const ClipInfo& clip, double bpm) {
    return clip.getStartBeats(bpm);
}

double timelineEndBeats(const ClipInfo& clip, double bpm) {
    return clip.getEndBeats(bpm);
}

/**
 * Progress window for audio export that runs the engine-neutral render task in
 * a background thread.
 */
class ExportProgressWindow : public juce::ThreadWithProgressWindow {
  public:
    ExportProgressWindow(std::unique_ptr<OfflineRenderTask> renderTask,
                         const juce::File& outputFile, std::function<void()> onComplete,
                         double prerollSeconds = 0.0, double leadInSilence = 0.0)
        : ThreadWithProgressWindow(trEllipsis("export.progress.exporting_audio"), true, true),
          renderTask_(std::move(renderTask)),
          outputFile_(outputFile),
          onComplete_(std::move(onComplete)),
          prerollSeconds_(prerollSeconds),
          leadInSilence_(leadInSilence),
          // Snapshot every string run() needs on the message thread. StringTable
          // isn't thread-safe and the user can change language mid-export, so
          // reading it from the background thread would data-race.
          strRendering_(tr("export.progress.rendering")),
          strTrimming_(trEllipsis("export.progress.trimming")),
          strComplete_(tr("export.progress.complete")),
          strFailed_(tr("export.progress.failed")),
          errTrimFailed_(tr("export.error.trim_failed")),
          errFileNotCreated_(tr("export.error.file_not_created")),
          errRenderFailed_(tr("export.error.render_failed")),
          errCancelled_(tr("export.error.cancelled")) {
        setStatusMessage(trEllipsis("export.progress.preparing"));
    }

    void run() override {
        setStatusMessage(strRendering_ + " " + outputFile_.getFileName());
        if (!renderTask_) {
            errorMessage_ = errRenderFailed_;
            return;
        }

        auto result = renderTask_->run([this]() { return threadShouldExit(); },
                                       [this](float progress) { setProgress(progress); });
        if (!result.success) {
            errorMessage_ = threadShouldExit() ? errCancelled_ : result.error;
            if (errorMessage_.isEmpty())
                errorMessage_ = errRenderFailed_;
            setStatusMessage(strFailed_);
            return;
        }

        if (!outputFile_.existsAsFile()) {
            errorMessage_ = errFileNotCreated_;
            setStatusMessage(strFailed_);
            return;
        }
        if (prerollSeconds_ > 0.0) {
            setStatusMessage(strTrimming_);
            if (!trimPreroll()) {
                errorMessage_ = errTrimFailed_;
                setStatusMessage(strFailed_);
                return;
            }
        }
        success_ = true;
        setStatusMessage(strComplete_);
        setProgress(1.0);
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
                                                       tr("export.alert.cancelled_title"),
                                                       tr("export.alert.cancelled_body"));
            } else if (success) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, tr("export.alert.complete_title"),
                    tr("export.alert.audio_success_prefix") + "\n" + outputFile.getFullPathName());
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, tr("export.alert.failed_title"),
                    errorMessage.isEmpty() ? tr("export.error.unknown") : errorMessage);
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

    std::unique_ptr<OfflineRenderTask> renderTask_;
    juce::File outputFile_;
    std::function<void()> onComplete_;
    double prerollSeconds_ = 0.0;
    double leadInSilence_ = 0.0;
    bool success_ = false;
    juce::String errorMessage_;

    // Translated strings snapshotted at construction — safe for run() to read
    // from the background thread while the message thread may mutate StringTable.
    const juce::String strRendering_;
    const juce::String strTrimming_;
    const juce::String strComplete_;
    const juce::String strFailed_;
    const juce::String errTrimFailed_;
    const juce::String errFileNotCreated_;
    const juce::String errRenderFailed_;
    const juce::String errCancelled_;
};

/**
 * Modal progress for the real-time hardware capture pass that precedes the
 * offline render when External FX / Instrument inserts are routed (#1623).
 * The pass ending (complete or cancelled) deletes this via the service's
 * onFinished callback; the Cancel button aborts the pass.
 */
class InsertCaptureProgressBox : private juce::Timer {
  public:
    explicit InsertCaptureProgressBox(magda::InsertRenderCaptureService& service)
        : service_(service),
          window_(tr("export.capture.title"), tr("export.capture.body"),
                  juce::MessageBoxIconType::InfoIcon) {
        window_.addProgressBarComponent(progress_);
        window_.addButton(tr("export.capture.cancel"), 0);
        // cancelCapturePass no-ops once the pass has ended, so the callback is
        // safe whether Cancel was clicked or the window is being torn down.
        window_.enterModalState(true, juce::ModalCallbackFunction::create(
                                          [&service](int) { service.cancelCapturePass(); }));
        startTimerHz(10);
    }

    ~InsertCaptureProgressBox() override {
        stopTimer();
    }

  private:
    void timerCallback() override {
        progress_ = service_.getProgress();
    }

    magda::InsertRenderCaptureService& service_;
    double progress_ = 0.0;
    juce::AlertWindow window_;
};

}  // namespace

void MainWindow::performExport(const ExportAudioDialog::Settings& settings, AudioEngine* engine) {
    if (!engine || !engine->hasActiveEdit()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("dialogs.export_audio"),
                                               tr("dialogs.error.export_no_edit"));
        return;
    }

    launchAudioExport(settings, engine);
}

void MainWindow::launchAudioExport(const ExportAudioDialog::Settings& settings,
                                   AudioEngine* engine) {
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
    fileChooser_ = std::make_unique<juce::FileChooser>(tr("dialogs.export_audio"), defaultFile,
                                                       "*" + extension, true);

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                 juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(
        flags, [this, settings, engine, extension](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File()) {
                fileChooser_.reset();
                return;
            }

            // Ensure correct extension
            if (!file.hasFileExtension(extension)) {
                file = file.withFileExtension(extension);
            }

            // Export range — also the capture window for external inserts.
            double requestedStart = 0.0;
            double requestedEnd = engine->getEditLengthSeconds();
            using ExportRange = ExportAudioDialog::ExportRange;
            switch (settings.exportRange) {
                case ExportRange::TimeSelection:
                    // TODO: Get actual time selection from SelectionManager when implemented
                    break;

                case ExportRange::LoopRegion: {
                    const auto loop = engine->getLoopRegionSeconds();
                    requestedStart = loop.first;
                    requestedEnd = loop.second;
                    break;
                }

                case ExportRange::EntireSong:
                default:
                    break;
            }

            // The offline render itself, launched directly or after the capture
            // pass below has recorded the external inserts' returns.
            auto launchRender = [this, settings, engine, file, requestedStart, requestedEnd]() {
                OfflineRenderRequest request;
                request.destination = file;
                request.format = settings.format == "FLAC" ? OfflineRenderFormat::Flac
                                                           : OfflineRenderFormat::Wav;
                request.bitDepth = getBitDepthForFormat(settings.format);
                request.sampleRate = settings.sampleRate;
                request.shouldNormalise = settings.normalize;
                request.useMasterPlugins = true;
                request.usePlugins = true;
                request.realTimeRender = settings.realTimeRender;
                request.startSeconds = requestedStart;
                request.endSeconds = requestedEnd;

                // The chord track is monitor-only: exclude it from the bounce so its
                // notes never reach the master render.
                if (auto chordId = magda::TrackManager::getInstance().getChordTrackId();
                    chordId != magda::INVALID_TRACK_ID)
                    request.excludedTrackIds = {chordId};

                // Add preroll for offline renders to let plugins settle.
                // Even with the default 512 block size, some plugins need extra
                // warmup time. The preroll is rendered then trimmed off.
                constexpr double prerollSeconds = 2.0;
                double actualPreroll = 0.0;
                if (!settings.realTimeRender) {
                    actualPreroll = prerollSeconds;
                    request.startSeconds -= actualPreroll;
                }

                // Launch progress window with background rendering (non-blocking)
                // The window will delete itself via threadComplete() callback.
                auto* captureService = engine->getInsertRenderCaptureService();
                auto* progressWindow = new ExportProgressWindow(
                    engine->createOfflineRenderTask(request), file,
                    [captureService]() {
                        // Remove the hidden capture taps + temp files (no-op when
                        // no capture pass ran).
                        if (captureService)
                            captureService->cleanupAfterRender();
                    },
                    actualPreroll, settings.leadInSilence);
                progressWindow->launchThread();
            };

            // External FX / Instrument inserts can't render offline — run the
            // real-time capture pass first (#1623), then render. The pass plays
            // the export range once through the live engine while hidden taps
            // record each insert's return; during the render the same taps
            // substitute the recordings at the insert position.
            auto* captureService = engine->getInsertRenderCaptureService();
            if (captureService != nullptr && captureService->exportNeedsCapturePass()) {
                using PassError = InsertRenderCaptureService::PassError;
                auto* progressBox = new InsertCaptureProgressBox(*captureService);
                const bool started = captureService->startCapturePass(
                    requestedStart, requestedEnd, settings.sampleRate,
                    [launchRender, progressBox, captureService](bool success) {
                        juce::MessageManager::callAsync([progressBox]() { delete progressBox; });
                        if (success) {
                            launchRender();
                        } else {
                            // A recorded error is a capture failure the user must
                            // see; otherwise the user cancelled the pass.
                            const auto error = captureService->getLastPassError();
                            captureService->cleanupAfterRender();
                            if (error != PassError::None) {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::AlertWindow::WarningIcon, tr("export.alert.failed_title"),
                                    tr("export.capture.error_failed"));
                            } else {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::AlertWindow::InfoIcon, tr("export.alert.cancelled_title"),
                                    tr("export.alert.cancelled_body"));
                            }
                        }
                    });
                if (!started) {
                    delete progressBox;
                    // Aborting here beats rendering an export with silent
                    // hardware returns; only proceed when there was genuinely
                    // nothing to capture.
                    if (captureService->getLastPassError() != PassError::None) {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                               tr("export.alert.failed_title"),
                                                               tr("export.capture.error_setup"));
                    } else {
                        launchRender();
                    }
                }
            } else {
                launchRender();
            }

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

// ============================================================================
// Export MIDI Implementation
// ============================================================================

void MainWindow::performMidiExport(const ExportMidiDialog::Settings& settings) {
    DBG("performMidiExport called, format=" << settings.midiFormat);
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();
    const auto& clips = clipManager.getArrangementClips();

    DBG("Total arrangement clips: " << clips.size());

    double projectTempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (projectTempo <= 0.0)
        projectTempo = 120.0;

    int timeSigNum = ProjectManager::getInstance().getCurrentProjectInfo().timeSignatureNumerator;
    int timeSigDen = ProjectManager::getInstance().getCurrentProjectInfo().timeSignatureDenominator;
    if (timeSigNum <= 0)
        timeSigNum = 4;
    if (timeSigDen <= 0)
        timeSigDen = 4;

    // Determine export range in timeline beats. MIDI file ticks are beat-based,
    // so seconds should only appear at external Tracktion API boundaries.
    double rangeStartBeats = 0.0;
    double rangeEndBeats = 0.0;

    // Find the extent of all MIDI clips
    for (const auto& clip : clips) {
        if (clip.isMidi()) {
            double endBeats = timelineEndBeats(clip, projectTempo);
            if (endBeats > rangeEndBeats)
                rangeEndBeats = endBeats;
        }
    }

    if (settings.exportRange == ExportMidiDialog::ExportRange::LoopRegion) {
        auto* engine = mainComponent->getAudioEngine();
        if (engine && engine->hasActiveEdit()) {
            const auto loopRange = engine->getLoopRegionSeconds();
            rangeStartBeats = loopRange.first * projectTempo / 60.0;
            rangeEndBeats = loopRange.second * projectTempo / 60.0;
        }
    }

    DBG("MIDI export range beats: " << rangeStartBeats << " - " << rangeEndBeats);

    if (rangeEndBeats <= rangeStartBeats) {
        DBG("No MIDI clips found - rangeEndBeats <= rangeStartBeats");
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            tr("action.export")
                .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)),
            tr("export.error.no_midi_clips")
                .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)));
        return;
    }

    // Collect MIDI clips grouped by track — copy data to avoid dangling pointers
    // during async file chooser
    struct ClipMidiData {
        double startBeats;
        std::vector<MidiNote> midiNotes;
        std::vector<MidiCCData> midiCCData;
        std::vector<MidiPitchBendData> midiPitchBendData;
    };
    struct TrackMidiData {
        juce::String trackName;
        std::vector<ClipMidiData> clips;
    };
    std::map<TrackId, TrackMidiData> trackData;

    for (const auto& clip : clips) {
        if (!clip.isMidi())
            continue;
        if (clip.midiNotes.empty() && clip.midiCCData.empty() && clip.midiPitchBendData.empty())
            continue;

        // Check if clip overlaps with range
        const double clipStartBeats = timelineStartBeats(clip, projectTempo);
        const double clipEndBeats = timelineEndBeats(clip, projectTempo);
        if (clipEndBeats <= rangeStartBeats || clipStartBeats >= rangeEndBeats)
            continue;

        auto& td = trackData[clip.trackId];
        if (td.trackName.isEmpty()) {
            auto* track = trackManager.getTrack(clip.trackId);
            td.trackName = track ? track->name : "Track";
        }
        td.clips.push_back(
            {clipStartBeats, clip.midiNotes, clip.midiCCData, clip.midiPitchBendData});
    }

    DBG("Track data count: " << trackData.size());
    if (trackData.empty()) {
        DBG("No MIDI clips with notes found");
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            tr("action.export")
                .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)),
            tr("export.error.no_midi_notes")
                .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)));
        return;
    }

    // Build default output path
    juce::String projName = ProjectManager::getInstance().getProjectName();
    if (projName.isEmpty())
        projName = "untitled";
    projName = projName.replaceCharacters(" /\\:", "____");

    auto defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto defaultFile = defaultDir.getChildFile(projName + ".mid");

    // Launch file chooser
    fileChooser_ = std::make_unique<juce::FileChooser>(
        tr("action.export").replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)),
        defaultFile, "*.mid", true);

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                 juce::FileBrowserComponent::warnAboutOverwriting;

    auto midiFormat = settings.midiFormat;
    auto capturedRangeStartBeats = rangeStartBeats;
    auto capturedRangeEndBeats = rangeEndBeats;

    fileChooser_->launchAsync(flags, [this, trackData = std::move(trackData), projectTempo,
                                      timeSigNum, timeSigDen, midiFormat, capturedRangeStartBeats,
                                      capturedRangeEndBeats](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        fileChooser_.reset();

        if (file == juce::File())
            return;

        if (!file.hasFileExtension(".mid"))
            file = file.withFileExtension(".mid");

        constexpr int ticksPerQuarter = 960;
        auto beatsToTicks = [&](double beats) -> double { return beats * ticksPerQuarter; };

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(ticksPerQuarter);

        // Tempo and time signature meta-events
        int microsecondsPerBeat = static_cast<int>(60000000.0 / projectTempo);
        auto tempoMsg = juce::MidiMessage::tempoMetaEvent(microsecondsPerBeat);
        tempoMsg.setTimeStamp(0.0);
        auto timeSigMsg = juce::MidiMessage::timeSignatureMetaEvent(timeSigNum, timeSigDen);
        timeSigMsg.setTimeStamp(0.0);

        // Range end in beats (relative to range start) for clamping.
        double relativeRangeEndBeats = capturedRangeEndBeats - capturedRangeStartBeats;
        double rangeEndTick = beatsToTicks(relativeRangeEndBeats);

        // Helper to add notes/CC/PB from clips to a sequence
        auto addClipDataToSequence = [&](juce::MidiMessageSequence& seq, const ClipMidiData& clip,
                                         int channel, double& maxTick) {
            double clipStartBeats = clip.startBeats - capturedRangeStartBeats;

            for (const auto& note : clip.midiNotes) {
                double startTick = beatsToTicks(clipStartBeats + note.startBeat);
                double endTick = beatsToTicks(clipStartBeats + note.startBeat + note.lengthBeats);
                if (startTick < 0.0)
                    startTick = 0.0;
                if (startTick >= rangeEndTick)
                    continue;
                if (endTick > rangeEndTick)
                    endTick = rangeEndTick;

                auto noteOn = juce::MidiMessage::noteOn(channel, note.noteNumber,
                                                        static_cast<juce::uint8>(note.velocity));
                noteOn.setTimeStamp(startTick);
                seq.addEvent(noteOn);

                auto noteOff = juce::MidiMessage::noteOff(channel, note.noteNumber);
                noteOff.setTimeStamp(endTick);
                seq.addEvent(noteOff);

                maxTick = std::max(maxTick, endTick);
            }

            for (const auto& cc : clip.midiCCData) {
                double tick = beatsToTicks(clipStartBeats + cc.beatPosition);
                if (tick < 0.0)
                    tick = 0.0;
                if (tick >= rangeEndTick)
                    continue;
                auto msg = juce::MidiMessage::controllerEvent(channel, cc.controller, cc.value);
                msg.setTimeStamp(tick);
                seq.addEvent(msg);
                maxTick = std::max(maxTick, tick);
            }

            for (const auto& pb : clip.midiPitchBendData) {
                double tick = beatsToTicks(clipStartBeats + pb.beatPosition);
                if (tick < 0.0)
                    tick = 0.0;
                if (tick >= rangeEndTick)
                    continue;
                auto msg = juce::MidiMessage::pitchWheel(channel, pb.value);
                msg.setTimeStamp(tick);
                seq.addEvent(msg);
                maxTick = std::max(maxTick, tick);
            }
        };

        if (midiFormat == 0) {
            // Type 0: everything in a single track (including tempo meta-events)
            juce::MidiMessageSequence seq;
            seq.addEvent(tempoMsg);
            seq.addEvent(timeSigMsg);
            double maxTick = 0.0;

            for (const auto& [trackId, td] : trackData)
                for (const auto& clip : td.clips)
                    addClipDataToSequence(seq, clip, 1, maxTick);

            seq.sort();
            auto eot = juce::MidiMessage::endOfTrack();
            eot.setTimeStamp(maxTick + 1.0);
            seq.addEvent(eot);
            midiFile.addTrack(seq);

            DBG("Type 0: single track with " << seq.getNumEvents() << " events");
        } else {
            // Type 1: track 0 = tempo, then one track per MAGDA track
            juce::MidiMessageSequence tempoTrack;
            tempoTrack.addEvent(tempoMsg);
            tempoTrack.addEvent(timeSigMsg);
            auto eotTempo = juce::MidiMessage::endOfTrack();
            eotTempo.setTimeStamp(0.0);
            tempoTrack.addEvent(eotTempo);
            midiFile.addTrack(tempoTrack);

            int trackIdx = 0;
            for (const auto& [trackId, td] : trackData) {
                juce::MidiMessageSequence seq;
                double maxTick = 0.0;
                int channel = 1;

                auto nameMsg = juce::MidiMessage::textMetaEvent(3, td.trackName);
                nameMsg.setTimeStamp(0.0);
                seq.addEvent(nameMsg);

                for (const auto& clip : td.clips)
                    addClipDataToSequence(seq, clip, channel, maxTick);

                seq.sort();
                auto eot = juce::MidiMessage::endOfTrack();
                eot.setTimeStamp(maxTick + 1.0);
                seq.addEvent(eot);
                midiFile.addTrack(seq);

                DBG("Type 1 track " << trackIdx << " (" << td.trackName
                                    << "): " << seq.getNumEvents() << " events");
                trackIdx++;
            }
        }

        // Write the MIDI file
        DBG("Writing MIDI file to: " << file.getFullPathName());
        juce::FileOutputStream stream(file);
        if (stream.openedOk()) {
            stream.setPosition(0);
            stream.truncate();
            bool written = midiFile.writeTo(stream, midiFormat == 0 ? 0 : 1);
            stream.flush();

            DBG("MIDI write result: " << (written ? "success" : "failed")
                                      << ", file size: " << file.getSize());

            if (written) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, tr("export.alert.complete_title"),
                    tr("export.alert.midi_success_prefix")
                            .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)) +
                        "\n" + file.getFullPathName());
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, tr("export.alert.failed_title"),
                    tr("export.error.midi_write_failed")
                        .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)));
            }
        } else {
            DBG("Failed to open output stream for: " << file.getFullPathName());
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, tr("export.alert.failed_title"),
                tr("export.error.cannot_create_file") + "\n" + file.getFullPathName());
        }
    });
}

}  // namespace magda
