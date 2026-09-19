#include "ClipManager.hpp"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>

#include "../project/ProjectManager.hpp"
#include "ClipOperations.hpp"
#include "CompSectionMath.hpp"
#include "Config.hpp"
#include "GridDivision.hpp"
#include "MidiFileWriter.hpp"
#include "RangesHelpers.hpp"
#include "TempoUtils.hpp"
#include "TimeStretchModes.hpp"
#include "TrackManager.hpp"
#include "UndoManager.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "audio/CompService.hpp"
#include "media_db/MediaDbContext.hpp"
#include "media_db/MediaDbIndexer.hpp"
#include "media_db/MediaDbMetadata.hpp"

namespace magda {

namespace {

double currentProjectTempoOrDefault() {
    double bpm = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    return isValidBpm(bpm) ? bpm : DEFAULT_BPM;
}

/// The file length is what ties a tempo to a beat count, so fill it from the
/// thumbnail when the source probe left it unknown.
void ensureSourceDurationKnown(const AudioEvent& event) {
    auto* source = SourcePool::getInstance().getMutable(event.sourceId);
    if (source == nullptr || source->durationSeconds > 0.0)
        return;

    const auto filePath = event.sourceFilePath();
    if (filePath.isEmpty() || !juce::File(filePath).existsAsFile())
        return;
    if (auto* thumbnail = AudioThumbnailManager::getInstance().getThumbnail(filePath))
        source->durationSeconds = juce::jmax(0.0, thumbnail->getTotalLength());
}

/// With no probe yet, the interpretation is the only account of the length.
void fillSourceDurationFromInterpretation(const AudioEvent& event) {
    auto* source = SourcePool::getInstance().getMutable(event.sourceId);
    if (source != nullptr && source->durationSeconds <= 0.0 && event.hasInterpretedBpm() &&
        event.interpTotalBeats > 0.0) {
        source->durationSeconds = event.interpTotalBeats * 60.0 / event.interpBpm;
    }
}

juce::File midiLibraryFileForClip(const ClipInfo& clip, const juce::File& midiDir) {
    const juce::File existing(clip.midi().sourceFilePath);
    if (existing != juce::File() && existing.getParentDirectory() == midiDir &&
        existing.hasFileExtension(".mid;.midi")) {
        return existing;
    }

    auto safeName = juce::File::createLegalFileName(clip.name);
    if (safeName.isEmpty()) {
        safeName = "midi_clip";
    }
    return midiDir.getNonexistentChildFile(safeName + "_" + juce::String(clip.id), ".mid");
}

juce::File externalEditFileForClip(const ClipInfo& clip, const juce::File& destDir,
                                   const juce::File& sourceFile) {
    auto safeName = juce::File::createLegalFileName(clip.name);
    if (safeName.isEmpty()) {
        safeName = sourceFile.getFileNameWithoutExtension();
    }
    if (safeName.isEmpty()) {
        safeName = "audio_clip";
    }
    return destDir.getNonexistentChildFile(safeName, sourceFile.getFileExtension(), false);
}

bool isLaunchableExternalAudioEditor(const juce::File& editor) {
#if JUCE_MAC
    if (editor.isBundle()) {
        return true;
    }
#endif
    return editor.existsAsFile();
}

bool launchExternalAudioEditor(const juce::File& editor, const juce::File& editFile) {
    juce::StringArray args;

#if JUCE_MAC
    if (editor.isBundle()) {
        args.add("/usr/bin/open");
        args.add("-n");
        args.add("-a");
        args.add(editor.getFullPathName());
        args.add(editFile.getFullPathName());
    } else
#endif
    {
        args.add(editor.getFullPathName());
        args.add(editFile.getFullPathName());
    }

    juce::ChildProcess process;
    return process.start(args, 0);
}

class ExternalEditPoller : private juce::Timer {
  public:
    static ExternalEditPoller& getInstance() {
        static ExternalEditPoller poller;
        return poller;
    }

    void watch(ClipId clipId, const juce::File& file) {
        if (!file.existsAsFile()) {
            return;
        }

        const auto& path = file.getFullPathName();
        const auto mtime = file.getLastModificationTime();
        for (auto& item : watched_) {
            if (item.path == path) {
                item.clipId = clipId;
                item.lastModified = mtime;
                return;
            }
        }

        watched_.push_back({clipId, path, mtime});
        startTimer(1000);
    }

  private:
    struct WatchedFile {
        ClipId clipId = INVALID_CLIP_ID;
        juce::String path;
        juce::Time lastModified;
    };

    void timerCallback() override {
        for (auto it = watched_.begin(); it != watched_.end();) {
            juce::File file(it->path);
            if (!file.existsAsFile()) {
                it = watched_.erase(it);
                continue;
            }

            const auto currentModified = file.getLastModificationTime();
            if (currentModified != it->lastModified) {
                it->lastModified = currentModified;
                AudioThumbnailManager::getInstance().invalidateFile(it->path);
                ClipManager::getInstance().forceNotifyClipPropertyChanged(it->clipId);
                ProjectManager::getInstance().markDirty();
            }
            ++it;
        }

        if (watched_.empty()) {
            stopTimer();
        }
    }

    std::vector<WatchedFile> watched_;
};

}  // namespace

ClipManager& ClipManager::getInstance() {
    static ClipManager instance;
    return instance;
}

ClipManager::ClipManager() {
    // A source's positions are sample counts at its own rate. Until the file is
    // probed that rate is only the nominal guess, so resolving it (or relinking
    // to a file at another rate) has to move the counts to keep the same time.
    SourcePool::getInstance().setRateChangeHandler(
        [this](SourceId sourceId, double oldRate, double newRate) {
            rescaleEventsForSourceRate(sourceId, oldRate, newRate);
        });
}

void ClipManager::stashClipboardSourcePaths() {
    clipboardSourcePaths_.clear();
    for (const auto& clip : clipboard_) {
        if (!clip.isAudio())
            continue;
        for (const auto& event : clip.audio().events) {
            if (event.sourceId == INVALID_SOURCE_ID)
                continue;
            const auto path = sourcePathOf(event.sourceId);
            if (path.isNotEmpty())
                clipboardSourcePaths_[event.sourceId] = path;
        }
    }
}

juce::String ClipManager::clipboardSourcePathFor(const AudioEvent& event) const {
    if (const auto it = clipboardSourcePaths_.find(event.sourceId);
        it != clipboardSourcePaths_.end()) {
        return it->second;
    }
    return event.sourceFilePath();
}

void ClipManager::repointEventsToSource(SourceId from, SourceId to) {
    if (from == to || from == INVALID_SOURCE_ID || to == INVALID_SOURCE_ID)
        return;

    // The two sources describe the same file but may have been probed at
    // different rates, so the sample counts have to move with the events.
    const double fromRate = sourceRateOf(from);
    const double toRate = sourceRateOf(to);

    for (auto& [clipId, clip] : clips_) {
        if (!clip.isAudio())
            continue;

        bool touched = false;
        for (auto& event : clip.audio().events) {
            if (event.sourceId != from)
                continue;

            event.rescaleSourcePositions(fromRate, toRate);
            event.sourceId = to;
            touched = true;
        }

        if (touched)
            notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::rescaleEventsForSourceRate(SourceId sourceId, double oldRate, double newRate) {
    if (sourceId == INVALID_SOURCE_ID || oldRate <= 0.0 || newRate <= 0.0)
        return;

    for (auto& [clipId, clip] : clips_) {
        if (!clip.isAudio())
            continue;

        bool touched = false;
        for (auto& event : clip.audio().events) {
            if (event.sourceId != sourceId)
                continue;

            event.rescaleSourcePositions(oldRate, newRate);
            touched = true;
        }

        if (touched)
            notifyClipPropertyChanged(clipId);
    }
}

// ============================================================================
// Clip Creation
// ============================================================================

double ClipManager::findNonOverlappingStartBeats(TrackId trackId, double desiredStartBeats,
                                                 double lengthBeats, ClipView view) const {
    if (view != ClipView::Arrangement || lengthBeats <= 0.0)
        return desiredStartBeats;

    struct Span {
        double start = 0.0;
        double end = 0.0;
    };

    std::vector<Span> spans;
    spans.reserve(clips_.size());
    for (const auto& [_, clip] : clips_) {
        if (clip.trackId != trackId || clip.view != ClipView::Arrangement)
            continue;

        spans.push_back(
            {clip.placement.startBeat, clip.placement.startBeat + clip.placement.lengthBeats});
    }

    std::sort(spans.begin(), spans.end(),
              [](const Span& a, const Span& b) { return a.start < b.start; });

    double candidateStart = desiredStartBeats;
    constexpr double epsilon = 1.0e-9;
    for (const auto& span : spans) {
        if (span.end <= candidateStart + epsilon)
            continue;

        if (span.start >= candidateStart + lengthBeats - epsilon)
            break;

        candidateStart = span.end;
    }

    return candidateStart;
}

ClipId ClipManager::createAudioClipBeats(TrackId trackId, double startBeats, double lengthBeats,
                                         const juce::String& audioFilePath, ClipView view,
                                         double projectBPM, ClipOverlapPolicy overlapPolicy) {
    if (overlapPolicy == ClipOverlapPolicy::PreserveExisting) {
        startBeats = findNonOverlappingStartBeats(trackId, startBeats, lengthBeats, view);
    }

    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.setAudioContent();
    clip.view = view;
    if (audioFilePath.isNotEmpty()) {
        clip.name = juce::File(audioFilePath).getFileNameWithoutExtension();
    } else {
        clip.name = generateClipName(ClipType::Audio);
    }
    if (Config::getInstance().getClipColourMode() == 0) {
        // Inherit from parent track
        const auto* track = TrackManager::getInstance().getTrack(trackId);
        clip.colour = track ? track->colour : juce::Colour(Config::getDefaultColour(0));
    } else {
        clip.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(clips_.size())));
    }
    // One event spanning the clip. The pooled Source carries the file facts;
    // the event carries how they are interpreted.
    AudioEvent event;
    event.sourceId = SourcePool::getInstance().acquire(audioFilePath);
    event.speedRatio = 1.0;
    event.seedInterpretationFromSource();
    auto& newEvent = clip.audio().addEvent(event);

    // New audio clips default to AUTO-XFADE (#1499): overlaps with other
    // auto-crossfade audio clips play as crossfades instead of trimming.
    clip.autoCrossfade = Config::getInstance().getAutoCrossfadeByDefault();

    // What a new clip starts with when something covers it (#2003). Per clip
    // from here on: the preference seeds it and never speaks for it again.
    clip.overlapPlaysBoth = Config::getInstance().getClipOverlapPlaysBoth();

    const double bpm = isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();

    clip.setPlacementBeats(startBeats, lengthBeats);
    clip.deriveTimesFromBeats(bpm);

    // Read from the top of the file, over the whole of it. Nothing has chosen
    // a range yet, so nothing may look like the user did.
    newEvent.loopStartSamples = 0;
    newEvent.setLoopExtent(RegionExtent::WholeSource);

    // A drop loads what the library holds for the file and nothing else. What
    // the user saved is the user's own; what the scan measured is analysis.
    std::optional<magda::media::EffectiveMetadata> savedMetadata;
    if (audioFilePath.isNotEmpty() && juce::File(audioFilePath).existsAsFile()) {
        savedMetadata = magda::media::getUserMetadataForFile(
            std::filesystem::path(audioFilePath.toStdString()));
        if (savedMetadata) {
            if (savedMetadata->bpm && isValidBpm(*savedMetadata->bpm)) {
                newEvent.adoptBpm(*savedMetadata->bpm, Provenance::User);
            } else if (savedMetadata->detectedBpm && isValidBpm(*savedMetadata->detectedBpm)) {
                newEvent.adoptBpm(*savedMetadata->detectedBpm, Provenance::Analysis);
            }
            if (savedMetadata->totalBeats && *savedMetadata->totalBeats > 0.0) {
                newEvent.adoptTotalBeats(*savedMetadata->totalBeats, Provenance::User);
            } else if (newEvent.hasInterpretedBpm() && newEvent.sourceDurationSeconds() > 0.0) {
                newEvent.adoptTotalBeats(
                    beatCountForDuration(newEvent.sourceDurationSeconds(), newEvent.interpBpm),
                    newEvent.bpmFrom);
            }
            if (savedMetadata->keyRoot && !savedMetadata->keyRoot->empty()) {
                newEvent.keyRoot = *savedMetadata->keyRoot;
            }
            if (savedMetadata->keyScale && !savedMetadata->keyScale->empty()) {
                newEvent.keyScale = *savedMetadata->keyScale;
            }
        }
        const auto savedMarkers = magda::media::getUserWarpMarkersForFile(
            std::filesystem::path(audioFilePath.toStdString()));
        if (savedMarkers) {
            newEvent.warpMarkers.clear();
            newEvent.warpMarkers.reserve(savedMarkers->size());
            for (const auto& marker : *savedMarkers) {
                newEvent.warpMarkers.push_back({marker.sourceSec, marker.beat});
            }
            newEvent.warpEnabled = true;
        }
    }

    if (view == ClipView::Session)
        clip.loopEnabled = true;
    clips_[clip.id] = clip;

    if (savedMetadata && savedMetadata->beatMode) {
        auto& savedClip = clips_[clip.id];
        auto* savedEvent = savedClip.primaryEvent();
        if (savedEvent != nullptr) {
            savedEvent->setPlaybackIntent(*savedMetadata->beatMode ? PlaybackIntent::Beat
                                                                   : PlaybackIntent::Free);
            if (savedEvent->autoTempo) {
                savedClip.loopEnabled = true;
                savedEvent->analogPitch = false;
                savedEvent->speedRatio = 1.0;
                if (savedEvent->interpTotalBeats > 0.0) {
                    // A beat-mode clip's natural length is its saved beat count,
                    // not the file duration the placement was derived from. The
                    // region follows the interpretation so a later correction refits it.
                    savedClip.setPlacementBeats(startBeats, savedEvent->interpTotalBeats);
                    if (savedEvent->interpBpm > 0.0)
                        savedEvent->setLoopExtent(RegionExtent::Interpretation);
                }
                savedClip.deriveTimesFromBeats(bpm);
            }
        }
    }

    addToSessionSlotIndex(clips_[clip.id]);
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::ResolveOverlaps)
        resolveOverlaps(clip.id);
    notifyClipsChanged();

    // A drop does nothing else. Detection runs when BEAT is pressed on a clip
    // with no tempo, and the library is written only by Save to library.
    return clip.id;
}

ClipId ClipManager::createAudioClip(TrackId trackId, double startTime, double length,
                                    const juce::String& audioFilePath, ClipView view,
                                    double projectBPM, ClipOverlapPolicy overlapPolicy) {
    const double bpm = isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();
    return createAudioClipBeats(trackId, startTime * bpm / 60.0, length * bpm / 60.0, audioFilePath,
                                view, bpm, overlapPolicy);
}

ClipId ClipManager::createRecordedAudioClip(TrackId trackId, RecordedAudioClipData recording,
                                            ClipOverlapPolicy overlapPolicy, ClipView view,
                                            int sceneIndex) {
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::PreserveExisting) {
        recording.startBeat = findNonOverlappingStartBeats(
            trackId, recording.startBeat, recording.lengthBeats, ClipView::Arrangement);
    }

    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.setAudioContent();
    clip.view = view;
    clip.sceneIndex = sceneIndex;
    clip.name = recording.filePath.isNotEmpty()
                    ? juce::File(recording.filePath).getFileNameWithoutExtension()
                    : generateClipName(ClipType::Audio);
    if (Config::getInstance().getClipColourMode() == 0) {
        const auto* track = TrackManager::getInstance().getTrack(trackId);
        clip.colour = track ? track->colour : juce::Colour(Config::getDefaultColour(0));
    } else {
        clip.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(clips_.size())));
    }

    clip.autoCrossfade = Config::getInstance().getAutoCrossfadeByDefault();
    clip.overlapPlaysBoth = Config::getInstance().getClipOverlapPlaysBoth();
    const auto projectBpm = currentProjectTempoOrDefault();
    clip.setPlacementBeats(recording.startBeat, recording.lengthBeats);
    clip.deriveTimesFromBeats(projectBpm);
    clip.audio() = std::move(recording.takeModel);

    AudioEvent event;
    event.sourceId = SourcePool::getInstance().acquire(recording.filePath);
    event.speedRatio = 1.0;
    event.seedInterpretationFromSource();
    auto& active = clip.audio().addEvent(event);
    clip.syncSingleEventToClipBounds();
    active.loopStartSamples = 0;
    active.setLoopExtent(RegionExtent::WholeSource);
    active.adoptBpm(projectBpm, Provenance::User);
    active.adoptTotalBeats(recording.lengthBeats, Provenance::User);
    if (view == ClipView::Session) {
        clip.loopEnabled = true;
        clip.loopLengthBeats = recording.lengthBeats;
        active.setPlaybackIntent(PlaybackIntent::Beat);
        active.setLoopLengthBeats(recording.lengthBeats);
    }

    const auto clipId = clip.id;
    clips_[clipId] = std::move(clip);
    addToSessionSlotIndex(clips_[clipId]);
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::ResolveOverlaps)
        resolveOverlaps(clipId);
    notifyClipsChanged();
    return clipId;
}

ClipId ClipManager::createMidiClipBeats(TrackId trackId, double startBeats, double lengthBeats,
                                        ClipView view, ClipOverlapPolicy overlapPolicy) {
    if (overlapPolicy == ClipOverlapPolicy::PreserveExisting) {
        startBeats = findNonOverlappingStartBeats(trackId, startBeats, lengthBeats, view);
    }

    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.setMidiContent();
    clip.view = view;
    // Occlusion applies to audio and MIDI alike, so the preference seeds both
    // (#2003). Per clip from here on.
    clip.overlapPlaysBoth = Config::getInstance().getClipOverlapPlaysBoth();
    clip.name = generateClipName(ClipType::MIDI);
    // Chord-track clips are chord progressions, not generic MIDI clips.
    if (const auto* nameTrack = TrackManager::getInstance().getTrack(trackId);
        nameTrack && nameTrack->type == TrackType::Chord) {
        int n = 1;
        for (const auto& [id, c] : clips_) {
            const auto* t = TrackManager::getInstance().getTrack(c.trackId);
            if (t && t->type == TrackType::Chord)
                n++;
        }
        clip.name = "Progression " + juce::String(n);
    }
    if (Config::getInstance().getClipColourMode() == 0) {
        const auto* track = TrackManager::getInstance().getTrack(trackId);
        clip.colour = track ? track->colour : juce::Colour(Config::getDefaultColour(0));
    } else {
        clip.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(clips_.size())));
    }

    clip.setPlacementBeats(startBeats, lengthBeats);

    // Derive seconds for display caches only — never round-tripped back into
    // beats. ClipSynchronizer reads clip->startBeats / lengthBeats directly
    // when positioning the TE clip, so the seconds stored here are advisory.
    double tempo = currentProjectTempoOrDefault();
    clip.deriveTimesFromBeats(tempo);

    if (view == ClipView::Arrangement) {
        clips_[clip.id] = clip;
    } else {
        // Session clips loop by default
        clip.loopEnabled = true;
        clip.loopLengthBeats = clip.placement.lengthBeats;
        clips_[clip.id] = clip;
    }

    addToSessionSlotIndex(clips_[clip.id]);
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::ResolveOverlaps)
        resolveOverlaps(clip.id);
    notifyClipsChanged();

    return clip.id;
}

ClipId ClipManager::createRecordedMidiClip(TrackId trackId, RecordedMidiClipData recording,
                                           ClipOverlapPolicy overlapPolicy, ClipView view,
                                           int sceneIndex) {
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::PreserveExisting) {
        recording.startBeat = findNonOverlappingStartBeats(
            trackId, recording.startBeat, recording.lengthBeats, ClipView::Arrangement);
    }

    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.setMidiContent();
    clip.view = view;
    clip.sceneIndex = sceneIndex;
    clip.overlapPlaysBoth = Config::getInstance().getClipOverlapPlaysBoth();
    clip.name = generateClipName(ClipType::MIDI);
    if (Config::getInstance().getClipColourMode() == 0) {
        const auto* track = TrackManager::getInstance().getTrack(trackId);
        clip.colour = track ? track->colour : juce::Colour(Config::getDefaultColour(0));
    } else {
        clip.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(clips_.size())));
    }

    clip.setPlacementBeats(recording.startBeat, recording.lengthBeats);
    clip.deriveTimesFromBeats(currentProjectTempoOrDefault());
    clip.midiNotes = std::move(recording.active.notes);
    clip.midiCCData = std::move(recording.active.cc);
    clip.midiPitchBendData = std::move(recording.active.pitchBend);
    clip.midi() = std::move(recording.takeModel);
    if (view == ClipView::Session) {
        clip.loopEnabled = true;
        clip.loopLengthBeats = clip.placement.lengthBeats;
    }

    const auto clipId = clip.id;
    clips_[clipId] = std::move(clip);
    addToSessionSlotIndex(clips_[clipId]);
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::ResolveOverlaps)
        resolveOverlaps(clipId);
    notifyClipsChanged();
    return clipId;
}

ClipId ClipManager::createCapturedSessionClip(const ClipInfo& source, double startBeat,
                                              double lengthBeats, double offsetBeats,
                                              ClipOverlapPolicy overlapPolicy, double sourceTempo) {
    if (!std::isfinite(startBeat) || !std::isfinite(lengthBeats) || !std::isfinite(offsetBeats) ||
        startBeat < 0.0 || lengthBeats <= 0.0 || offsetBeats < 0.0 ||
        source.trackId == INVALID_TRACK_ID) {
        return INVALID_CLIP_ID;
    }

    if (overlapPolicy == ClipOverlapPolicy::PreserveExisting) {
        startBeat = findNonOverlappingStartBeats(source.trackId, startBeat, lengthBeats,
                                                 ClipView::Arrangement);
    }

    const double bpm = isValidBpm(sourceTempo) ? sourceTempo : currentProjectTempoOrDefault();
    ClipInfo normalisedSource = source;
    normalisedSource.setPlacementBeats(0.0, source.sessionCycleBeats(bpm));

    ClipInfo captured = normalisedSource;
    captured.id = nextClipId_++;
    captured.view = ClipView::Arrangement;
    captured.linkGroupId = 0;
    captured.stackOrder = 0;
    captured.sceneIndex = -1;
    captured.launchMode = LaunchMode::Trigger;
    captured.launchQuantize = LaunchQuantize::OneBar;
    captured.followAction = FollowAction::None;
    captured.followActionDelayBeats = 0.0;
    captured.followActionLoopCount = 1;
    captured.sessionPlayheadPos = -1.0;

    ClipOperations::setBeatPlacement(captured, startBeat, lengthBeats, bpm);
    if (captured.isAudio()) {
        captured.audio() = normalisedSource.audio();
        for (auto& event : captured.audio().events)
            event.startBeat -= offsetBeats;

        const auto sourceWindow = captured.audio().envelopeWindow.value_or(
            ClipPlacement{0.0, normalisedSource.placement.lengthBeats});
        captured.audio().envelopeWindow =
            ClipPlacement{sourceWindow.startBeat - offsetBeats, sourceWindow.lengthBeats};
    } else {
        if (captured.loopEnabled && captured.loopLengthBeats <= 0.0) {
            captured.loopLengthBeats = normalisedSource.placement.lengthBeats;
            captured.loopStartBeats = 0.0;
        }

        if (captured.loopEnabled && captured.loopLengthBeats > 0.0) {
            captured.midiOffset =
                wrapPhase(captured.midiOffset + offsetBeats, captured.loopLengthBeats);
        } else {
            captured.midiTrimOffset += offsetBeats;
        }
    }

    const auto clipId = captured.id;
    clips_[clipId] = std::move(captured);
    indexClipGroup(clipId, 0);

    if (overlapPolicy == ClipOverlapPolicy::ResolveOverlaps)
        resolveOverlaps(clipId);
    notifyClipsChanged();
    return clipId;
}

ClipId ClipManager::createMidiClip(TrackId trackId, double startTime, double length, ClipView view,
                                   ClipOverlapPolicy overlapPolicy) {
    // Seconds → beats once, at the boundary, using project tempo. Then
    // delegate to the beats-authoritative path. Anything driven by musical
    // input (bars, beats from a parser, etc.) should call createMidiClipBeats
    // directly to avoid this seconds detour.
    double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (tempo <= 0.0)
        tempo = 120.0;
    double startBeats = (startTime * tempo) / 60.0;
    double lengthBeats = (length * tempo) / 60.0;
    return createMidiClipBeats(trackId, startBeats, lengthBeats, view, overlapPolicy);
}

void ClipManager::deleteClip(ClipId clipId) {
    auto it = clips_.find(clipId);
    if (it == clips_.end())
        return;

    if (selectedClipId_ == clipId) {
        selectedClipId_ = INVALID_CLIP_ID;
        notifyClipSelectionChanged(INVALID_CLIP_ID);
    }
    if (lastTriggeredSessionClipId_ == clipId) {
        lastTriggeredSessionClipId_ = INVALID_CLIP_ID;
    }

    removeFromSessionSlotIndex(it->second);
    indexClipGroup(clipId, 0);
    clips_.erase(it);
    notifyClipsChanged();
}

void ClipManager::restoreClip(const ClipInfo& clipInfo) {
    if (clips_.count(clipInfo.id))
        return;

    clips_[clipInfo.id] = clipInfo;
    addToSessionSlotIndex(clips_[clipInfo.id]);
    indexClipGroup(clipInfo.id, clipInfo.linkGroupId);

    // Ensure nextClipId_ is beyond any restored clip IDs
    if (clipInfo.id >= nextClipId_) {
        nextClipId_ = clipInfo.id + 1;
    }
    // Same for the stacking counter, so a clip placed after a project loads
    // lands on top of everything the project already had.
    if (clipInfo.stackOrder >= nextStackOrder_) {
        nextStackOrder_ = clipInfo.stackOrder + 1;
    }
    // Same for link groups (project load and delete-undo both land here).
    if (clipInfo.linkGroupId >= nextLinkGroupId_) {
        nextLinkGroupId_ = clipInfo.linkGroupId + 1;
    }

    // A restored group member's snapshot may be stale: delete ghost B, edit
    // sibling A, undo the delete — B would rejoin with pre-delete content and
    // the next edit would silently clobber one side. The live group is
    // authoritative, so the restored clip adopts its current shared content.
    // (Project load is unaffected: members are saved consistent, so the copy
    // is a no-op there.)
    if (clipInfo.linkGroupId != 0) {
        const auto siblings = getLinkGroupSiblings(clipInfo.id);
        if (!siblings.empty()) {
            if (const auto* source = getClip(siblings.front()))
                clips_[clipInfo.id].copySharedContentFrom(*source);
        }
    }

    notifyClipsChanged();
}

void ClipManager::forceNotifyClipsChanged() {
    notifyClipsChanged();
}

namespace {
// Undoable snapshot of a clip's full state, used for take/comp operations
// (select take, comp section, clear comp, delete take). The state is already
// `after` when constructed, so the first execute() (from executeCommand) is a
// no-op; undo restores `before`, redo re-applies `after`.
class ClipStateSnapshotCommand : public UndoableCommand {
  public:
    ClipStateSnapshotCommand(juce::String desc, ClipInfo before, ClipInfo after)
        : desc_(std::move(desc)), before_(std::move(before)), after_(std::move(after)) {}

    void execute() override {
        if (skipFirst_) {
            skipFirst_ = false;
            return;
        }
        ClipManager::getInstance().replaceClipState(after_);
    }
    void undo() override {
        ClipManager::getInstance().replaceClipState(before_);
    }
    juce::String getDescription() const override {
        return desc_;
    }

  private:
    juce::String desc_;
    ClipInfo before_;
    ClipInfo after_;
    bool skipFirst_ = true;
};

// Capture the post-mutation state and push an undoable snapshot (before -> after).
void pushClipStateSnapshot(const juce::String& desc, const ClipInfo& before) {
    auto* after = ClipManager::getInstance().getClip(before.id);
    if (after == nullptr)
        return;
    UndoManager::getInstance().executeCommand(
        std::make_unique<ClipStateSnapshotCommand>(desc, before, *after));
}
}  // namespace

void ClipManager::pushClipTakeUndo(const juce::String& desc, const ClipInfo& before) {
    pushClipStateSnapshot(desc, before);
}

void ClipManager::replaceClipState(const ClipInfo& clipInfo) {
    auto it = clips_.find(clipInfo.id);
    if (it == clips_.end())
        return;
    it->second = clipInfo;
    // Audio comps carry a render that the snapshot's source.filePath may not
    // reflect (it is regenerated async); regenerate it from the restored sections.
    if (it->second.isAudio() && it->second.audio().compActive && !it->second.audio().comp.empty())
        CompService::getInstance().renderComp(clipInfo.id);
    // Listeners (AudioBridge -> ClipSynchronizer) re-push notes/source to TE.
    forceNotifyClipPropertyChanged(clipInfo.id);
}

void ClipManager::forceNotifyClipPropertyChanged(ClipId clipId) {
    notifyClipPropertyChanged(clipId);
}

void ClipManager::setMidiClipCurrentTake(ClipId clipId, int takeIndex) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || !clip->isMidi())
        return;
    if (takeIndex < 0 || takeIndex >= static_cast<int>(clip->midi().takes.size()))
        return;
    // Picking a take exits any active comp and fronts that take.
    auto& midi = clip->midi();
    if (takeIndex == midi.currentTakeIndex && !midi.compActive)
        return;  // genuine no-op
    ClipInfo before = *clip;
    midi.compActive = false;
    midi.comp.clear();
    clip->frontMidiTake(takeIndex);
    // Listeners (AudioBridge -> ClipSynchronizer) re-push the new notes to TE.
    forceNotifyClipPropertyChanged(clipId);
    pushClipStateSnapshot("Select Take", before);
}

void ClipManager::setAudioClipCurrentTake(ClipId clipId, int takeIndex) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || !clip->isAudio())
        return;
    auto& a = clip->audio();
    if (takeIndex < 0 || takeIndex >= static_cast<int>(a.takes.size()))
        return;
    if (takeIndex == a.currentTakeIndex && !a.compActive)
        return;
    ClipInfo before = *clip;
    a.compActive = false;
    a.comp.clear();
    a.currentTakeIndex = takeIndex;
    if (auto* event = a.primaryEvent()) {
        // Same discipline as repointEventsToSource: the positions are sample
        // counts at the old take's rate, and an imported or comp-rendered take
        // need not share it.
        const double oldRate = sourceRateOf(event->sourceId);
        event->sourceId =
            SourcePool::getInstance().acquire(a.takes[static_cast<size_t>(takeIndex)].filePath);
        event->rescaleSourcePositions(oldRate, sourceRateOf(event->sourceId));
    }
    forceNotifyClipPropertyChanged(clipId);
    pushClipStateSnapshot("Select Take", before);
}

namespace {
// Append the events of one take that land in a comp section [start, end).
template <std::ranges::input_range R, class Beat>
void appendEventsInSection(const R& events, Beat beatOf, const MidiCompSection& section,
                           std::vector<std::ranges::range_value_t<R>>& out) {
    const auto insideSection = [&](const auto& event) {
        const double beat = std::invoke(beatOf, event);
        return beat >= section.startBeat && beat < section.endBeat;
    };
    std::ranges::copy(events | std::views::filter(insideSection), std::back_inserter(out));
}

// Assemble a clip's active event vectors from its comp sections + take note
// sets: each section [startBeat, endBeat) contributes the events of its take
// whose start beat falls in that range.
void rebuildMidiComp(ClipInfo& clip) {
    auto& midi = clip.midi();
    std::vector<MidiNote> notes;
    std::vector<MidiCCData> cc;
    std::vector<MidiPitchBendData> pb;

    const int numTakes = static_cast<int>(midi.takes.size());
    const auto namesATake = [numTakes](const MidiCompSection& section) {
        return section.takeIndex >= 0 && section.takeIndex < numTakes;
    };

    for (const auto& section : midi.comp | std::views::filter(namesATake)) {
        const auto& take = midi.takes[static_cast<size_t>(section.takeIndex)];
        appendEventsInSection(take.notes, &MidiNote::startBeat, section, notes);
        appendEventsInSection(take.cc, &MidiCCData::beatPosition, section, cc);
        appendEventsInSection(take.pitchBend, &MidiPitchBendData::beatPosition, section, pb);
    }

    clip.midiNotes = std::move(notes);
    clip.midiCCData = std::move(cc);
    clip.midiPitchBendData = std::move(pb);
}

// Persist edits to the active take: mirror the clip's live event vectors back
// into takes[currentTakeIndex] so per-take edits survive switching takes. Skips
// comping (the active content is an assembled composite, not one take).
void syncActiveMidiTake(ClipInfo& clip) {
    if (!clip.isMidi())
        return;
    auto& m = clip.midi();
    if (m.compActive || m.takes.empty())
        return;
    const int idx = m.currentTakeIndex;
    if (idx < 0 || idx >= static_cast<int>(m.takes.size()))
        return;
    auto& take = m.takes[static_cast<size_t>(idx)];
    take.notes = clip.midiNotes;
    take.cc = clip.midiCCData;
    take.pitchBend = clip.midiPitchBendData;
}
}  // namespace

void ClipManager::setMidiCompSection(ClipId clipId, double startBeat, double endBeat,
                                     int takeIndex) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || !clip->isMidi())
        return;
    auto& midi = clip->midi();
    if (midi.takes.size() < 2)
        return;
    if (takeIndex < 0 || takeIndex >= static_cast<int>(midi.takes.size()))
        return;

    // Comp length is the loop length the passes share (clip content length).
    const double compLen = clip->placement.lengthBeats;
    if (compLen <= 0.0)
        return;

    ClipInfo before = *clip;
    const auto asSpan = [](const MidiCompSection& section) {
        return CompSpan{section.startBeat, section.endBeat, section.takeIndex};
    };
    const auto asSection = [](const CompSpan& span) {
        return MidiCompSection{span.start, span.end, span.takeIndex};
    };

    const auto spans = midi.comp | std::views::transform(asSpan) | toStd<std::vector<CompSpan>>();
    const auto out =
        assignCompSections(spans, compLen, midi.currentTakeIndex, startBeat, endBeat, takeIndex);

    midi.comp = out | std::views::transform(asSection) | toStd<std::vector<MidiCompSection>>();
    midi.compActive = true;

    rebuildMidiComp(*clip);
    forceNotifyClipPropertyChanged(clipId);
    pushClipStateSnapshot("Comp Section", before);
}

void ClipManager::clearMidiComp(ClipId clipId) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || !clip->isMidi())
        return;
    auto& midi = clip->midi();
    if (!midi.compActive && midi.comp.empty())
        return;
    ClipInfo before = *clip;
    midi.comp.clear();
    midi.compActive = false;
    clip->frontMidiTake(midi.currentTakeIndex);
    forceNotifyClipPropertyChanged(clipId);
    pushClipStateSnapshot("Clear Comp", before);
}

namespace {
// Adjust the active take index after take `deleted` is removed from `count`
// takes (post-erase size).
int activeAfterDelete(int current, int deleted, int newSize) {
    int next = current;
    if (current == deleted)
        next = std::min(deleted, newSize - 1);
    else if (current > deleted)
        next = current - 1;
    return std::clamp(next, 0, std::max(0, newSize - 1));
}
}  // namespace

void ClipManager::deleteClipTake(ClipId clipId, int takeIndex) {
    auto* clip = getClip(clipId);
    if (clip == nullptr)
        return;

    if (clip->isAudio()) {
        auto& a = clip->audio();
        const int n = static_cast<int>(a.takes.size());
        if (n <= 1 || takeIndex < 0 || takeIndex >= n)
            return;
        ClipInfo before = *clip;
        a.takes.erase(a.takes.begin() + takeIndex);
        const int newSize = static_cast<int>(a.takes.size());
        const int newCurrent = activeAfterDelete(a.currentTakeIndex, takeIndex, newSize);

        std::vector<CompSpan> spans;
        spans.reserve(a.comp.size());
        for (const auto& s : a.comp)
            spans.push_back({s.startSeconds, s.endSeconds, s.takeIndex});
        remapCompSpansAfterDelete(spans, takeIndex, newCurrent);
        a.comp.clear();
        for (const auto& s : spans)
            a.comp.push_back({s.start, s.end, s.takeIndex});

        a.currentTakeIndex = newCurrent;
        if (newSize < 2) {
            a.comp.clear();
            a.compActive = false;
        }
        if (a.compActive && !a.comp.empty()) {
            CompService::getInstance().renderComp(clipId);  // re-renders + notifies
        } else {
            if (newCurrent < newSize) {
                if (auto* event = a.primaryEvent()) {
                    const double oldRate = sourceRateOf(event->sourceId);
                    event->sourceId = SourcePool::getInstance().acquire(
                        a.takes[static_cast<size_t>(newCurrent)].filePath);
                    event->rescaleSourcePositions(oldRate, sourceRateOf(event->sourceId));
                }
            }
            forceNotifyClipPropertyChanged(clipId);
        }
        pushClipStateSnapshot("Delete Take", before);
        return;
    }

    if (clip->isMidi()) {
        auto& m = clip->midi();
        const int n = static_cast<int>(m.takes.size());
        if (n <= 1 || takeIndex < 0 || takeIndex >= n)
            return;
        ClipInfo before = *clip;
        m.takes.erase(m.takes.begin() + takeIndex);
        const int newSize = static_cast<int>(m.takes.size());
        const int newCurrent = activeAfterDelete(m.currentTakeIndex, takeIndex, newSize);

        std::vector<CompSpan> spans;
        spans.reserve(m.comp.size());
        for (const auto& s : m.comp)
            spans.push_back({s.startBeat, s.endBeat, s.takeIndex});
        remapCompSpansAfterDelete(spans, takeIndex, newCurrent);
        m.comp.clear();
        for (const auto& s : spans)
            m.comp.push_back({s.start, s.end, s.takeIndex});

        m.currentTakeIndex = newCurrent;
        if (newSize < 2) {
            m.comp.clear();
            m.compActive = false;
        }
        if (m.compActive && !m.comp.empty())
            rebuildMidiComp(*clip);
        else
            clip->frontMidiTake(newCurrent);
        forceNotifyClipPropertyChanged(clipId);
        pushClipStateSnapshot("Delete Take", before);
    }
}

void ClipManager::forceNotifyMultipleClipPropertiesChanged(const std::vector<ClipId>& clipIds) {
    if (clipIds.empty())
        return;
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipPropertiesChanged(clipIds);
        }
    }
}

bool ClipManager::editAudioClipSourceInExternalEditor(ClipId clipId, juce::String& errorMessage) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || !clip->isAudio()) {
        errorMessage = "Select an audio clip first.";
        return false;
    }

    const auto editorPath = juce::String(Config::getInstance().getExternalAudioEditorPath());
    if (editorPath.isEmpty()) {
        errorMessage = "Choose an external audio editor in Preferences > Media Library first.";
        return false;
    }

    juce::File editor(editorPath);
    if (!editor.exists()) {
        errorMessage = "The configured external audio editor could not be found.";
        return false;
    }
    if (!isLaunchableExternalAudioEditor(editor)) {
        errorMessage = "Choose the editor application or executable, not its containing folder.";
        return false;
    }

    auto* event = clip->primaryEvent();
    if (event == nullptr) {
        errorMessage = "The clip has no audio to edit.";
        return false;
    }

    const juce::File sourceFile(event->sourceFilePath());
    if (!sourceFile.existsAsFile()) {
        errorMessage = "The clip source file could not be found.";
        return false;
    }

    // The edit copy is audio that came from outside this timeline, so it lands
    // beside the collected media rather than in a root of its own (#2170).
    auto importedDir = ProjectManager::getInstance().getImportedDirectory();
    if (importedDir == juce::File() || !importedDir.createDirectory()) {
        errorMessage = "Could not create the project imported media folder.";
        return false;
    }

    const auto editFile = externalEditFileForClip(*clip, importedDir, sourceFile);
    if (!sourceFile.copyFileTo(editFile) || !editFile.existsAsFile()) {
        errorMessage = "Could not copy the clip source into the project imported media folder.";
        return false;
    }

    if (!launchExternalAudioEditor(editor, editFile)) {
        editFile.deleteFile();
        errorMessage = "Could not launch the configured external audio editor.";
        return false;
    }

    const auto oldPath = event->sourceFilePath();
    // The edit is a separate file, so it gets its own pooled source rather than
    // relinking the shared one: other clips still reference the original.
    event->sourceId = SourcePool::getInstance().acquire(editFile.getFullPathName());
    AudioThumbnailManager::getInstance().invalidateFile(oldPath);
    AudioThumbnailManager::getInstance().invalidateFile(event->sourceFilePath());
    notifyClipPropertyChanged(clipId);
    ProjectManager::getInstance().markDirty();
    ExternalEditPoller::getInstance().watch(clipId, editFile);
    return true;
}

ClipId ClipManager::duplicateClip(ClipId clipId) {
    const auto* original = getClip(clipId);
    if (!original) {
        return INVALID_CLIP_ID;
    }

    ClipInfo newClip = *original;
    newClip.id = nextClipId_++;
    // Link-group members share their name (the UI adds a per-instance #index),
    // so only unlinked duplicates get the " Copy" suffix.
    newClip.name = original->linkGroupId != 0 ? original->name : original->name + " Copy";

    if (newClip.view == ClipView::Arrangement) {
        // Beats are authoritative for clip positioning: place the duplicate one
        // clip-length after the original, fully in beats. setBeatPlacement
        // derives the seconds cache at the boundary. (The old audio path left the
        // duplicate's startBeats equal to the original's, so a later beats-driven
        // re-derivation snapped it back on top of the original.)
        const double bpm = currentProjectTempoOrDefault();
        const double clipLengthBeats = original->placement.lengthBeats;
        ClipOperations::setBeatPlacement(newClip, original->placement.startBeat + clipLengthBeats,
                                         clipLengthBeats, bpm);
    } else {
        // Session clips always loop
        const double bpm = currentProjectTempoOrDefault();
        ClipOperations::setBeatPlacement(newClip, 0.0, newClip.placement.lengthBeats, bpm);
        newClip.loopEnabled = true;
        newClip.sceneIndex = -1;
    }
    clips_[newClip.id] = newClip;
    addToSessionSlotIndex(clips_[newClip.id]);
    indexClipGroup(newClip.id, newClip.linkGroupId);

    if (newClip.view == ClipView::Arrangement)
        resolveOverlaps(newClip.id);
    notifyClipsChanged();

    return newClip.id;
}

ClipId ClipManager::duplicateClipAtBeats(ClipId clipId, double startBeat, TrackId trackId,
                                         double tempo) {
    const auto* original = getClip(clipId);
    if (!original) {
        return INVALID_CLIP_ID;
    }

    ClipInfo newClip = *original;
    newClip.id = nextClipId_++;
    newClip.name = original->linkGroupId != 0 ? original->name : original->name + " Copy";

    // Use specified track or keep same track
    if (trackId != INVALID_TRACK_ID) {
        newClip.trackId = trackId;
    }

    if (newClip.view == ClipView::Arrangement) {
        const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
        ClipOperations::setBeatPlacement(newClip, startBeat, newClip.placement.lengthBeats, bpm);
        clips_[newClip.id] = newClip;
    } else {
        // Session clips always loop
        const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
        ClipOperations::setBeatPlacement(newClip, 0.0, newClip.placement.lengthBeats, bpm);
        newClip.loopEnabled = true;
        newClip.sceneIndex = -1;
        clips_[newClip.id] = newClip;
    }
    addToSessionSlotIndex(clips_[newClip.id]);
    indexClipGroup(newClip.id, newClip.linkGroupId);

    if (newClip.view == ClipView::Arrangement)
        resolveOverlaps(newClip.id);
    notifyClipsChanged();

    return newClip.id;
}

ClipId ClipManager::duplicateClipAt(ClipId clipId, double startTime, TrackId trackId,
                                    double tempo) {
    const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
    return duplicateClipAtBeats(clipId, startTime * bpm / 60.0, trackId, bpm);
}

// ============================================================================
// Ghost clips (link groups)
// ============================================================================

int ClipManager::ensureLinkGroup(ClipInfo& clip) {
    if (clip.linkGroupId == 0) {
        clip.linkGroupId = nextLinkGroupId_++;
        indexClipGroup(clip.id, clip.linkGroupId);
    }
    return clip.linkGroupId;
}

ClipId ClipManager::duplicateClipAsGhost(ClipId clipId) {
    auto* original = getClip(clipId);
    if (original == nullptr)
        return INVALID_CLIP_ID;
    ensureLinkGroup(*original);
    // duplicateClip is a full struct copy, so the copy inherits linkGroupId
    // (and, being grouped, keeps the shared name instead of " Copy").
    return duplicateClip(clipId);
}

ClipId ClipManager::duplicateClipAsGhostAtBeats(ClipId clipId, double startBeat, TrackId trackId,
                                                double tempo) {
    auto* original = getClip(clipId);
    if (original == nullptr)
        return INVALID_CLIP_ID;
    ensureLinkGroup(*original);
    return duplicateClipAtBeats(clipId, startBeat, trackId, tempo);
}

void ClipManager::makeClipUnique(ClipId clipId) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || clip->linkGroupId == 0)
        return;
    const auto siblings = getLinkGroupSiblings(clipId);
    clip->linkGroupId = 0;
    notifyClipPropertyChanged(clipId);
    // If exactly one member remains, its group is now inert: repaint it so the
    // ghost visuals disappear.
    if (siblings.size() == 1)
        notifyClipPropertyChanged(siblings.front());
}

std::vector<ClipId> ClipManager::getLinkGroupSiblings(ClipId clipId) const {
    auto it = clips_.find(clipId);
    if (it == clips_.end() || it->second.linkGroupId == 0)
        return {};
    auto groupIt = linkGroupMembers_.find(it->second.linkGroupId);
    if (groupIt == linkGroupMembers_.end())
        return {};
    std::vector<ClipId> siblings;
    siblings.reserve(groupIt->second.size());
    for (ClipId id : groupIt->second)
        if (id != clipId)
            siblings.push_back(id);
    return siblings;
}

bool ClipManager::isGhostClip(ClipId clipId) const {
    auto it = clips_.find(clipId);
    if (it == clips_.end() || it->second.linkGroupId == 0)
        return false;
    auto groupIt = linkGroupMembers_.find(it->second.linkGroupId);
    return groupIt != linkGroupMembers_.end() && groupIt->second.size() > 1;
}

int ClipManager::getLinkGroupIndex(ClipId clipId) const {
    auto it = clips_.find(clipId);
    if (it == clips_.end() || it->second.linkGroupId == 0)
        return 0;
    auto groupIt = linkGroupMembers_.find(it->second.linkGroupId);
    if (groupIt == linkGroupMembers_.end() || groupIt->second.size() < 2)
        return 0;
    const auto& members = groupIt->second;  // sorted by ClipId = creation order
    const auto pos = std::lower_bound(members.begin(), members.end(), clipId);
    if (pos == members.end() || *pos != clipId)
        return 0;
    return static_cast<int>(pos - members.begin()) + 1;
}

std::vector<ClipId> ClipManager::propagateLinkGroupContent(ClipId clipId) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || clip->linkGroupId == 0)
        return {};
    auto groupIt = linkGroupMembers_.find(clip->linkGroupId);
    if (groupIt == linkGroupMembers_.end())
        return {};
    std::vector<ClipId> siblings;
    for (ClipId id : groupIt->second) {
        if (id == clipId)
            continue;
        auto it = clips_.find(id);
        if (it == clips_.end())
            continue;
        // Members are kept in lockstep, so ONE comparison decides whether
        // this notification touched shared content at all: per-instance
        // edits (colour, mix, loop window, per-tick drags) skip the deep
        // copies and the sibling notifications entirely.
        if (siblings.empty() && it->second.sharedContentEquals(*clip))
            return {};
        it->second.copySharedContentFrom(*clip);
        siblings.push_back(id);
    }
    return siblings;
}

void ClipManager::indexClipGroup(ClipId clipId, int groupId) {
    const auto recorded = indexedGroupOf_.find(clipId);
    const int oldGroup = recorded != indexedGroupOf_.end() ? recorded->second : 0;
    if (oldGroup == groupId)
        return;
    if (oldGroup != 0) {
        auto bucketIt = linkGroupMembers_.find(oldGroup);
        if (bucketIt != linkGroupMembers_.end()) {
            auto& members = bucketIt->second;
            members.erase(std::remove(members.begin(), members.end(), clipId), members.end());
            if (members.empty())
                linkGroupMembers_.erase(bucketIt);
        }
    }
    if (groupId != 0) {
        auto& members = linkGroupMembers_[groupId];
        members.insert(std::upper_bound(members.begin(), members.end(), clipId), clipId);
        indexedGroupOf_[clipId] = groupId;
    } else {
        indexedGroupOf_.erase(clipId);
    }
}

void ClipManager::rebuildLinkGroupIndex() {
    linkGroupMembers_.clear();
    indexedGroupOf_.clear();
    for (const auto& [id, clip] : clips_) {
        if (clip.linkGroupId != 0) {
            linkGroupMembers_[clip.linkGroupId].push_back(id);
            indexedGroupOf_[id] = clip.linkGroupId;
        }
    }
    for (auto& [groupId, members] : linkGroupMembers_)
        std::sort(members.begin(), members.end());
}

void ClipManager::resetLoopedClipLength(ClipInfo& clip) {
    if (!clip.loopEnabled)
        return;

    const double bpm = currentProjectTempoOrDefault();
    const double loopBeats = clip.loopLengthBeats;
    const auto* event = clip.primaryEvent();

    if (loopBeats > 0.0) {
        ClipOperations::setBeatPlacement(clip, clip.placement.startBeat, loopBeats, bpm);
    } else if (event != nullptr) {
        // No usable beat view (interpretation BPM unknown): fall back to the
        // region's timeline extent, the whole file for a whole-source region.
        const double regionSeconds = event->loopLengthSamples > 0 ? event->loopLengthSeconds()
                                                                  : event->sourceDurationSeconds();
        if (regionSeconds > 0.0)
            ClipOperations::setTimelinePlacement(clip, clip.getTimelineStart(bpm),
                                                 event->sourceToTimeline(regionSeconds), bpm);
    }
    clip.loopEnabled = false;
}

// ============================================================================
// Clip Manipulation
// ============================================================================

void ClipManager::moveClipBeats(ClipId clipId, double newStartBeat, double tempo) {
    if (auto* clip = getClip(clipId)) {
        double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
        ClipOperations::moveContainerBeats(*clip, newStartBeat, bpm);
        // Notes maintain their relative position within the clip (startBeat unchanged)
        // so they move with the clip on the timeline
        if (clip->view == ClipView::Arrangement)
            resolveOverlaps(clipId);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::moveClip(ClipId clipId, double newStartTime, double tempo) {
    const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
    moveClipBeats(clipId, newStartTime * bpm / 60.0, bpm);
}

void ClipManager::moveClipToTrack(ClipId clipId, TrackId newTrackId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->trackId != newTrackId) {
            removeFromSessionSlotIndex(*clip);
            clip->trackId = newTrackId;
            addToSessionSlotIndex(*clip);
            if (clip->view == ClipView::Arrangement)
                resolveOverlaps(clipId);
            notifyClipsChanged();  // Track assignment change affects layout
        }
    }
}

void ClipManager::resizeClipBeats(ClipId clipId, double newLengthBeats, bool fromStart,
                                  double tempo) {
    if (auto* clip = getClip(clipId)) {
        const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
        const double newLength = newLengthBeats * 60.0 / bpm;
        auto* event = clip->primaryEvent();
        if (fromStart) {
            ClipOperations::resizeContainerFromLeft(*clip, newLength, bpm);
            // Non-loop mode: keep the region anchored at the read position
            if (event != nullptr && !clip->loopEnabled)
                event->loopStartSamples = event->sourceAnchorSamples;
        } else {
            ClipOperations::resizeContainerFromRight(*clip, newLength, bpm);

            // In non-loop mode the clip length defines the source region
            if (event != nullptr && !clip->loopEnabled) {
                event->setLoopLengthSeconds(event->timelineToSource(clip->getTimelineLength(bpm)));
            }
        }
        if (clip->view == ClipView::Arrangement)
            resolveOverlaps(clipId);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::resizeClip(ClipId clipId, double newLength, bool fromStart, double tempo) {
    const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
    resizeClipBeats(clipId, newLength * bpm / 60.0, fromStart, bpm);
}

ClipId ClipManager::splitClipAtBeat(ClipId clipId, double splitBeat, double tempo) {
    auto* clip = getClip(clipId);
    if (!clip) {
        return INVALID_CLIP_ID;
    }

    const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
    const double splitTime = splitBeat * 60.0 / bpm;
    const double clipStart = clip->getTimelineStart(bpm);
    const double clipEnd = clip->getTimelineEnd(bpm);

    // Validate split position is within clip
    if (splitTime <= clipStart || splitTime >= clipEnd) {
        return INVALID_CLIP_ID;
    }

    // Calculate lengths
    double leftLength = splitTime - clipStart;
    double rightLength = clipEnd - splitTime;
    const double leftLengthBeats = leftLength * bpm / 60.0;
    const double rightLengthBeats = rightLength * bpm / 60.0;

    // Create right half as new clip
    ClipInfo rightClip = *clip;
    rightClip.id = nextClipId_++;
    rightClip.name = clip->name + " R";
    ClipOperations::setBeatPlacement(rightClip, clip->placement.startBeat + leftLengthBeats,
                                     rightLengthBeats, bpm);

    // Advance the right half's read position: it starts leftLength further into
    // the source. In beat/warp mode speedRatio is 1.0 and the real stretch is
    // projectBPM / interpBpm, so the delta converts through the beat domain.
    auto* rightEvent = rightClip.primaryEvent();
    if (rightEvent != nullptr) {
        const bool useSourceBeatProcessing = rightEvent->autoTempo || rightEvent->warpEnabled;
        const double sourceDelta = (useSourceBeatProcessing && rightEvent->interpBpm > 0.0)
                                       ? leftLengthBeats * 60.0 / rightEvent->interpBpm
                                       : leftLength * rightEvent->speedRatio;
        rightEvent->setAnchorSeconds(rightEvent->anchorSeconds() + sourceDelta);
    }

    // Handle MIDI clip splitting
    if (rightClip.isMidi() && !rightClip.midiNotes.empty()) {
        if (clip->loopEnabled && clip->loopLengthBeats > 0.0) {
            // Looped MIDI: both halves keep the same notes.
            // If the split falls mid-loop, adjust the right clip's midiOffset
            // so it starts playing from the correct phase within the loop.
            // If the split lands on a loop boundary, midiOffset stays unchanged.
            double splitBeat = leftLengthBeats;
            double loopLen = clip->loopLengthBeats;
            double phase = std::fmod(splitBeat, loopLen);
            // Treat near-zero and near-loopLen as boundary (floating-point tolerance)
            constexpr double kEpsilon = 0.0001;
            bool onBoundary = phase < kEpsilon || (loopLen - phase) < kEpsilon;
            if (!onBoundary) {
                rightClip.midiOffset = std::fmod(clip->midiOffset + phase, loopLen);
            }
        } else {
            // Non-looped MIDI: partition notes by split position
            double splitBeat = leftLengthBeats;

            std::vector<MidiNote> leftNotes;
            std::vector<MidiNote> rightNotes;

            for (const auto& note : clip->midiNotes) {
                if (note.startBeat < splitBeat) {
                    leftNotes.push_back(note);
                } else {
                    MidiNote adjustedNote = note;
                    adjustedNote.startBeat -= splitBeat;
                    rightNotes.push_back(adjustedNote);
                }
            }

            clip->midiNotes = std::move(leftNotes);
            rightClip.midiNotes = std::move(rightNotes);

            // Partitioning rewrites shared content, so ghost halves cannot
            // stay in their link group: the next propagation would truncate
            // every sibling. Looped-MIDI and audio splits are pure windowing
            // (midiOffset / offset) and keep their group.
            clip->linkGroupId = 0;
            rightClip.linkGroupId = 0;
            indexClipGroup(clip->id, 0);
        }
    }

    // Resize original clip to be left half
    ClipOperations::setBeatPlacement(*clip, clip->placement.startBeat, leftLengthBeats, bpm);
    clip->name = clip->name + " L";

    // Sync loop region after split
    auto* leftEvent = clip->primaryEvent();
    if (clip->isMidi()) {
        if (clip->loopEnabled) {
            // Truncate each half's loop to its own portion.
            clip->loopLengthBeats = std::min(clip->loopLengthBeats, leftLengthBeats);
            rightClip.loopLengthBeats = std::min(rightClip.loopLengthBeats, rightLengthBeats);
        }
    } else if (leftEvent != nullptr && rightEvent != nullptr) {
        if (clip->loopEnabled) {
            // Beat-mode events keep the ORIGINAL source loop region: the right
            // half's anchor carries the playback phase, and truncating the
            // region to the split point makes the right side render silence.
            if (!leftEvent->autoTempo && leftEvent->interpBpm > 0.0) {
                if (leftEvent->loopLengthBeats() > leftLengthBeats)
                    leftEvent->setLoopLengthSeconds(leftLengthBeats * 60.0 / leftEvent->interpBpm);
                if (rightEvent->loopLengthBeats() > rightLengthBeats) {
                    rightEvent->setLoopLengthSeconds(rightLengthBeats * 60.0 /
                                                     rightEvent->interpBpm);
                    rightEvent->loopStartSamples = rightEvent->sourceAnchorSamples;
                }
            }
        } else {
            // Non-looped: each half's source region is exactly what it plays.
            // The right half's region must start at its anchor, or the engine
            // wraps and doubles the transient at the split point.
            leftEvent->setLoopLengthSeconds(
                leftEvent->timelineToSource(clip->getTimelineLength(bpm)));
            rightEvent->loopStartSamples = rightEvent->sourceAnchorSamples;
            rightEvent->setLoopLengthSeconds(
                rightEvent->timelineToSource(rightClip.getTimelineLength(bpm)));
        }
    }

    // Add right clip to the clip pool
    clips_[rightClip.id] = rightClip;
    addToSessionSlotIndex(clips_[rightClip.id]);
    indexClipGroup(rightClip.id, rightClip.linkGroupId);

    // Left clip mutated in place (length, midiNotes, loop range, fades, beats);
    // notifyClipsChanged carries only structural info (right clip added), so the
    // mutated left clip needs its own property notification.
    notifyClipPropertyChanged(clipId);
    notifyClipsChanged();

    return rightClip.id;
}

ClipId ClipManager::splitClip(ClipId clipId, double splitTime, double tempo) {
    const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
    return splitClipAtBeat(clipId, splitTime * bpm / 60.0, bpm);
}

void ClipManager::trimClipBeats(ClipId clipId, double newStartBeat, double newLengthBeats,
                                double tempo) {
    if (auto* clip = getClip(clipId)) {
        const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
        ClipOperations::setBeatPlacement(*clip, newStartBeat, newLengthBeats, bpm);
        if (clip->view == ClipView::Arrangement)
            resolveOverlaps(clipId);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::trimClip(ClipId clipId, double newStartTime, double newLength, double tempo) {
    const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
    trimClipBeats(clipId, newStartTime * bpm / 60.0, newLength * bpm / 60.0, bpm);
}

// ============================================================================
// Clip Properties
// ============================================================================

void ClipManager::setClipName(ClipId clipId, const juce::String& name) {
    if (auto* clip = getClip(clipId)) {
        clip->name = name;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipColour(ClipId clipId, juce::Colour colour) {
    if (auto* clip = getClip(clipId)) {
        clip->colour = colour;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipEnabled(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->enabled == enabled)
            return;
        clip->enabled = enabled;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipLoopEnabled(ClipId clipId, bool enabled, double projectBPM) {
    if (auto* clip = getClip(clipId)) {
        // Invariant: autoTempo (beat mode) requires loopEnabled. TE's
        // autoTempo beat range only operates over a loop region, and
        // ClipOperations' resize / offset math for autoTempo clips assumes
        // loopLengthBeats / loopStartBeats are live. Allowing loop-off while
        // beat mode is on lands the clip in a state nothing models, so
        // resize gestures fall through inconsistent branches and the user
        // sees the clip resize to an unrelated length. Reject the disable
        // here rather than corrupt state — the user must exit beat mode
        // first to turn looping off. Emit a property-changed notification
        // anyway so callers that flipped their local toggle optimistically
        // re-read the (unchanged) model and revert.
        auto* event = clip->primaryEvent();
        if (!enabled && event != nullptr && event->autoTempo) {
            notifyClipPropertyChanged(clipId);
            return;
        }
        clip->loopEnabled = enabled;

        // When enabling loop on MIDI clips, capture current length as loop region
        if (enabled && clip->isMidi()) {
            double bpm = isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();
            if (clip->loopLengthBeats <= 0.0)
                clip->loopLengthBeats = clip->getLengthInBeats();
        }

        // When enabling loop on audio, the current read position becomes the
        // loop start (phase resets to 0).
        if (enabled && event != nullptr && event->sourceFilePath().isNotEmpty()) {
            event->loopStartSamples = event->sourceAnchorSamples;

            // Loop on a clip loops what it shows now: the span becomes a range
            // of its own, so stretching the clip afterwards repeats it.
            if (event->loopLengthSamples <= 0) {
                const double bpm =
                    isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();
                event->setLoopLengthSeconds(event->timelineToSource(clip->getTimelineLength(bpm)));
            }

            sanitizeAudioClip(*clip);
        }

        // When disabling loop on MIDI clips, reset midiOffset — the looped
        // phase value has no meaning in non-looped mode.
        if (!enabled && clip->isMidi()) {
            clip->midiOffset = 0.0;
        }

        // When disabling loop on audio clips, snap the clip's timeline length
        // to the audible source content so the user doesn't end up with empty
        // space after the audio. Two reasons the previous behaviour wasn't
        // enough: the old clamp path only edited clip->length (the seconds
        // cache) without touching placement.lengthBeats, so the next
        // beats→seconds derive would resurrect the old length; and a clamp
        // (cap-if-longer) leaves the clip oversized whenever the file is
        // longer than the timeline span, which still reads as empty space
        // after a short loop region. Set the length explicitly to "file
        // content from offset on", routed through setPlacementBeats so the
        // beat domain stays authoritative.
        if (!enabled && event != nullptr && event->sourceFilePath().isNotEmpty()) {
            event->loopStartSamples = event->sourceAnchorSamples;

            double fileDuration = 0.0;
            if (auto* thumbnail =
                    AudioThumbnailManager::getInstance().getThumbnail(event->sourceFilePath())) {
                fileDuration = thumbnail->getTotalLength();
            }
            if (fileDuration <= 0.0)
                fileDuration = event->sourceDurationSeconds();

            const double speed = event->speedRatio > 0.0 ? event->speedRatio : 1.0;
            if (fileDuration > 0.0) {
                const double availableSource =
                    juce::jmax(0.0, fileDuration - event->anchorSeconds());
                const double newTimelineLength =
                    juce::jmax(ClipInfo::MIN_CLIP_LENGTH, availableSource / speed);
                const double bpm =
                    isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();
                clip->setPlacementBeats(clip->placement.startBeat, newTimelineLength * bpm / 60.0);
                clip->deriveTimesFromBeats(bpm);

                // The new timeline length can exceed the previous loop region,
                // which on the arrangement view can push the clip into a
                // neighbour. Match the policy other length-changing setters
                // (resizeClip / trimClip) already enforce.
                if (clip->view == ClipView::Arrangement)
                    resolveOverlaps(clipId);
            }
        }

        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipMidiOffset(ClipId clipId, double offsetBeats) {
    if (auto* clip = getClip(clipId)) {
        if (!clip->isMidi()) {
            return;
        }
        clip->midiOffset = juce::jmax(0.0, offsetBeats);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipLaunchMode(ClipId clipId, LaunchMode mode) {
    if (auto* clip = getClip(clipId)) {
        clip->launchMode = mode;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipLaunchQuantize(ClipId clipId, LaunchQuantize quantize) {
    if (auto* clip = getClip(clipId)) {
        clip->launchQuantize = quantize;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipFollowAction(ClipId clipId, FollowAction action) {
    if (auto* clip = getClip(clipId)) {
        clip->followAction = action;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipFollowActionDelayBeats(ClipId clipId, double delayBeats) {
    if (auto* clip = getClip(clipId)) {
        clip->followActionDelayBeats = juce::jmax(0.0, delayBeats);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipFollowActionLoopCount(ClipId clipId, int loopCount) {
    if (auto* clip = getClip(clipId)) {
        clip->followActionLoopCount = juce::jmax(1, loopCount);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipWarpEnabled(ClipId clipId, bool enabled) {
    if (auto* event = primaryEventOf(getClip(clipId))) {
        if (event->warpEnabled != enabled) {
            event->warpEnabled = enabled;
            if (enabled)
                event->analogPitch = false;  // Analog pitch is incompatible with warp
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setPlaybackIntent(ClipId clipId, PlaybackIntent intent, double projectBPM) {
    auto* clip = getClip(clipId);
    if (clip == nullptr || !clip->isAudio())
        return;

    ClipOperations::setPlaybackIntent(*clip, intent, projectBPM);

    auto* event = clip->primaryEvent();
    if (event == nullptr)
        return;

    // Beat mode plays through a stretcher; keep an engine the user chose.
    if (event->autoTempo && event->timeStretchMode == time_stretch_mode::kDisabled)
        event->timeStretchMode = time_stretch_mode::kSignalsmith;

    // Issue #1157: ClipOperations wrote the placement in beats at the mode
    // boundary. Refresh the seconds cache from it rather than converting back.
    refreshDerivedSeconds(clipId, projectBPM);
    notifyClipPropertyChanged(clipId);
}

void ClipManager::setAutoTempo(ClipId clipId, bool enabled, double bpm) {
    setPlaybackIntent(clipId, enabled ? PlaybackIntent::Beat : PlaybackIntent::Free, bpm);
}

void ClipManager::detectMissingTempo(const std::vector<ClipId>& clipIds, double /*projectBPM*/,
                                     std::function<void()> onReady) {
    std::vector<std::pair<ClipId, juce::String>> pending;
    for (auto id : clipIds) {
        const auto* event = primaryEventOf(getClip(id));
        if (event == nullptr || event->hasInterpretedBpm())
            continue;
        if (auto path = event->sourceFilePath(); path.isNotEmpty())
            pending.emplace_back(id, path);
    }
    if (pending.empty()) {
        if (onReady)
            onReady();
        return;
    }
    // Counted before any request goes out: a cached answer calls back at once.
    auto remaining = std::make_shared<size_t>(pending.size());
    auto ready = std::make_shared<std::function<void()>>(std::move(onReady));
    for (const auto& entry : pending) {
        const ClipId id = entry.first;
        const juce::String file = entry.second;
        AudioThumbnailManager::getInstance().requestBPMDetection(
            file, [id, file, remaining, ready](double bpm) {
                // The answer belongs to the clip that asked; adoptAnalysis
                // no-ops when it is gone, as it is after a test's teardown.
                ClipManager::getInstance().adoptAnalysis(id, file, bpm);
                if (--*remaining == 0 && *ready)
                    (*ready)();
            });
    }
}

void ClipManager::setOffset(ClipId clipId, double offset) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            // MIDI phase lives in midiOffset (beats) — caller passes beats directly
            clip->midiOffset = juce::jmax(0.0, offset);
        } else if (auto* event = clip->primaryEvent()) {
            event->setAnchorSeconds(offset);
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLoopPhase(ClipId clipId, double phase) {
    if (auto* clip = getClip(clipId)) {
        auto* event = clip->primaryEvent();
        if (event != nullptr && (clip->loopEnabled || event->autoTempo)) {
            // phase is in source seconds, measured from the loop start
            event->setAnchorSeconds(event->loopStartSeconds() + phase);
            sanitizeAudioClip(*clip);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setLoopStart(ClipId clipId, double loopStart, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
            clip->loopStartBeats =
                projectBpm > 0.0 ? (juce::jmax(0.0, loopStart) * projectBpm) / 60.0 : 0.0;
        } else if (auto* event = clip->primaryEvent()) {
            event->setLoopStartSeconds(loopStart);
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLoopLength(ClipId clipId, double loopLength, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
            clip->loopLengthBeats =
                projectBpm > 0.0 ? (juce::jmax(0.0, loopLength) * projectBpm) / 60.0 : 0.0;
        } else if (auto* event = clip->primaryEvent()) {
            event->setLoopLengthSeconds(loopLength);
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setAudioLoopLengthBeats(ClipId clipId, double loopLengthBeats) {
    if (auto* clip = getClip(clipId)) {
        auto* event = clip->primaryEvent();
        if (event == nullptr)
            return;
        event->setLoopLengthBeats(loopLengthBeats);
        sanitizeAudioClip(*clip);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::restoreLoopLength(ClipId clipId, const LoopLengthState& state, double bpm) {
    juce::ignoreUnused(bpm);
    if (auto* clip = getClip(clipId)) {
        auto* event = clip->primaryEvent();
        if (event == nullptr)
            return;
        event->restoreLoopLength(state);
        if (event->loopLengthIntent == LoopLengthIntent::Musical)
            event->fitRegionToMusicalLength();
        sanitizeAudioClip(*clip);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::restoreLoopLength(ClipId clipId, int64_t loopLengthSamples, RegionExtent extent,
                                    double bpm) {
    restoreLoopLength(clipId, {loopLengthSamples, extent, LoopLengthIntent::Source, 0.0}, bpm);
}

void ClipManager::restoreAudioLoopRegion(ClipId clipId, int64_t loopStartSamples,
                                         const LoopLengthState& lengthState,
                                         int64_t sourceAnchorSamples, double snapshotSampleRate) {
    if (auto* clip = getClip(clipId)) {
        auto* event = clip->primaryEvent();
        if (event == nullptr)
            return;

        const double currentRate = event->sourceSampleRate();
        const double ratio =
            snapshotSampleRate > 0.0 && currentRate > 0.0 ? currentRate / snapshotSampleRate : 1.0;
        const auto atCurrentRate = [ratio](int64_t samples) {
            return static_cast<int64_t>(std::llround(static_cast<double>(samples) * ratio));
        };
        auto restoredLength = lengthState;
        if (restoredLength.intent == LoopLengthIntent::Source)
            restoredLength.samples = atCurrentRate(restoredLength.samples);

        event->loopStartSamples = juce::jmax<int64_t>(0, atCurrentRate(loopStartSamples));
        event->restoreLoopLength(restoredLength);
        if (event->loopLengthIntent == LoopLengthIntent::Musical)
            event->fitRegionToMusicalLength();
        event->sourceAnchorSamples = juce::jmax<int64_t>(0, atCurrentRate(sourceAnchorSamples));
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setMidiLoopStartBeats(ClipId clipId, double loopStartBeats, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (!clip->isMidi())
            return;

        juce::ignoreUnused(bpm);
        clip->loopStartBeats = juce::jmax(0.0, loopStartBeats);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setMidiLoopLengthBeats(ClipId clipId, double loopLengthBeats, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (!clip->isMidi())
            return;

        juce::ignoreUnused(bpm);
        clip->loopLengthBeats = juce::jmax(0.0, loopLengthBeats);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::relocateLoopRegion(ClipId clipId, double loopStart, double loopLength,
                                     double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            const double oldLoopStart = event->loopStartSeconds();
            event->setLoopStartSeconds(loopStart);
            event->setLoopLengthSeconds(loopLength);

            // Composite intent: moving the region resets the phase to 0 by
            // snapping the read position to the new loop start.
            if (std::abs(event->loopStartSeconds() - oldLoopStart) > 1e-9)
                event->sourceAnchorSamples = event->loopStartSamples;

            sanitizeAudioClip(*clip);
        } else if (clip->isMidi()) {
            const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
            clip->loopStartBeats = (juce::jmax(0.0, loopStart) * projectBpm) / 60.0;
            clip->loopLengthBeats = (juce::jmax(0.0, loopLength) * projectBpm) / 60.0;
        }

        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::relocateLoopStartPreservingLength(ClipId clipId, double loopStart) {
    if (auto* clip = getClip(clipId)) {
        auto* event = clip->primaryEvent();
        if (event == nullptr)
            return;

        const auto lengthState = event->loopLengthState();
        const double oldLoopStart = event->loopStartSeconds();
        event->setLoopStartSeconds(loopStart);
        if (std::abs(event->loopStartSeconds() - oldLoopStart) > 1e-9)
            event->sourceAnchorSamples = event->loopStartSamples;
        sanitizeAudioClip(*clip);
        event->restoreLoopLength(lengthState);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::relocateMusicalLoopRegion(ClipId clipId, double loopStart,
                                            double loopLengthBeats) {
    if (auto* clip = getClip(clipId)) {
        auto* event = clip->primaryEvent();
        if (event == nullptr)
            return;

        const double oldLoopStart = event->loopStartSeconds();
        event->setLoopStartSeconds(loopStart);
        event->setLoopLengthBeats(loopLengthBeats);
        if (std::abs(event->loopStartSeconds() - oldLoopStart) > 1e-9)
            event->sourceAnchorSamples = event->loopStartSamples;
        sanitizeAudioClip(*clip);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLengthBeats(ClipId clipId, double newBeats, double bpm) {
    const auto* event = primaryEventOf(getClip(clipId));
    if (event == nullptr || !event->autoTempo || bpm <= 0.0)
        return;

    // Issue #1157: the beat-length slider edits USER INTENT only — how many
    // timeline beats the clip occupies. The source interpretation is NOT
    // touched here; the stretch follows from projectBPM / interpBpm at sync.
    auto* clip = getClip(clipId);
    const double minBeats = isValidBpm(bpm) ? (ClipInfo::MIN_CLIP_LENGTH * bpm / 60.0) : 0.0;
    clip->setPlacementBeats(clip->placement.startBeat, juce::jmax(minBeats, newBeats));

    refreshDerivedSeconds(clipId, bpm);
    notifyClipPropertyChanged(clipId);
}

void ClipManager::recordUserBpm(ClipId clipId, double bpm) {
    if (!isValidBpm(bpm)) {
        return;
    }
    const auto* event = primaryEventOf(getClip(clipId));
    if (event == nullptr)
        return;
    const auto filePath = event->sourceFilePath();
    if (filePath.isEmpty())
        return;
    magda::media::setUserBpmForFile(std::filesystem::path(filePath.toStdString()), bpm);
}

void ClipManager::recordUserKey(ClipId clipId, const std::string& root) {
    const auto* event = primaryEventOf(getClip(clipId));
    if (event == nullptr)
        return;
    const auto filePath = event->sourceFilePath();
    if (filePath.isEmpty())
        return;
    std::optional<std::string> rootOpt;
    if (!root.empty()) {
        rootOpt = root;
    }
    magda::media::setUserKeyRootForFile(std::filesystem::path(filePath.toStdString()), rootOpt);
}

bool ClipManager::canSaveClipToLibrary(ClipId clipId) const {
    const auto* clip = getClip(clipId);
    if (clip == nullptr) {
        return false;
    }
    if (clip->isMidi()) {
        // Chord-track progressions can be all-chords with their voicings
        // implied, so annotations alone make a clip worth saving.
        return !clip->midiNotes.empty() || !clip->midiCCData.empty() ||
               !clip->midiPitchBendData.empty() || !clip->chordAnnotations.empty();
    }
    const auto* event = clip->primaryEvent();
    if (event == nullptr)
        return false;
    const auto filePath = event->sourceFilePath();
    if (filePath.isEmpty())
        return false;
    return juce::File(filePath).existsAsFile();
}

bool ClipManager::saveClipToLibrary(ClipId clipId,
                                    std::optional<std::vector<WarpMarker>> warpMarkers) {
    auto* clip = getClip(clipId);
    if (clip == nullptr) {
        return false;
    }
    if (clip->isMidi()) {
        if (!canSaveClipToLibrary(clipId)) {
            return false;
        }

        auto& ctx = magda::media::MediaDbContext::getInstance();
        if (!ctx.ensureInitialized()) {
            return false;
        }

        // Chord-track clips are progressions: persist their chords as CHORD:
        // markers and save under the progressions dir so the indexer models
        // them as kind='progression'. A plain MIDI clip saves its notes only.
        const bool isProgression = TrackManager::getInstance().getChordTrackId() == clip->trackId;

        std::vector<magda::daw::ChordMarker> chordMarkers;
        if (isProgression) {
            for (const auto& ann : clip->chordAnnotations) {
                chordMarkers.push_back({ann.beatPosition, ann.lengthBeats, ann.chordName});
            }
        }

        const auto dir = isProgression ? ctx.progressionsDir() : ctx.midiClipsDir();
        const juce::File midiDir(juce::String(dir.string()));
        if (!midiDir.createDirectory()) {
            return false;
        }

        const auto outFile = midiLibraryFileForClip(*clip, midiDir);
        const double tempo = currentProjectTempoOrDefault();
        if (!magda::daw::MidiFileWriter::writeToFile(outFile, clip->midiNotes, clip->midiCCData,
                                                     clip->midiPitchBendData, tempo, clip->name,
                                                     chordMarkers)) {
            return false;
        }

        magda::media::MediaDbIndexer indexer(ctx.db(), nullptr);
        const auto stats =
            indexer.indexFile(std::filesystem::path(outFile.getFullPathName().toStdString()),
                              magda::media::MediaDbIndexer::Mode::ForceAll);
        if (stats.inserted + stats.updated + stats.skipped <= 0) {
            return false;
        }

        clip->midi().sourceFilePath = outFile.getFullPathName();
        notifyClipPropertyChanged(clipId);
        ctx.bumpMediaRevision();
        return true;
    }

    auto* event = clip->primaryEvent();
    if (event == nullptr)
        return false;
    const auto filePath = event->sourceFilePath();
    if (filePath.isEmpty())
        return false;
    const auto path = std::filesystem::path(filePath.toStdString());
    if (!magda::media::isFileIndexed(path)) {
        auto& ctx = magda::media::MediaDbContext::getInstance();
        if (!ctx.ensureInitialized()) {
            return false;
        }
        magda::media::MediaDbIndexer indexer(ctx.db(), nullptr);
        const auto stats = indexer.indexFile(path, magda::media::MediaDbIndexer::Mode::ForceAll);
        if (stats.inserted + stats.updated + stats.skipped <= 0) {
            return false;
        }
    }

    // The event's interpretation is what gets promoted to library metadata:
    // the Source only ever holds detected facts.
    std::optional<double> bpm;
    if (isValidBpm(event->interpBpm)) {
        bpm = event->interpBpm;
    }
    std::optional<double> totalBeats;
    if (event->interpTotalBeats > 0.0) {
        totalBeats = event->interpTotalBeats;
    }
    const std::optional<bool> beatMode = event->autoTempo;

    std::optional<std::string> keyRoot;
    if (!event->keyRoot.empty()) {
        keyRoot = event->keyRoot;
    }

    std::optional<std::vector<magda::media::WarpMarkerMetadata>> mediaMarkers;
    if (event->warpEnabled) {
        const auto& sourceMarkers = warpMarkers ? *warpMarkers : event->warpMarkers;
        std::vector<magda::media::WarpMarkerMetadata> converted;
        converted.reserve(sourceMarkers.size());
        for (const auto& marker : sourceMarkers) {
            converted.push_back({marker.sourceTime, marker.warpTime});
        }
        mediaMarkers = std::move(converted);
    }

    return magda::media::saveUserMetadataForFile(path, bpm, std::move(keyRoot), totalBeats,
                                                 beatMode, std::move(mediaMarkers));
}

/// A tempo landing on a clip that asked for beat mode grants it, and the
/// transition (loop, speed, stretch engine) has to follow the grant.
static void settleBeatMode(ClipInfo& clip, double projectBpm, bool wasInBeatMode) {
    auto* event = clip.primaryEvent();
    if (event == nullptr || !event->autoTempo)
        return;
    ClipOperations::setPlaybackIntent(clip, event->playbackIntent, projectBpm, wasInBeatMode);
    if (event->timeStretchMode == time_stretch_mode::kDisabled)
        event->timeStretchMode = time_stretch_mode::kSignalsmith;
}

void ClipManager::setSourceTempo(ClipId clipId, double bpm, Provenance from) {
    auto* clip = getClip(clipId);
    auto* event = primaryEventOf(clip);
    if (event == nullptr)
        return;

    // A tempo no file has is refused whole: a beat count typed into the wrong
    // field implied 43,000 BPM and the engine played a 20 ms sliver of the loop.
    if (!isValidBpm(bpm)) {
        return;
    }

    ensureSourceDurationKnown(*event);
    const bool wasInBeatMode = event->autoTempo;

    if (!event->adoptBpm(bpm, from)) {
        return;
    }

    // Tempo and beat count are one fact in two units, tied by the file length,
    // so stating either restates the other.
    const double fileSeconds = event->sourceDurationSeconds();
    if (fileSeconds > 0.0)
        event->adoptTotalBeats(beatCountForDuration(fileSeconds, bpm), from);
    fillSourceDurationFromInterpretation(*event);

    if (clip->loopEnabled)
        event->followInterpretationIfWholeSource();
    settleBeatMode(*clip, currentProjectTempoOrDefault(), wasInBeatMode);

    refreshDerivedSeconds(clipId, currentProjectTempoOrDefault());
    notifyClipPropertyChanged(clipId);
}

void ClipManager::setSourceBeatCount(ClipId clipId, double beats, Provenance from) {
    auto* clip = getClip(clipId);
    auto* event = primaryEventOf(clip);
    if (event == nullptr)
        return;

    if (!(beats > 0.0)) {
        return;
    }

    ensureSourceDurationKnown(*event);
    const bool wasInBeatMode = event->autoTempo;
    const double fileSeconds = event->sourceDurationSeconds();
    const double impliedBpm = fileSeconds > 0.0 ? beats * 60.0 / fileSeconds : 0.0;
    if (fileSeconds > 0.0 && !isValidBpm(impliedBpm)) {
        return;
    }

    // Checked before either write, so the pair can never land half-applied.
    if (!AudioEvent::provenanceAllows(event->beatsFrom, from) ||
        (fileSeconds > 0.0 && !AudioEvent::provenanceAllows(event->bpmFrom, from))) {
        return;
    }

    event->adoptTotalBeats(beats, from);
    if (fileSeconds > 0.0)
        event->adoptBpm(impliedBpm, from);
    fillSourceDurationFromInterpretation(*event);

    if (clip->loopEnabled)
        event->followInterpretationIfWholeSource();
    settleBeatMode(*clip, currentProjectTempoOrDefault(), wasInBeatMode);

    refreshDerivedSeconds(clipId, currentProjectTempoOrDefault());
    notifyClipPropertyChanged(clipId);
}

void ClipManager::adoptAnalysis(ClipId clipId, const juce::String& sourcePath, double bpm) {
    auto* clip = getClip(clipId);
    auto* event = primaryEventOf(clip);
    if (event == nullptr) {
        return;
    }
    if (bpm <= 0.0) {
        return;
    }
    // The request was for a file this clip no longer plays.
    if (event->sourceFilePath() != sourcePath) {
        return;
    }

    ensureSourceDurationKnown(*event);
    const bool wasInBeatMode = event->autoTempo;

    if (!event->adoptBpm(bpm, Provenance::Analysis)) {
        return;
    }

    const double fileSeconds = event->sourceDurationSeconds();
    if (fileSeconds > 0.0)
        event->adoptTotalBeats(beatCountForDuration(fileSeconds, bpm), Provenance::Analysis);
    fillSourceDurationFromInterpretation(*event);

    // A loop with a tempo is its beat count.
    if (clip->loopEnabled)
        event->followInterpretationIfWholeSource();
    settleBeatMode(*clip, currentProjectTempoOrDefault(), wasInBeatMode);

    refreshDerivedSeconds(clipId, currentProjectTempoOrDefault());
    notifyClipPropertyChanged(clipId);
}

void ClipManager::refreshDerivedSeconds(ClipId clipId, double projectBPM) {
    auto* clip = getClip(clipId);
    if (!clip)
        return;

    // TE requires speedRatio == 1.0 in autoTempo mode.
    if (auto* event = clip->primaryEvent(); event != nullptr && event->autoTempo)
        event->speedRatio = 1.0;

    // Timeline-domain seconds (length, startTime): depend on PROJECT BPM.
    // The source domain needs no refreshing at all now that it is stored in
    // samples; only these timeline caches are derived.
    if (isValidBpm(projectBPM)) {
        if (clip->placement.lengthBeats > 0.0)
            clip->length = clip->placement.lengthBeats * 60.0 / projectBPM;
        clip->startTime = clip->placement.startBeat * 60.0 / projectBPM;
        clip->startBeats = clip->placement.startBeat;
        clip->lengthBeats = clip->placement.lengthBeats;
    }
}

void ClipManager::setSpeedRatio(ClipId clipId, double speedRatio) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            const double bpm = currentProjectTempoOrDefault();
            double oldSourceExtent = event->timelineToSource(clip->getTimelineLength(bpm));
            event->speedRatio = juce::jlimit(ClipOperations::MIN_SPEED_RATIO,
                                             ClipOperations::MAX_SPEED_RATIO, speedRatio);
            double newSourceExtent = event->timelineToSource(clip->getTimelineLength(bpm));

            // Only a range the user chose follows the speed. A non-looping
            // clip reads to the file end, and a region sized by the
            // interpretation follows its beat count instead.
            if (clip->loopEnabled && event->loopExtent == RegionExtent::Explicit &&
                event->loopLengthIntent == LoopLengthIntent::Source &&
                std::abs(event->loopLengthSeconds() - oldSourceExtent) < 0.001) {
                event->setLoopLengthSeconds(newSourceExtent);
            }
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setTimeStretchMode(ClipId clipId, int mode) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->timeStretchMode = mode;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Pitch
// ============================================================================

void ClipManager::setAutoPitch(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->autoPitch = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAnalogPitch(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->analogPitch = enabled;
            if (enabled && !event->autoTempo && !event->warpEnabled) {
                // Analog pitch is sample-rate style playback: pitch and speed
                // are the same factor, and the selected source span stays fixed.
                const double pitchFactor = std::pow(2.0, event->pitchChange / 12.0);
                const double bpm = currentProjectTempoOrDefault();
                const double sourceContent = event->timelineToSource(clip->getTimelineLength(bpm));
                event->speedRatio = pitchFactor;
                ClipOperations::setTimelinePlacement(
                    *clip, clip->getTimelineStart(bpm),
                    juce::jmax(ClipInfo::MIN_CLIP_LENGTH, sourceContent / pitchFactor), bpm);
                if (clip->view == ClipView::Arrangement)
                    resolveOverlaps(clipId);
            }
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoPitchMode(ClipId clipId, int mode) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->autoPitchMode = juce::jlimit(0, 2, mode);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setPitchChange(ClipId clipId, float semitones) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            const double bpm = currentProjectTempoOrDefault();
            const double sourceContent = event->timelineToSource(clip->getTimelineLength(bpm));
            event->pitchChange = juce::jlimit(-48.0f, 48.0f, semitones);

            if (event->isAnalogPitchActive()) {
                const double newFactor = std::pow(2.0, event->pitchChange / 12.0);
                event->speedRatio = newFactor;
                ClipOperations::setTimelinePlacement(
                    *clip, clip->getTimelineStart(bpm),
                    juce::jmax(ClipInfo::MIN_CLIP_LENGTH, sourceContent / newFactor), bpm);
                if (clip->view == ClipView::Arrangement)
                    resolveOverlaps(clipId);
            }

            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setTranspose(ClipId clipId, int semitones) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->transpose = juce::jlimit(-24, 24, semitones);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Beat Detection
// ============================================================================

void ClipManager::setAutoDetectBeats(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->autoDetectBeats = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setBeatSensitivity(ClipId clipId, float sensitivity) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->beatSensitivity = juce::jlimit(0.0f, 1.0f, sensitivity);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Playback
// ============================================================================

void ClipManager::setIsReversed(ClipId clipId, bool reversed) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->reversed = reversed;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Groove/Shuffle/Swing
// ============================================================================

void ClipManager::setGrooveTemplate(ClipId clipId, const juce::String& templateName) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            clip->grooveTemplate = templateName;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setGrooveStrength(ClipId clipId, float strength) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            clip->grooveStrength = juce::jlimit(0.0f, 1.0f, strength);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Per-Clip Mix
// ============================================================================

void ClipManager::setClipVolumeDB(ClipId clipId, float dB) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->volumeDB = juce::jlimit(-100.0f, 0.0f, dB);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setClipGainDB(ClipId clipId, float dB) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->gainDB = juce::jlimit(0.0f, 24.0f, dB);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setClipPan(ClipId clipId, float pan) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->pan = juce::jlimit(-1.0f, 1.0f, pan);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Fades
// ============================================================================

void ClipManager::setFadeIn(ClipId clipId, double seconds) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->fadeInSeconds = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOut(ClipId clipId, double seconds) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->fadeOutSeconds = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->fadeInType = juce::jlimit(1, 4, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->fadeOutType = juce::jlimit(1, 4, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->fadeInBehaviour = juce::jlimit(0, 1, behaviour);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->fadeOutBehaviour = juce::jlimit(0, 1, behaviour);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoCrossfade(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->autoCrossfade = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setOverlapPlaysBoth(ClipId clipId, bool playsBoth) {
    if (auto* clip = getClip(clipId)) {
        if (clip->overlapPlaysBoth != playsBoth) {
            clip->overlapPlaysBoth = playsBoth;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setLaunchFadeSamples(ClipId clipId, int samples) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->launchFadeSamples = juce::jlimit(0, 16384, samples);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Crossfades (#1499)
// ============================================================================

// Guard margin (beats) keeping a crossfade edge strictly inside the other
// clip, so an overlap can never degenerate into full containment.
static constexpr double kCrossfadeEdgeGuardBeats = 1e-3;

namespace {

const ClipInfo* addressOf(const ClipInfo& clip) {
    return &clip;
}

bool isFound(const ClipInfo* clip) {
    return clip != nullptr;
}

const ClipInfo& derefClip(const ClipInfo* clip) {
    return *clip;
}

/// The crossfade rules take a lane of pointers and filter it themselves, so
/// what they are handed is every clip in the project.
std::vector<const ClipInfo*> everyClipAsLane(const std::unordered_map<ClipId, ClipInfo>& clips) {
    return clips | std::views::values | std::views::transform(addressOf) |
           toStd<std::vector<const ClipInfo*>>();
}

}  // namespace

std::optional<ClipManager::CrossfadeInfo> ClipManager::crossfadeAtStartIn(
    const std::vector<ClipInfo>& lane, ClipId clipId) {
    return ::magda::crossfadeAtStartIn(lane, clipId);
}

std::optional<ClipManager::CrossfadeInfo> ClipManager::crossfadeAtEndIn(
    const std::vector<ClipInfo>& lane, ClipId clipId) {
    return ::magda::crossfadeAtEndIn(lane, clipId);
}

std::vector<ClipInfo> ClipManager::arrangementLane(TrackId trackId) const {
    const auto clipIds = getClipsOnTrack(trackId, ClipView::Arrangement);
    const auto clipFor = [this](ClipId id) { return getClip(id); };

    return clipIds | std::views::transform(clipFor) | std::views::filter(isFound) |
           std::views::transform(derefClip) | toStd<std::vector<ClipInfo>>();
}

ClipManager::EffectiveFades ClipManager::effectiveFadesIn(const std::vector<ClipInfo>& lane,
                                                          ClipId clipId, double bpm) {
    return ::magda::effectiveFadesIn(lane, clipId, bpm);
}

ClipManager::EffectiveFades ClipManager::getEffectiveFades(ClipId clipId, double bpm) const {
    const auto* clip = getClip(clipId);
    if (clip == nullptr)
        return {};

    return effectiveFadesOf(*clip, everyClipAsLane(clips_), bpm);
}

std::optional<ClipManager::CrossfadeInfo> ClipManager::getCrossfadeAtStart(ClipId clipId) const {
    const auto* clip = getClip(clipId);
    if (!clip)
        return std::nullopt;

    return crossfadeAtStartOf(*clip, everyClipAsLane(clips_));
}

std::optional<ClipManager::CrossfadeInfo> ClipManager::getCrossfadeAtEnd(ClipId clipId) const {
    const auto* clip = getClip(clipId);
    if (!clip)
        return std::nullopt;

    return crossfadeAtEndOf(*clip, everyClipAsLane(clips_));
}

ClipId ClipManager::findCrossfadeNeighbour(ClipId clipId, bool atStart) const {
    const auto* clip = getClip(clipId);
    if (!clip || !clip->isAudio() || clip->view != ClipView::Arrangement)
        return INVALID_CLIP_ID;

    // Butt joints from splits are beat-exact; the tolerance only absorbs float
    // wiggle, it does not bridge real gaps.
    constexpr double tolBeats = 1e-4;
    const double startB = clip->placement.startBeat;
    const double endB = clip->placement.endBeat();

    ClipId bestId = INVALID_CLIP_ID;
    double bestEdge = 0.0;
    for (const auto& [cid, other] : clips_) {
        if (other.id == clipId || other.view != ClipView::Arrangement ||
            other.trackId != clip->trackId || !other.isAudio())
            continue;
        const double oStart = other.placement.startBeat;
        const double oEnd = other.placement.endBeat();
        if (atStart) {
            // Previous clip: starts before us, ends at/inside our span
            if (oStart < startB && oEnd >= startB - tolBeats && oEnd < endB) {
                if (bestId == INVALID_CLIP_ID || oEnd > bestEdge) {
                    bestId = other.id;
                    bestEdge = oEnd;
                }
            }
        } else {
            // Next clip: ends after us, starts at/inside our span
            if (oEnd > endB && oStart <= endB + tolBeats && oStart > startB) {
                if (bestId == INVALID_CLIP_ID || oStart < bestEdge) {
                    bestId = other.id;
                    bestEdge = oStart;
                }
            }
        }
    }
    return bestId;
}

double ClipManager::availableLeftExtensionBeats(const ClipInfo& clip, double bpm) {
    const auto* event = clip.primaryEvent();
    if (event == nullptr)
        return 0.0;
    if (clip.loopEnabled)
        return std::numeric_limits<double>::infinity();
    if (event->autoTempo && event->interpBpm > 0.0) {
        // Under autoTempo, timeline beats consume source beats 1:1.
        return juce::jmax(0.0, event->anchorBeats());
    }
    const double speed = event->speedRatio > 0.0 ? event->speedRatio : 1.0;
    return (juce::jmax(0.0, event->anchorSeconds()) / speed) * bpm / 60.0;
}

double ClipManager::availableRightExtensionBeats(const ClipInfo& clip, double bpm) {
    const auto* event = clip.primaryEvent();
    if (event == nullptr)
        return 0.0;
    if (clip.loopEnabled)
        return std::numeric_limits<double>::infinity();
    if (event->autoTempo && event->interpBpm > 0.0) {
        const double totalBeats = event->interpTotalBeats > 0.0
                                      ? event->interpTotalBeats
                                      : event->sourceDurationSeconds() * event->interpBpm / 60.0;
        if (totalBeats <= 0.0)
            return std::numeric_limits<double>::infinity();
        return juce::jmax(0.0, totalBeats - (event->anchorBeats() + clip.placement.lengthBeats));
    }
    const double sourceDuration = event->sourceDurationSeconds();
    if (sourceDuration <= 0.0)
        return std::numeric_limits<double>::infinity();
    const double speed = event->speedRatio > 0.0 ? event->speedRatio : 1.0;
    const double sourceEnd = event->anchorSeconds() + clip.getTimelineLength(bpm) * speed;
    return (juce::jmax(0.0, sourceDuration - sourceEnd) / speed) * bpm / 60.0;
}

bool ClipManager::setCrossfadeRegionBeats(ClipId leftId, ClipId rightId, double startBeat,
                                          double endBeat, double tempo) {
    auto* left = getClip(leftId);
    auto* right = getClip(rightId);
    if (!left || !right || left == right)
        return false;
    if (left->trackId != right->trackId)
        return false;
    if (left->view != ClipView::Arrangement || right->view != ClipView::Arrangement)
        return false;
    if (!left->isAudio() || !right->isAudio())
        return false;
    if (endBeat < startBeat)
        return false;

    if (right->placement.startBeat < left->placement.startBeat)
        std::swap(left, right);

    const double bpm = isValidBpm(tempo) ? tempo : currentProjectTempoOrDefault();
    const double minLenBeats = ClipInfo::MIN_CLIP_LENGTH * bpm / 60.0;

    const double leftStart = left->placement.startBeat;
    const double leftEnd = left->placement.endBeat();
    const double rightStart = right->placement.startBeat;
    const double rightEnd = right->placement.endBeat();

    // One clip already inside the other is not a region this can edit: the
    // overlap has no edge of either clip to move, so the resize below would
    // pull the containing clip in over its own material. A swallowed clip
    // fades over the whole of what they share and that is not draggable (#2003).
    if (leftEnd >= rightEnd)
        return false;

    // Left clip's new right edge: keep it strictly inside the right clip
    // (no containment), keep the left clip at least minimum length, and don't
    // outrun the left clip's source tail.
    const double maxLeftEnd = juce::jmin(rightEnd - kCrossfadeEdgeGuardBeats,
                                         leftEnd + availableRightExtensionBeats(*left, bpm));
    const double minLeftEnd = leftStart + minLenBeats;
    if (maxLeftEnd < minLeftEnd)
        return false;
    const double newEnd = juce::jlimit(minLeftEnd, maxLeftEnd, endBeat);

    // Right clip's new left edge: strictly after the left clip's start, keep
    // the right clip at least minimum length, and don't read before the start
    // of its source.
    const double minRightStart = juce::jmax(leftStart + kCrossfadeEdgeGuardBeats,
                                            rightStart - availableLeftExtensionBeats(*right, bpm));
    const double maxRightStart = rightEnd - minLenBeats;
    if (maxRightStart < minRightStart)
        return false;
    const double newStart = juce::jlimit(minRightStart, maxRightStart, startBeat);

    bool leftChanged = std::abs(newEnd - leftEnd) > 1e-9;
    bool rightChanged = std::abs(newStart - rightStart) > 1e-9;

    if (leftChanged) {
        // Beat-domain target; the seconds boundary stays confined to the
        // resize helpers (same convention as resolveOverlaps).
        ClipOperations::resizeContainerFromRight(*left, (newEnd - leftStart) * 60.0 / bpm, bpm);
    }
    if (rightChanged) {
        ClipOperations::resizeContainerFromLeft(*right, (rightEnd - newStart) * 60.0 / bpm, bpm);
    }

    // Asking for a crossfade here is asking for both clips to be heard over the
    // overlap, so it sets both switches: the fade on each edge, and the
    // play-through without which the overlap would silence one side and leave
    // the fade with nothing to fade into (#2003).
    if (newEnd - newStart > 0.0) {
        for (auto* clip : {left, right}) {
            bool& changed = (clip == left) ? leftChanged : rightChanged;
            if (!clip->autoCrossfade) {
                clip->autoCrossfade = true;
                changed = true;
            }
            if (!clip->overlapPlaysBoth) {
                clip->overlapPlaysBoth = true;
                changed = true;
            }
        }
    }

    if (leftChanged)
        notifyClipPropertyChanged(left->id);
    if (rightChanged)
        notifyClipPropertyChanged(right->id);
    return true;
}

bool ClipManager::setCrossfadeBeats(ClipId leftId, ClipId rightId, double durationBeats,
                                    double tempo) {
    const auto* left = getClip(leftId);
    const auto* right = getClip(rightId);
    if (!left || !right)
        return false;
    if (right->placement.startBeat < left->placement.startBeat)
        std::swap(left, right);

    // Joint centre: middle of the current overlap; for abutting clips this is
    // simply the touch point.
    const double centre = (right->placement.startBeat + left->placement.endBeat()) * 0.5;
    const double half = juce::jmax(0.0, durationBeats) * 0.5;
    return setCrossfadeRegionBeats(left->id, right->id, centre - half, centre + half, tempo);
}

// ============================================================================
// Channels
// ============================================================================

void ClipManager::setLeftChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->leftChannelActive = active;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setRightChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (auto* event = clip->primaryEvent()) {
            event->rightChannelActive = active;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Per-Clip Grid Settings
// ============================================================================

void ClipManager::setClipGridSettings(ClipId clipId, bool autoGrid, int numerator,
                                      int denominator) {
    if (auto* clip = getClip(clipId)) {
        const auto [num, den] = grid::normaliseFraction(numerator, denominator);
        clip->gridAutoGrid = autoGrid;
        clip->gridNumerator = num;
        clip->gridDenominator = den;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipSnapEnabled(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        clip->gridSnapEnabled = enabled;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipMidiEditorRowHeight(ClipId clipId, int rowHeight) {
    if (auto* clip = getClip(clipId)) {
        const int clampedHeight = juce::jlimit(ClipInfo::MIN_MIDI_EDITOR_ROW_HEIGHT,
                                               ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT, rowHeight);
        if (clip->midiEditorRowHeight != clampedHeight) {
            clip->midiEditorRowHeight = clampedHeight;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Content-Level Operations (Editor Operations)
// ============================================================================

void ClipManager::trimAudioLeft(ClipId clipId, double trimAmount, double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            ClipOperations::trimAudioFromLeft(*clip, trimAmount, fileDuration,
                                              currentProjectTempoOrDefault());
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::trimAudioRight(ClipId clipId, double trimAmount, double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            ClipOperations::trimAudioFromRight(*clip, trimAmount, fileDuration,
                                               currentProjectTempoOrDefault());
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioLeft(ClipId clipId, double newLength, double oldLength,
                                   double originalSpeedRatio, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            ClipOperations::stretchAudioFromLeft(*clip, newLength, oldLength, originalSpeedRatio,
                                                 bpm);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioRight(ClipId clipId, double newLength, double oldLength,
                                    double originalSpeedRatio, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            ClipOperations::stretchAudioFromRight(*clip, newLength, oldLength, originalSpeedRatio,
                                                  bpm);
            notifyClipPropertyChanged(clipId);
        }
    }
}

bool ClipManager::addMidiNote(ClipId clipId, const MidiNote& note) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            auto clippedNote = note;
            if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, clippedNote))
                return false;

            clip->midiNotes.push_back(clippedNote);
            notifyClipPropertyChanged(clipId);
            return true;
        }
    }
    return false;
}

void ClipManager::setMidiNotePitchExpression(ClipId clipId, size_t noteIndex,
                                             std::vector<MidiPitchExpressionPoint> points) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi() && noteIndex < clip->midiNotes.size()) {
            clip->midiNotes[noteIndex].pitchExpression = std::move(points);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::removeMidiNote(ClipId clipId, int noteIndex) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi() && noteIndex >= 0 &&
            noteIndex < static_cast<int>(clip->midiNotes.size())) {
            clip->midiNotes.erase(clip->midiNotes.begin() + noteIndex);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::clearMidiNotes(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            clip->midiNotes.clear();
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::addChordAnnotation(ClipId clipId, const ClipInfo::ChordAnnotation& annotation) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            clip->chordAnnotations.push_back(annotation);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::removeChordAnnotation(ClipId clipId, size_t index) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi() && index < clip->chordAnnotations.size()) {
            clip->chordAnnotations.erase(clip->chordAnnotations.begin() +
                                         static_cast<ptrdiff_t>(index));
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::clearChordAnnotations(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            clip->chordAnnotations.clear();
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Access
// ============================================================================

namespace {

bool isArrangementClip(const ClipInfo& clip) {
    return clip.view == ClipView::Arrangement;
}

bool isSessionClip(const ClipInfo& clip) {
    return clip.view == ClipView::Session;
}

}  // namespace

ClipInfo* ClipManager::getClip(ClipId clipId) {
    auto it = clips_.find(clipId);
    return (it != clips_.end()) ? &it->second : nullptr;
}

const ClipInfo* ClipManager::getClip(ClipId clipId) const {
    auto it = clips_.find(clipId);
    return (it != clips_.end()) ? &it->second : nullptr;
}

std::vector<ClipInfo> ClipManager::getArrangementClips() const {
    return clips_ | std::views::values | std::views::filter(isArrangementClip) |
           toStd<std::vector<ClipInfo>>();
}

std::vector<ClipInfo> ClipManager::getSessionClips() const {
    return clips_ | std::views::values | std::views::filter(isSessionClip) |
           toStd<std::vector<ClipInfo>>();
}

std::vector<ClipInfo> ClipManager::getClips() const {
    return clips_ | std::views::values | toStd<std::vector<ClipInfo>>();
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId) const {
    const auto onTrack = [trackId](const ClipInfo& clip) { return clip.trackId == trackId; };

    auto result = clips_ | std::views::values | std::views::filter(onTrack) |
                  std::views::transform(&ClipInfo::id) | toStd<std::vector<ClipId>>();
    sortByTimelineStart(result);
    return result;
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId, ClipView view) const {
    const auto onTrackInView = [trackId, view](const ClipInfo& clip) {
        return clip.trackId == trackId && clip.view == view;
    };

    auto result = clips_ | std::views::values | std::views::filter(onTrackInView) |
                  std::views::transform(&ClipInfo::id) | toStd<std::vector<ClipId>>();
    if (view == ClipView::Arrangement)
        sortByTimelineStart(result);
    return result;
}

void ClipManager::sortByTimelineStart(std::vector<ClipId>& clipIds) const {
    const double bpm = currentProjectTempoOrDefault();
    const auto startsEarlier = [this, bpm](ClipId a, ClipId b) {
        const auto* clipA = getClip(a);
        const auto* clipB = getClip(b);
        return clipA && clipB && clipA->getTimelineStart(bpm) < clipB->getTimelineStart(bpm);
    };
    std::ranges::sort(clipIds, startsEarlier);
}

ClipId ClipManager::getClipAtPosition(TrackId trackId, double time) const {
    const double bpm = currentProjectTempoOrDefault();
    const auto coversTime = [&](const auto& entry) {
        const auto& clip = entry.second;
        const double clipStart = clip.getTimelineStart(bpm);
        const double clipEnd = clip.getTimelineEnd(bpm);
        return clip.view == ClipView::Arrangement && clip.trackId == trackId && time >= clipStart &&
               time < clipEnd;
    };
    const auto found = std::ranges::find_if(clips_, coversTime);
    return found == clips_.end() ? INVALID_CLIP_ID : found->second.id;
}

std::vector<ClipId> ClipManager::getClipsInRange(TrackId trackId, double startTime,
                                                 double endTime) const {
    const double bpm = currentProjectTempoOrDefault();
    const auto overlapsRange = [&](const ClipInfo& clip) {
        return isArrangementClip(clip) && clip.trackId == trackId &&
               clip.getTimelineStart(bpm) < endTime && clip.getTimelineEnd(bpm) > startTime;
    };

    return clips_ | std::views::values | std::views::filter(overlapsRange) |
           std::views::transform(&ClipInfo::id) | toStd<std::vector<ClipId>>();
}

// ============================================================================
// Selection
// ============================================================================

void ClipManager::setSelectedClip(ClipId clipId) {
    if (selectedClipId_ != clipId) {
        selectedClipId_ = clipId;
        notifyClipSelectionChanged(clipId);
    }
}

void ClipManager::clearClipSelection() {
    selectedClipId_ = INVALID_CLIP_ID;
    // Always notify so listeners can clear stale visual state
    // (e.g. ClipComponents still showing selected after multi-clip deselection)
    notifyClipSelectionChanged(INVALID_CLIP_ID);
}

// ============================================================================
// Session View (Clip Launcher)
// ============================================================================

void ClipManager::addToSessionSlotIndex(const ClipInfo& clip) {
    if (clip.view != ClipView::Session || clip.sceneIndex < 0)
        return;
    sessionSlotIndex_[makeSessionSlotKey(clip.trackId, clip.sceneIndex)] = clip.id;
}

void ClipManager::removeFromSessionSlotIndex(const ClipInfo& clip) {
    if (clip.view != ClipView::Session || clip.sceneIndex < 0)
        return;
    auto it = sessionSlotIndex_.find(makeSessionSlotKey(clip.trackId, clip.sceneIndex));
    // Only erase if the cached entry still points at this clip — guards against
    // sequences where a slot was already overwritten by another mutation.
    if (it != sessionSlotIndex_.end() && it->second == clip.id)
        sessionSlotIndex_.erase(it);
}

ClipId ClipManager::getClipInSlot(TrackId trackId, int sceneIndex) const {
    if (sceneIndex < 0)
        return INVALID_CLIP_ID;
    auto it = sessionSlotIndex_.find(makeSessionSlotKey(trackId, sceneIndex));
    return it != sessionSlotIndex_.end() ? it->second : INVALID_CLIP_ID;
}

void ClipManager::setClipSceneIndex(ClipId clipId, int sceneIndex) {
    if (auto* clip = getClip(clipId)) {
        if (clip->sceneIndex == sceneIndex)
            return;
        removeFromSessionSlotIndex(*clip);
        clip->sceneIndex = sceneIndex;
        addToSessionSlotIndex(*clip);
        notifyClipsChanged();  // Structural change: old slot must also refresh
    }
}

void ClipManager::triggerClip(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        // Remember the last triggered session clip so transport Record can
        // re-trigger it. Don't touch selectedClipId_ — that's for UI selection.
        if (clip->view == ClipView::Session) {
            lastTriggeredSessionClipId_ = clipId;
        }

        // Emit a play request — the scheduler handles toggle logic,
        // same-track exclusion, and all state management.
        notifyClipPlaybackRequested(clipId, ClipPlaybackRequest::Play);
    }
}

void ClipManager::stopClip(ClipId clipId) {
    if (getClip(clipId)) {
        notifyClipPlaybackRequested(clipId, ClipPlaybackRequest::Stop);
    }
}

void ClipManager::stopAllClips() {
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Session)
            notifyClipPlaybackRequested(clip.id, ClipPlaybackRequest::Stop);
    }
}

// ============================================================================
// Listener Management
// ============================================================================

void ClipManager::addListener(ClipManagerListener* listener) {
    if (listener && std::ranges::find(listeners_, listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void ClipManager::removeListener(ClipManagerListener* listener) {
    std::erase(listeners_, listener);
}

// ============================================================================
// Project Management
// ============================================================================

void ClipManager::clearAllClips() {
    clips_.clear();
    sessionSlotIndex_.clear();
    selectedClipId_ = INVALID_CLIP_ID;
    nextClipId_ = 1;
    nextLinkGroupId_ = 1;
    // The clipboard survives project switches, but link-group ids are
    // project-scoped: a stale id pasted into the next project could collide
    // with an unrelated group and get overwritten by its propagation. Ghosts
    // paste as unique clips across projects.
    for (auto& entry : clipboard_)
        entry.linkGroupId = 0;
    notifyClipsChanged();
}

void ClipManager::createTestClips() {
    // Create random test clips on existing tracks for development
    auto& trackManager = TrackManager::getInstance();
    const auto& tracks = trackManager.getTracks();

    if (tracks.empty()) {
        return;
    }

    // Random number generator
    juce::Random random;

    for (const auto& track : tracks) {
        // Create 1-4 clips per track
        int numClips = random.nextInt({1, 4});
        double currentTime = random.nextFloat() * 2.0;  // Start within first 2 seconds

        for (int i = 0; i < numClips; ++i) {
            // Random clip length between 1 and 8 seconds
            double length = 1.0 + random.nextFloat() * 7.0;

            // Create MIDI clip in arrangement view (works on all track types for testing)
            createMidiClip(track.id, currentTime, length, ClipView::Arrangement);

            // Gap between clips (0 to 2 seconds)
            currentTime += length + random.nextFloat() * 2.0;
        }
    }
}

// ============================================================================
// Overlap Resolution
// ============================================================================

void ClipManager::resolveOverlaps(ClipId dominantClipId) {
    auto* dominant = getClip(dominantClipId);
    if (!dominant || dominant->view != ClipView::Arrangement) {
        return;
    }

    // Every path that places or moves an arrangement clip lands here, so this
    // is where "the clip you touched last is on top" gets decided (#2003).
    // That is now the whole job: nothing on the lane is trimmed, split or
    // deleted to make room. A clip keeps its placement and its content whatever
    // lands on it, computeAudibleSpans decides what each of them plays, and
    // moving the covering clip away brings the covered part back on its own.
    //
    // The one edit that survived until now was splitting an audio clip a drop
    // landed inside, into head / covered slice / tail. That existed because the
    // Tracktion mirror holds one engine clip per model clip and cannot express
    // a hole in the middle of one. Playback moves to the native engine, whose
    // clip snapshot carries the silenced ranges directly (#1890), so the split
    // has nothing left to buy and the lane keeps whole clips in every case.
    bringToFrontOfStack(*dominant);
}

// ============================================================================
// Private Helpers
// ============================================================================

void ClipManager::bringToFrontOfStack(ClipInfo& clip) {
    if (clip.view != ClipView::Arrangement)
        return;
    clip.stackOrder = nextStackOrder_++;
}

void ClipManager::notifyClipsChanged() {
    // Structural changes may have assigned whole ClipInfo structs outside the
    // manager (undo snapshot restores); re-derive the link-group index before
    // listeners query it.
    rebuildLinkGroupIndex();

    // Make a copy because listeners may be removed during iteration
    // (e.g., ClipComponent destroyed when TrackContentPanel rebuilds)
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipsChanged();
        }
    }
}

void ClipManager::notifyClipPropertyChanged(ClipId clipId) {
    // Keep the active take in sync with piano-roll edits before notifying, so
    // per-take edits are preserved across take switches (#1465/#1466).
    // Reconcile the link-group index first: commands restore whole ClipInfo
    // snapshots (linkGroupId included) outside the manager, and this funnel
    // is their notification contract.
    if (auto* clip = getClip(clipId)) {
        indexClipGroup(clipId, clip->linkGroupId);
        syncActiveMidiTake(*clip);
    }

    // Ghost clips: every content edit funnels through here, so mirror the
    // shared fields to the link-group siblings and notify them in the same
    // pass (no recursion — siblings are notified directly below/batched).
    const auto siblings = propagateLinkGroupContent(clipId);

    if (batchDepth_ > 0) {
        // Coalesce: record once, fire at end of outermost batch.
        auto append = [this](ClipId id) {
            if (std::ranges::find(batchedClipIds_, id) == batchedClipIds_.end()) {
                batchedClipIds_.push_back(id);
            }
        };
        append(clipId);
        for (auto siblingId : siblings)
            append(siblingId);
        return;
    }
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipPropertyChanged(clipId);
            for (auto siblingId : siblings)
                listener->clipPropertyChanged(siblingId);
        }
    }
}

void ClipManager::beginBatch() {
    ++batchDepth_;
}

void ClipManager::endBatch() {
    if (batchDepth_ <= 0) {
        jassertfalse;  // unbalanced endBatch
        return;
    }
    if (--batchDepth_ > 0)
        return;

    if (batchedClipIds_.empty())
        return;

    auto ids = std::move(batchedClipIds_);
    batchedClipIds_.clear();

    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipPropertiesChanged(ids);
        }
    }
}

ClipManager::ScopedListenerMuteForTests::ScopedListenerMuteForTests() {
    auto& manager = ClipManager::getInstance();
    savedListeners_ = std::move(manager.listeners_);
    manager.listeners_.clear();
}

ClipManager::ScopedListenerMuteForTests::~ScopedListenerMuteForTests() {
    auto& manager = ClipManager::getInstance();
    manager.listeners_ = std::move(savedListeners_);
}

void ClipManager::notifyClipSelectionChanged(ClipId clipId) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipSelectionChanged(clipId);
        }
    }
}

void ClipManager::notifyClipPlaybackStateChanged(ClipId clipId) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipPlaybackStateChanged(clipId);
        }
    }
}

void ClipManager::notifyClipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipPlaybackRequested(clipId, request);
        }
    }
}

void ClipManager::notifyClipDragPreview(ClipId clipId, double previewStartTime,
                                        double previewLength) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::ranges::find(listeners_, listener) != listeners_.end()) {
            listener->clipDragPreview(clipId, previewStartTime, previewLength);
        }
    }
}

juce::String ClipManager::generateClipName(ClipType type) const {
    const auto isType = [type](const auto& entry) { return entry.second.getType() == type; };
    const int count = 1 + static_cast<int>(std::ranges::count_if(clips_, isType));

    if (type == ClipType::Audio) {
        return "Audio " + juce::String(count);
    } else {
        return "MIDI " + juce::String(count);
    }
}

void ClipManager::sanitizeAudioClip(ClipInfo& clip) {
    if (!clip.isAudio())
        return;

    // Every event is clamped to its own source: they can point at different
    // files even though nothing builds such a clip yet.
    for (auto& event : clip.audio().events) {
        const auto filePath = event.sourceFilePath();
        if (filePath.isEmpty())
            continue;

        auto* thumbnail = AudioThumbnailManager::getInstance().getThumbnail(filePath);
        double fileDuration = thumbnail ? thumbnail->getTotalLength() : 0.0;
        if (fileDuration <= 0.0)
            fileDuration = event.sourceDurationSeconds();
        if (fileDuration <= 0.0)
            continue;

        event.clampLoopRegionToSource(fileDuration);
        event.setAnchorSeconds(juce::jlimit(0.0, fileDuration, event.anchorSeconds()));

        if (!clip.loopEnabled && !event.autoTempo) {
            const double bpm = currentProjectTempoOrDefault();
            const double currentLength = clip.getTimelineLength(bpm);
            const double available = fileDuration - event.anchorSeconds();
            const double maxLength = available / event.speedRatio;
            if (currentLength > maxLength) {
                ClipOperations::setTimelinePlacement(
                    clip, clip.getTimelineStart(bpm),
                    juce::jmax(ClipInfo::MIN_CLIP_LENGTH, maxLength), bpm);
            }
        }
    }
}

// ============================================================================
// Clipboard Operations
// ============================================================================

void ClipManager::copyToClipboard(const std::unordered_set<ClipId>& clipIds) {
    clipboard_.clear();

    if (clipIds.empty()) {
        return;
    }

    // Find the earliest start beat to use as the paste reference anchor.
    clipboardReferenceBeat_ = std::numeric_limits<double>::max();
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboardReferenceBeat_ = std::min(clipboardReferenceBeat_, clip->placement.startBeat);
        }
    }

    // Copy clips maintaining relative positions
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboard_.push_back(*clip);
        }
    }

    stashClipboardSourcePaths();
    DBG("CLIPBOARD: Copied " << clipboard_.size() << " clip(s)");
}

void ClipManager::copyBeatRangeToClipboard(double startBeat, double endBeat,
                                           const std::vector<TrackId>& trackIds, double tempoBPM) {
    clipboard_.clear();
    clipboardReferenceBeat_ = startBeat;

    if (startBeat >= endBeat)
        return;

    for (const auto& [id, clip] : clips_) {
        if (clip.view != ClipView::Arrangement)
            continue;
        // Filter by track if trackIds is non-empty
        if (!trackIds.empty()) {
            if (std::ranges::find(trackIds, clip.trackId) == trackIds.end())
                continue;
        }

        // Overlap against beat-authoritative timeline placement.
        const double clipStartBeat = clip.placement.startBeat;
        const double clipEndBeat = clip.placement.endBeat();
        if (clipStartBeat >= endBeat || clipEndBeat <= startBeat)
            continue;

        const double overlapStartBeat = std::max(clipStartBeat, startBeat);
        const double overlapEndBeat = std::min(clipEndBeat, endBeat);

        ClipInfo trimmed = clip;
        ClipOperations::setBeatPlacement(trimmed, overlapStartBeat,
                                         overlapEndBeat - overlapStartBeat, tempoBPM);

        if (auto* trimmedEvent = trimmed.primaryEvent()) {
            // Advance the source read position by the trimmed-off beats.
            const double trimFromLeftBeats = overlapStartBeat - clipStartBeat;
            if (trimmedEvent->interpBpm > 0.0) {
                // Beat mode: the trim is a source-beat distance.
                trimmedEvent->setAnchorBeats(trimmedEvent->anchorBeats() + trimFromLeftBeats);
            } else if (isValidBpm(tempoBPM)) {
                // Otherwise convert the beat trim to timeline seconds, then to
                // source seconds via speedRatio.
                const double trimFromLeftSeconds = trimFromLeftBeats * 60.0 / tempoBPM;
                trimmedEvent->setAnchorSeconds(trimmedEvent->anchorSeconds() +
                                               trimFromLeftSeconds * trimmedEvent->speedRatio);
            }
            // Sync the source region for non-looped events
            if (!trimmed.loopEnabled) {
                trimmedEvent->loopStartSamples = trimmedEvent->sourceAnchorSamples;
                trimmedEvent->setLoopLengthSeconds(
                    trimmedEvent->timelineToSource(trimmed.getTimelineLength(tempoBPM)));
            }
        } else if (clip.isMidi() && !clip.midiNotes.empty()) {
            // Notes are in beats relative to clip start; re-base to the overlap.
            const double overlapStartRelBeat = overlapStartBeat - clipStartBeat;
            const double overlapEndRelBeat = overlapEndBeat - clipStartBeat;

            std::vector<MidiNote> filteredNotes;
            for (const auto& note : clip.midiNotes) {
                if (note.startBeat >= overlapStartRelBeat && note.startBeat < overlapEndRelBeat) {
                    MidiNote adjusted = note;
                    adjusted.startBeat -= overlapStartRelBeat;
                    filteredNotes.push_back(adjusted);
                }
            }
            trimmed.midiNotes = filteredNotes;
        }

        // Range copies rewrite content (note partition, loop/offset re-base),
        // so they can never paste back into the link group: a later
        // propagation would push the trimmed content over every sibling.
        // Whole-clip copyToClipboard keeps membership; range copies detach.
        trimmed.linkGroupId = 0;

        clipboard_.push_back(trimmed);
    }

    stashClipboardSourcePaths();
}

void ClipManager::copyTimeRangeToClipboard(double startTime, double endTime,
                                           const std::vector<TrackId>& trackIds, double tempoBPM) {
    // Seconds shim: convert the range to beats and delegate to the
    // beats-authoritative implementation.
    const double bpm = isValidBpm(tempoBPM) ? tempoBPM : currentProjectTempoOrDefault();
    copyBeatRangeToClipboard(startTime * bpm / 60.0, endTime * bpm / 60.0, trackIds, bpm);
}

std::vector<ClipId> ClipManager::pasteFromClipboardBeats(double pasteBeat, TrackId targetTrackId,
                                                         ClipView targetView,
                                                         int targetSceneIndex) {
    std::vector<ClipId> newClips;

    if (clipboard_.empty()) {
        return newClips;
    }

    // Clipboard positions are beat-domain; maintain relative positions in beats.
    const double beatOffset = pasteBeat - clipboardReferenceBeat_;

    // Track which scene slots have been used during this paste (for multi-clip session paste)
    std::unordered_map<TrackId, int> trackSceneMap;

    // Clips that resolve to no track at all. A real paste entry point always
    // supplies a target via resolvePasteTarget(), so a non-zero count here means
    // a caller skipped that resolver (the #1670 class of bug). Surfaced below.
    int droppedForNoTrack = 0;

    for (const auto& clipData : clipboard_) {
        // Calculate new start beat maintaining relative position
        const double newStartBeat = clipData.placement.startBeat + beatOffset;
        const double clipLengthBeats = clipData.placement.lengthBeats;

        // Determine target track
        TrackId newTrackId = (targetTrackId != INVALID_TRACK_ID) ? targetTrackId : clipData.trackId;
        if (newTrackId == INVALID_TRACK_ID) {
            ++droppedForNoTrack;
            continue;
        }

        // Create new clip based on type, using targetView instead of clipData.view
        ClipId newClipId = INVALID_CLIP_ID;
        if (const auto* pastedEvent = clipData.primaryEvent()) {
            const auto pastedPath = clipboardSourcePathFor(*pastedEvent);
            if (pastedPath.isNotEmpty()) {
                newClipId =
                    createAudioClipBeats(newTrackId, newStartBeat, clipLengthBeats, pastedPath,
                                         targetView, 0.0, ClipOverlapPolicy::ResolveOverlaps);
            }
        } else if (clipData.isMidi()) {
            // For MIDI clips, create empty then copy notes
            newClipId = createMidiClipBeats(newTrackId, newStartBeat, clipLengthBeats, targetView,
                                            ClipOverlapPolicy::ResolveOverlaps);
        }

        if (newClipId != INVALID_CLIP_ID) {
            // Copy properties
            auto* newClip = getClip(newClipId);
            if (newClip) {
                // Ghost membership survives whole-clip copy/paste. Range
                // copies cleared it at copy time, and clearAllClips scrubs the
                // clipboard so cross-project pastes come in unlinked. Ghosts
                // keep the source name (they are the same content).
                newClip->linkGroupId = clipData.linkGroupId;
                indexClipGroup(newClipId, newClip->linkGroupId);
                newClip->name =
                    clipData.linkGroupId != 0 ? clipData.name : clipData.name + " (copy)";
                if (clipData.trackId != INVALID_TRACK_ID) {
                    newClip->colour = clipData.colour;
                } else if (const auto* targetTrack =
                               TrackManager::getInstance().getTrack(newTrackId)) {
                    newClip->colour = targetTrack->colour;
                }
                newClip->loopEnabled = clipData.loopEnabled;
                newClip->enabled = clipData.enabled;

                // Copy MIDI data
                if (clipData.isMidi()) {
                    newClip->midi().sourceFilePath = clipData.midi().sourceFilePath;
                    newClip->midiNotes = clipData.midiNotes;
                    newClip->midiOffset = clipData.midiOffset;
                    newClip->midiCCData = clipData.midiCCData;
                    newClip->midiPitchBendData = clipData.midiPitchBendData;
                }

                // Copy audio properties — but NOT when pasting arrangement→session,
                // because createAudioClip already set correct session defaults
                // (autoTempo, beat values, offset=0, loopStart=0).
                bool crossViewToSession =
                    (targetView == ClipView::Session && clipData.view == ClipView::Arrangement);

                auto* newEvent = newClip->primaryEvent();
                const auto* srcEvent = clipData.primaryEvent();
                if (newEvent != nullptr && srcEvent != nullptr) {
                    // The destination event keeps its own identity and its own
                    // pooled source (createAudioClipBeats already resolved the
                    // path); everything else comes from the copied event.
                    const EventId keepId = newEvent->id;
                    const SourceId keepSourceId = newEvent->sourceId;

                    if (crossViewToSession) {
                        // createAudioClipBeats already applied the session
                        // defaults (beat mode, looping, anchor at 0), so the
                        // fields that place the read head stay as created.
                        // Interpretation and per-event playback still copy.
                        if (srcEvent->warpEnabled) {
                            newEvent->warpEnabled = true;
                            newEvent->timeStretchMode = srcEvent->timeStretchMode;
                        }
                        newEvent->warpMarkers = srcEvent->warpMarkers;
                        newEvent->adoptInterpretationFrom(*srcEvent);
                        newEvent->setPlaybackIntent(srcEvent->playbackIntent);
                        newEvent->autoPitch = srcEvent->autoPitch;
                        newEvent->analogPitch = srcEvent->analogPitch;
                        newEvent->autoPitchMode = srcEvent->autoPitchMode;
                        newEvent->pitchChange = srcEvent->pitchChange;
                        newEvent->transpose = srcEvent->transpose;
                        newEvent->reversed = srcEvent->reversed;
                        newEvent->leftChannelActive = srcEvent->leftChannelActive;
                        newEvent->rightChannelActive = srcEvent->rightChannelActive;
                    } else {
                        *newEvent = *srcEvent;
                    }

                    newEvent->id = keepId;
                    newEvent->sourceId = keepSourceId;
                }

                if (!crossViewToSession) {
                    if (newClip->isMidi() && clipData.isMidi()) {
                        newClip->loopStartBeats = clipData.loopStartBeats;
                        newClip->loopLengthBeats = clipData.loopLengthBeats;
                    }
                    const double pastedLengthBeats = clipData.placement.lengthBeats > 0.0
                                                         ? clipData.placement.lengthBeats
                                                         : clipData.lengthBeats;
                    if (pastedLengthBeats > 0.0)
                        newClip->setPlacementBeats(newClip->placement.startBeat, pastedLengthBeats);
                    // Don't overwrite startBeats — createMidiClip/createAudioClip already
                    // computed the correct value from newStartTime
                }

                // Mix
                newClip->volumeDB = clipData.volumeDB;
                newClip->gainDB = clipData.gainDB;
                newClip->pan = clipData.pan;

                // Grid settings
                newClip->gridAutoGrid = clipData.gridAutoGrid;
                newClip->gridNumerator = clipData.gridNumerator;
                newClip->gridDenominator = clipData.gridDenominator;
                newClip->gridSnapEnabled = clipData.gridSnapEnabled;
                newClip->midiEditorRowHeight = clipData.midiEditorRowHeight;

                // Cross-view translation: pasting into session view
                if (targetView == ClipView::Session && targetSceneIndex >= 0) {
                    // Find next empty slot for this track
                    if (trackSceneMap.find(newTrackId) == trackSceneMap.end()) {
                        trackSceneMap[newTrackId] = targetSceneIndex;
                    }
                    int sceneForThisClip = trackSceneMap[newTrackId];
                    while (getClipInSlot(newTrackId, sceneForThisClip) != INVALID_CLIP_ID) {
                        sceneForThisClip++;
                    }
                    // The clip was inserted by createAudioClip/createMidiClip
                    // with sceneIndex=-1, so the slot index has no entry yet.
                    // Just add now that we know the final scene.
                    newClip->sceneIndex = sceneForThisClip;
                    addToSessionSlotIndex(*newClip);
                    trackSceneMap[newTrackId] = sceneForThisClip + 1;
                    newClip->loopEnabled = true;
                    newClip->launchMode = clipData.launchMode;
                    newClip->launchQuantize = clipData.launchQuantize;
                    newClip->followAction = clipData.followAction;
                    newClip->followActionDelayBeats = clipData.followActionDelayBeats;
                    newClip->followActionLoopCount = clipData.followActionLoopCount;

                    if (!crossViewToSession && clipData.loopEnabled) {
                        // Reset extended loops to base loop length for
                        // session→session pastes
                        const double bpm = currentProjectTempoOrDefault();
                        const double loopBeats = clipData.loopLengthInBeats(bpm);
                        const auto* srcLoopEvent = clipData.primaryEvent();
                        if (loopBeats > 0.0 && clipData.lengthBeats > loopBeats) {
                            ClipOperations::setTimelinePlacement(*newClip,
                                                                 newClip->getTimelineStart(bpm),
                                                                 loopBeats * 60.0 / bpm, bpm);
                        } else if (loopBeats <= 0.0 && srcLoopEvent != nullptr &&
                                   srcLoopEvent->loopLengthSamples > 0 &&
                                   clipData.getTimelineLength(bpm) >
                                       srcLoopEvent->sourceToTimeline(
                                           srcLoopEvent->loopLengthSeconds())) {
                            // No usable beat view of the region: fall back to
                            // its timeline extent.
                            ClipOperations::setTimelinePlacement(
                                *newClip, newClip->getTimelineStart(bpm),
                                srcLoopEvent->sourceToTimeline(srcLoopEvent->loopLengthSeconds()),
                                bpm);
                        }
                    }
                }

                // The slot loops (set just above), so a whole-source region
                // follows the interpretation the paste adopted.
                if (crossViewToSession && newClip->loopEnabled) {
                    if (auto* pastedEvent = newClip->primaryEvent())
                        pastedEvent->followInterpretationIfWholeSource();
                }

                // A whole-clip copy's snapshot may be stale: copy ghost A,
                // edit a sibling, paste - the pasted clip would rejoin
                // carrying pre-copy content and the notify below would
                // propagate it over every sibling. The live group is
                // authoritative (same rule as restoreClip): adopt its
                // current shared content on rejoin.
                if (newClip->linkGroupId != 0) {
                    const auto siblings = getLinkGroupSiblings(newClipId);
                    if (!siblings.empty()) {
                        if (const auto* source = getClip(siblings.front()))
                            newClip->copySharedContentFrom(*source);
                    }
                }

                if (newClip->view == ClipView::Arrangement)
                    resolveOverlaps(newClipId);
                forceNotifyClipPropertyChanged(newClipId);
            }

            newClips.push_back(newClipId);
        }
    }

    if (droppedForNoTrack > 0) {
        DBG("CLIPBOARD: pasteFromClipboardBeats dropped "
            << droppedForNoTrack
            << " clip(s) with no target track (no explicit target and no source track). "
               "The paste entry point should resolve a target via resolvePasteTarget().");
    }

    if (!newClips.empty())
        notifyClipsChanged();

    return newClips;
}

std::vector<ClipId> ClipManager::pasteFromClipboard(double pasteTime, TrackId targetTrackId,
                                                    ClipView targetView, int targetSceneIndex) {
    // Seconds shim: convert the paste position to beats and delegate to the
    // beats-authoritative implementation.
    const double bpm = currentProjectTempoOrDefault();
    const double pasteBeat = isValidBpm(bpm) ? pasteTime * bpm / 60.0 : 0.0;
    return pasteFromClipboardBeats(pasteBeat, targetTrackId, targetView, targetSceneIndex);
}

void ClipManager::cutToClipboard(const std::unordered_set<ClipId>& clipIds) {
    // Copy to clipboard
    copyToClipboard(clipIds);

    // Delete original clips
    for (auto clipId : clipIds) {
        deleteClip(clipId);
    }
}

bool ClipManager::hasClipsInClipboard() const {
    return !clipboard_.empty();
}

double ClipManager::getClipboardBeatSpan() const {
    double maxEnd = clipboardReferenceBeat_;
    for (const auto& clip : clipboard_)
        maxEnd = std::max(maxEnd, clip.placement.endBeat());
    return std::max(0.0, maxEnd - clipboardReferenceBeat_);
}

bool ClipManager::clipboardRequiresTargetTrack() const {
    const auto hasNoTargetTrack = [](const auto& clip) { return clip.trackId == INVALID_TRACK_ID; };
    return std::ranges::any_of(clipboard_, hasNoTargetTrack);
}

void ClipManager::clearClipboard() {
    clipboard_.clear();
    clipboardReferenceBeat_ = 0.0;
}

void ClipManager::setMidiClipClipboard(std::vector<MidiNote> notes, juce::String name,
                                       double lengthBeats) {
    clipboard_.clear();
    clipboardReferenceBeat_ = 0.0;

    if (notes.empty()) {
        return;
    }

    double minBeat = notes.front().startBeat;
    double maxEndBeat = notes.front().startBeat + notes.front().lengthBeats;
    for (const auto& note : notes) {
        minBeat = std::min(minBeat, note.startBeat);
        maxEndBeat = std::max(maxEndBeat, note.startBeat + note.lengthBeats);
    }

    const double noteOffset = lengthBeats > 0.0 ? 0.0 : minBeat;
    for (auto& note : notes)
        note.startBeat -= noteOffset;

    ClipInfo clip;
    clip.setMidiContent();
    clip.name = std::move(name);
    clip.trackId = INVALID_TRACK_ID;
    clip.view = ClipView::Arrangement;
    clip.midiNotes = std::move(notes);
    const double inferredLength = maxEndBeat - noteOffset;
    const double clipboardLength =
        lengthBeats > 0.0 ? juce::jmax(lengthBeats, inferredLength) : inferredLength;
    clip.setPlacementBeats(0.0, juce::jmax(0.25, clipboardLength));
    clip.deriveTimesFromBeats(currentProjectTempoOrDefault());

    clipboard_.push_back(std::move(clip));
    stashClipboardSourcePaths();
}

// ============================================================================
// Note Clipboard Operations
// ============================================================================

void ClipManager::copyNotesToClipboard(ClipId clipId, const std::vector<size_t>& noteIndices) {
    noteClipboard_.clear();
    noteClipboardMinBeat_ = 0.0;

    const auto* clip = getClip(clipId);
    if (!clip || !clip->isMidi() || noteIndices.empty()) {
        return;
    }

    // Copy selected notes
    double minBeat = std::numeric_limits<double>::max();
    for (size_t idx : noteIndices) {
        if (idx < clip->midiNotes.size()) {
            noteClipboard_.push_back(clip->midiNotes[idx]);
            minBeat = std::min(minBeat, clip->midiNotes[idx].startBeat);
        }
    }

    if (noteClipboard_.empty()) {
        return;
    }

    // Store original earliest beat and normalise
    noteClipboardMinBeat_ = minBeat;
    for (auto& note : noteClipboard_) {
        note.startBeat -= minBeat;
    }
}

bool ClipManager::hasNotesInClipboard() const {
    return !noteClipboard_.empty();
}

const std::vector<MidiNote>& ClipManager::getNoteClipboard() const {
    return noteClipboard_;
}

double ClipManager::getNoteClipboardMinBeat() const {
    return noteClipboardMinBeat_;
}

void ClipManager::setNoteClipboard(std::vector<MidiNote> notes) {
    noteClipboard_ = std::move(notes);
    noteClipboardMinBeat_ = 0.0;
    if (!noteClipboard_.empty()) {
        double minBeat = noteClipboard_.front().startBeat;
        for (const auto& n : noteClipboard_)
            minBeat = std::min(minBeat, n.startBeat);
        noteClipboardMinBeat_ = minBeat;
    }
}

}  // namespace magda
