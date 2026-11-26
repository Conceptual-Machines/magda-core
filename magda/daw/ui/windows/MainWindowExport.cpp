// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "../dialogs/ExportAudioDialog.hpp"
#include "../dialogs/ExportFormats.hpp"
#include "../dialogs/ExportMidiDialog.hpp"
#include "MainWindow.hpp"
#include "audio/insert_capture/InsertRenderCapture.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/host/StemRender.hpp"
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

/** @brief A render session and the one task it is running, which the message thread makes. */
struct ExportRenderState {
    std::unique_ptr<OfflineRenderSession> session;

    /// Declared after the session so it is destroyed first: it borrows the session's devices.
    std::unique_ptr<OfflineRenderTask> task;
    juce::WaitableEvent taskReady;
};

/**
 * Progress window for audio export that runs the engine-neutral render tasks in
 * a background thread, one file after another.
 */
class ExportProgressWindow : public juce::ThreadWithProgressWindow {
  public:
    ExportProgressWindow(std::unique_ptr<OfflineRenderSession> renderSession,
                         std::vector<OfflineRenderRequest> requests, const juce::String& title,
                         juce::String successMessage, std::function<void()> onComplete)
        : ThreadWithProgressWindow(title, true, true),
          state_(std::make_shared<ExportRenderState>()),
          requests_(std::move(requests)),
          successMessage_(std::move(successMessage)),
          onComplete_(std::move(onComplete)),
          // Snapshot every string run() needs on the message thread. StringTable
          // isn't thread-safe and the user can change language mid-export, so
          // reading it from the background thread would data-race.
          strRendering_(tr("export.progress.rendering")),
          strComplete_(tr("export.progress.complete")),
          strFailed_(tr("export.progress.failed")),
          errFileNotCreated_(tr("export.error.file_not_created")),
          errRenderFailed_(tr("export.error.render_failed")),
          errCancelled_(tr("export.error.cancelled")) {
        state_->session = std::move(renderSession);
        setStatusMessage(trEllipsis("export.progress.preparing"));
    }

    void run() override {
        const auto count = static_cast<double>(requests_.size());
        for (std::size_t index = 0; index < requests_.size(); ++index) {
            if (!renderOne(requests_[index], [this, index, count](float progress) {
                    setProgress((static_cast<double>(index) + progress) / count);
                })) {
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
        auto successMessage = successMessage_;
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
                    juce::AlertWindow::InfoIcon, tr("export.alert.complete_title"), successMessage);
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, tr("export.alert.failed_title"),
                    errorMessage.isEmpty() ? tr("export.error.unknown") : errorMessage);
            }
        });
    }

  private:
    /**
     * @brief Render @p request into its file. Render thread.
     *
     * A task reads the model and starts its own threads, so the message thread makes each one
     * as the last is dropped. The wait stays cancellable: Cancel holds the message thread
     * until this thread stops.
     */
    bool renderOne(const OfflineRenderRequest& request,
                   const std::function<void(float)>& progress) {
        setStatusMessage(strRendering_ + " " + request.destination.getFileName());

        state_->taskReady.reset();
        juce::MessageManager::callAsync([weak = std::weak_ptr(state_), request] {
            if (const auto state = weak.lock()) {
                state->task.reset();
                if (state->session)
                    state->task = state->session->createTask(request);
                state->taskReady.signal();
            }
        });

        while (!state_->taskReady.wait(20)) {
            if (threadShouldExit()) {
                errorMessage_ = errCancelled_;
                return false;
            }
        }

        if (!state_->task) {
            errorMessage_ = errRenderFailed_;
            return false;
        }

        const auto result = state_->task->run([this]() { return threadShouldExit(); }, progress);

        if (!result.success) {
            errorMessage_ = threadShouldExit() ? errCancelled_ : result.error;
            if (errorMessage_.isEmpty())
                errorMessage_ = errRenderFailed_;
            return false;
        }

        if (!request.destination.existsAsFile()) {
            errorMessage_ = errFileNotCreated_;
            return false;
        }
        return true;
    }

    std::shared_ptr<ExportRenderState> state_;
    std::vector<OfflineRenderRequest> requests_;
    juce::String successMessage_;
    std::function<void()> onComplete_;
    bool success_ = false;
    juce::String errorMessage_;

    // Translated strings snapshotted at construction — safe for run() to read
    // from the background thread while the message thread may mutate StringTable.
    const juce::String strRendering_;
    const juce::String strComplete_;
    const juce::String strFailed_;
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
    explicit InsertCaptureProgressBox(magda::InsertRenderCapture& service)
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

    magda::InsertRenderCapture& service_;
    double progress_ = 0.0;
    juce::AlertWindow window_;
};

/** @brief What every file of one export shares: its format, range, tail and lead-in. */
OfflineRenderRequest exportRequest(const ExportAudioDialog::Settings& settings, BeatRange range) {
    OfflineRenderRequest request;
    request.format = exportRenderFormatFor(settings.format);
    request.bitDepth = settings.bitDepth;
    request.bitrateKbps = settings.bitrateKbps;
    request.vbrQuality = settings.vbrQuality;
    request.dither = settings.dither;
    request.sampleRate = settings.sampleRate;
    request.shouldNormalise = settings.normalize;
    request.realTimeRender = settings.realTimeRender;
    request.range = range;
    // The longest tail the rendered devices declare, loops included (#3278).
    request.tailSeconds = std::nullopt;
    if (settings.exportRange == ExportAudioDialog::ExportRange::EntireSong)
        request.oneShot = true;
    else if (settings.exportRange == ExportAudioDialog::ExportRange::LoopRegion)
        request.oneShot = false;
    request.leadInSeconds = settings.leadInSilence;
    return request;
}

/** @brief "07 Bass.wav": the stem's place in the export, then its track's name. */
juce::String stemFileName(std::size_t number, std::size_t count, const juce::String& trackName,
                          const juce::String& extension) {
    const auto digits = std::max(2, juce::String(static_cast<int>(count)).length());
    const auto name = juce::File::createLegalFileName(trackName.isNotEmpty() ? trackName : "Track");
    return juce::String(static_cast<int>(number)).paddedLeft('0', digits) + " " + name + extension;
}

/** @brief One request per stem @p settings asks for, written into @p folder (#54). */
std::vector<OfflineRenderRequest> stemRequests(const OfflineRenderRequest& base,
                                               const ExportAudioDialog::Settings& settings,
                                               const juce::File& folder,
                                               const juce::String& extension) {
    const auto& trackManager = TrackManager::getInstance();
    const auto& tracks = trackManager.getTracks();

    auto stems = daw::engine_host::stemTracks(tracks);
    if (!settings.stemTrackIds.empty())
        std::erase_if(stems, [&settings](TrackId id) {
            return !std::ranges::contains(settings.stemTrackIds, id);
        });

    std::vector<OfflineRenderRequest> requests;
    for (const auto trackId : stems) {
        const auto* track = trackManager.getTrack(trackId);
        auto request = base;
        request.destination = folder.getChildFile(
            stemFileName(requests.size() + 1, stems.size(),
                         track != nullptr ? track->name : juce::String(), extension));
        request.useMasterPlugins = false;
        request.useTrackEffects = settings.stemTrackEffects;
        request.trackIds = daw::engine_host::stemSources(tracks, trackId);
        request.stemTrackId = trackId;
        requests.push_back(std::move(request));
    }
    return requests;
}

/** @brief Render @p requests, after the capture pass when hardware inserts need one (#1623). */
void startAudioExport(AudioEngine* engine, std::vector<OfflineRenderRequest> requests,
                      const juce::String& title, const juce::String& successMessage,
                      double startSeconds, double endSeconds, bool resumePlayback) {
    auto* captureService = engine->getInsertRenderCapture();

    auto launchRender = [engine, requests, title, successMessage, resumePlayback,
                         captureService]() {
        auto renderSession = engine->createOfflineRenderSession(resumePlayback);
        if (!renderSession) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   tr("dialogs.export_audio"),
                                                   tr("export.error.render_failed"));
            if (captureService)
                captureService->cleanupAfterRender();
            return;
        }

        // The window deletes itself via threadComplete().
        auto* progressWindow = new ExportProgressWindow(
            std::move(renderSession), requests, title, successMessage, [captureService]() {
                // Remove the hidden capture taps + temp files (no-op when
                // no capture pass ran).
                if (captureService)
                    captureService->cleanupAfterRender();
            });
        progressWindow->launchThread();
    };

    // External FX / Instrument inserts can't render offline — run the
    // real-time capture pass first (#1623), then render. The pass plays
    // the export range once through the live engine while hidden taps
    // record each insert's return; during the render the same taps
    // substitute the recordings at the insert position.
    if (captureService == nullptr || !captureService->exportNeedsCapturePass()) {
        launchRender();
        return;
    }

    using PassError = InsertRenderCapture::PassError;
    auto* progressBox = new InsertCaptureProgressBox(*captureService);
    const bool started = captureService->startCapturePass(
        startSeconds, endSeconds, requests.front().sampleRate,
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
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           tr("export.alert.failed_title"),
                                                           tr("export.capture.error_failed"));
                } else {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                           tr("export.alert.cancelled_title"),
                                                           tr("export.alert.cancelled_body"));
                }
            }
        });
    if (started)
        return;

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
}  // namespace

void MainWindow::performExport(const ExportAudioDialog::Settings& settings, AudioEngine* engine,
                               BeatRange selection) {
    if (!engine || !engine->hasActiveEdit()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("dialogs.export_audio"),
                                               tr("dialogs.error.export_no_edit"));
        return;
    }

    launchAudioExport(settings, engine, selection);
}

void MainWindow::launchAudioExport(const ExportAudioDialog::Settings& settings, AudioEngine* engine,
                                   BeatRange selection) {
    // Determine file extension
    juce::String extension = exportExtensionFor(settings.format);

    // Build default output path from render preferences
    auto& config = Config::getInstance();
    juce::File defaultDir;
    auto renderFolder = config.getRenderFolder();
    if (!renderFolder.empty())
        defaultDir = juce::File(renderFolder);
    else
        defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    if (settings.stems) {
        // Stems go into a folder, named for their tracks.
        fileChooser_ =
            std::make_unique<juce::FileChooser>(tr("export.choose_stems_folder"), defaultDir);
    } else {
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

        fileChooser_ = std::make_unique<juce::FileChooser>(
            tr("dialogs.export_audio"), defaultDir.getChildFile(pattern + extension),
            "*" + extension, true);
    }

    const auto flags = settings.stems ? juce::FileBrowserComponent::openMode |
                                            juce::FileBrowserComponent::canSelectDirectories
                                      : juce::FileBrowserComponent::saveMode |
                                            juce::FileBrowserComponent::canSelectFiles |
                                            juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(flags, [this, settings, engine, extension,
                                      selection](const juce::FileChooser& chooser) {
        auto chosen = chooser.getResult();
        fileChooser_.reset();
        if (chosen == juce::File())
            return;

        // The render takes beats; only the capture pass takes seconds.
        BeatRange requestedRange{{0.0}, {engine->getEditLengthBeats().value}};
        using ExportRange = ExportAudioDialog::ExportRange;
        switch (settings.exportRange) {
            case ExportRange::TimeSelection:
                if (selection.isValid())
                    requestedRange = selection;
                break;

            case ExportRange::LoopRegion: {
                const auto loop = engine->getLoopRegionBeats();
                if (loop.isValid())
                    requestedRange = loop;
                break;
            }

            case ExportRange::EntireSong:
            default:
                break;
        }

        const auto* tempoMap = engine->tempoMap();
        if (tempoMap == nullptr) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   tr("dialogs.export_audio"),
                                                   tr("export.error.render_failed"));
            return;
        }
        const double requestedStart = tempoMap->beatToTime(requestedRange.start.value);
        const double requestedEnd = tempoMap->beatToTime(requestedRange.end.value);
        const bool resumePlaybackAfterRender = engine->isPlaying();

        auto base = exportRequest(settings, requestedRange);

        if (!settings.stems) {
            if (!chosen.hasFileExtension(extension))
                chosen = chosen.withFileExtension(extension);

            base.destination = chosen;
            // The chord track is monitor-only: exclude it from the bounce so its
            // notes never reach the master render.
            if (auto chordId = magda::TrackManager::getInstance().getChordTrackId();
                chordId != magda::INVALID_TRACK_ID)
                base.excludedTrackIds = {chordId};

            startAudioExport(engine, {base}, trEllipsis("export.progress.exporting_audio"),
                             tr("export.alert.audio_success_prefix") + "\n" +
                                 chosen.getFullPathName(),
                             requestedStart, requestedEnd, resumePlaybackAfterRender);
            return;
        }

        auto requests = stemRequests(base, settings, chosen, extension);
        if (requests.empty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   tr("dialogs.export_audio"),
                                                   tr("export.error.no_stems"));
            return;
        }

        const auto successMessage =
            tr("export.alert.stems_success_prefix")
                .replace("{0}", juce::String(static_cast<int>(requests.size()))) +
            "\n" + chosen.getFullPathName();
        auto start = [engine, requests, successMessage, requestedStart, requestedEnd,
                      resumePlaybackAfterRender]() {
            startAudioExport(engine, requests, trEllipsis("export.progress.exporting_stems"),
                             successMessage, requestedStart, requestedEnd,
                             resumePlaybackAfterRender);
        };

        const auto existing = std::ranges::count_if(
            requests, [](const OfflineRenderRequest& r) { return r.destination.existsAsFile(); });
        if (existing == 0) {
            start();
            return;
        }

        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon, tr("export.alert.replace_title"),
            tr("export.alert.replace_body").replace("{0}", juce::String(existing)),
            tr("export.alert.replace_button"), tr("dialogs.cancel"), nullptr,
            juce::ModalCallbackFunction::create([start](int result) {
                if (result != 0)
                    start();
            }));
    });
}

// ============================================================================
// Export MIDI Implementation
// ============================================================================

void MainWindow::performMidiExport(const ExportMidiDialog::Settings& settings,
                                   BeatRange selection) {
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
    // so seconds should only appear at external API boundaries.
    double rangeStartBeats = 0.0;
    double rangeEndBeats = 0.0;

    // Find the extent of all MIDI clips
    for (const auto& clip : clips) {
        if (clip.isMidi()) {
            double endBeats = timelineEndBeats(clip, projectTempo);
            rangeEndBeats = std::max(rangeEndBeats, endBeats);
        }
    }

    if (settings.exportRange == ExportMidiDialog::ExportRange::TimeSelection &&
        selection.isValid()) {
        rangeStartBeats = selection.start.value;
        rangeEndBeats = selection.end.value;
    } else if (settings.exportRange == ExportMidiDialog::ExportRange::LoopRegion) {
        auto* engine = mainComponent->getAudioEngine();
        if (engine && engine->hasActiveEdit()) {
            const auto loopRange = engine->getLoopRegionBeats();
            rangeStartBeats = loopRange.start.value;
            rangeEndBeats = loopRange.end.value;
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
        std::vector<MidiProgramChangeData> midiProgramChangeData;
    };
    struct TrackMidiData {
        juce::String trackName;
        std::vector<ClipMidiData> clips;
    };
    std::map<TrackId, TrackMidiData> trackData;

    for (const auto& clip : clips) {
        if (!clip.isMidi())
            continue;
        if (clip.midiNotes.empty() && clip.midiCCData.empty() && clip.midiPitchBendData.empty() &&
            clip.midiProgramChangeData.empty())
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
        td.clips.push_back({clipStartBeats, clip.midiNotes, clip.midiCCData, clip.midiPitchBendData,
                            clip.midiProgramChangeData});
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

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(daw::MidiFileWriter::ticksPerQuarter);

        // Tempo and time signature meta-events
        int microsecondsPerBeat = static_cast<int>(60000000.0 / projectTempo);
        auto tempoMsg = juce::MidiMessage::tempoMetaEvent(microsecondsPerBeat);
        tempoMsg.setTimeStamp(0.0);
        auto timeSigMsg = juce::MidiMessage::timeSignatureMetaEvent(timeSigNum, timeSigDen);
        timeSigMsg.setTimeStamp(0.0);

        const double rangeLengthBeats = capturedRangeEndBeats - capturedRangeStartBeats;

        auto addClipDataToSequence = [&](juce::MidiMessageSequence& seq, const ClipMidiData& clip,
                                         int channel, double& maxTick) {
            const double lastTick = daw::MidiFileWriter::appendClipInRange(
                seq, clip.midiNotes, clip.midiCCData, clip.midiPitchBendData,
                clip.startBeats - capturedRangeStartBeats, rangeLengthBeats, channel,
                clip.midiProgramChangeData);
            maxTick = std::max(maxTick, lastTick);
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
