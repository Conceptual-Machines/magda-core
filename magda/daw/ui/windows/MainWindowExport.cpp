#include <juce_audio_basics/juce_audio_basics.h>

#include "../dialogs/ExportAudioDialog.hpp"
#include "../dialogs/ExportMidiDialog.hpp"
#include "MainWindow.hpp"
#include "audio/AudioBridge.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/TrackManager.hpp"
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

    // Determine export time range
    double rangeStart = 0.0;
    double rangeEnd = 0.0;

    // Find the extent of all MIDI clips
    for (const auto& clip : clips) {
        if (clip.type == ClipType::MIDI) {
            double end = clip.startTime + clip.length;
            if (end > rangeEnd)
                rangeEnd = end;
        }
    }

    if (settings.exportRange == ExportMidiDialog::ExportRange::LoopRegion) {
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        if (engine && engine->getEdit()) {
            auto loopRange = engine->getEdit()->getTransport().getLoopRange();
            rangeStart = loopRange.getStart().inSeconds();
            rangeEnd = loopRange.getEnd().inSeconds();
        }
    }

    DBG("MIDI export range: " << rangeStart << " - " << rangeEnd);

    if (rangeEnd <= rangeStart) {
        DBG("No MIDI clips found - rangeEnd <= rangeStart");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export MIDI",
                                               "No MIDI clips to export.");
        return;
    }

    // Collect MIDI clips grouped by track — copy data to avoid dangling pointers
    // during async file chooser
    struct ClipMidiData {
        double startTime;
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
        if (clip.type != ClipType::MIDI)
            continue;
        if (clip.midiNotes.empty() && clip.midiCCData.empty() && clip.midiPitchBendData.empty())
            continue;

        // Check if clip overlaps with range
        double clipEnd = clip.startTime + clip.length;
        if (clipEnd <= rangeStart || clip.startTime >= rangeEnd)
            continue;

        auto& td = trackData[clip.trackId];
        if (td.trackName.isEmpty()) {
            auto* track = trackManager.getTrack(clip.trackId);
            td.trackName = track ? track->name : "Track";
        }
        td.clips.push_back(
            {clip.startTime, clip.midiNotes, clip.midiCCData, clip.midiPitchBendData});
    }

    DBG("Track data count: " << trackData.size());
    if (trackData.empty()) {
        DBG("No MIDI clips with notes found");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export MIDI",
                                               "No MIDI clips with notes to export.");
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
    fileChooser_ = std::make_unique<juce::FileChooser>("Export MIDI", defaultFile, "*.mid", true);

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                 juce::FileBrowserComponent::warnAboutOverwriting;

    auto midiFormat = settings.midiFormat;
    auto capturedRangeStart = rangeStart;
    auto capturedRangeEnd = rangeEnd;

    fileChooser_->launchAsync(flags, [this, trackData = std::move(trackData), projectTempo,
                                      timeSigNum, timeSigDen, midiFormat, capturedRangeStart,
                                      capturedRangeEnd](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        fileChooser_.reset();

        if (file == juce::File())
            return;

        if (!file.hasFileExtension(".mid"))
            file = file.withFileExtension(".mid");

        constexpr int ticksPerQuarter = 960;
        auto beatsToTicks = [&](double beats) -> double { return beats * ticksPerQuarter; };
        auto secondsToBeats = [&](double seconds) -> double {
            return (seconds * projectTempo) / 60.0;
        };

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(ticksPerQuarter);

        // Tempo and time signature meta-events
        int microsecondsPerBeat = static_cast<int>(60000000.0 / projectTempo);
        auto tempoMsg = juce::MidiMessage::tempoMetaEvent(microsecondsPerBeat);
        tempoMsg.setTimeStamp(0.0);
        auto timeSigMsg = juce::MidiMessage::timeSignatureMetaEvent(timeSigNum, timeSigDen);
        timeSigMsg.setTimeStamp(0.0);

        // Range end in beats (relative to range start) for clamping
        double rangeEndBeats = secondsToBeats(capturedRangeEnd - capturedRangeStart);
        double rangeEndTick = beatsToTicks(rangeEndBeats);

        // Helper to add notes/CC/PB from clips to a sequence
        auto addClipDataToSequence = [&](juce::MidiMessageSequence& seq, const ClipMidiData& clip,
                                         int channel, double& maxTick) {
            double clipStartBeats = secondsToBeats(clip.startTime - capturedRangeStart);

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
                    juce::AlertWindow::InfoIcon, "Export Complete",
                    "MIDI exported successfully to:\n" + file.getFullPathName());
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Export Failed", "Failed to write MIDI file.");
            }
        } else {
            DBG("Failed to open output stream for: " << file.getFullPathName());
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Failed",
                                                   "Could not create output file:\n" +
                                                       file.getFullPathName());
        }
    });
}

}  // namespace magda
