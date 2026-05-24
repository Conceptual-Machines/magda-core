#include "ClipManager.hpp"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "../project/ProjectManager.hpp"
#include "ClipOperations.hpp"
#include "Config.hpp"
#include "MidiFileWriter.hpp"
#include "TempoUtils.hpp"
#include "TrackManager.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "media_db/MediaDbContext.hpp"
#include "media_db/MediaDbIndexer.hpp"
#include "media_db/MediaDbMetadata.hpp"

namespace magda {

namespace {

double currentProjectTempoOrDefault() {
    double bpm = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    return isValidBpm(bpm) ? bpm : DEFAULT_BPM;
}

bool sourceInterpretationBpmLooksDefaulted(const ClipInfo& clip, double projectBPM) {
    if (clip.audio().interpretation.bpm <= 0.0)
        return true;
    if (projectBPM <= 0.0 || std::abs(clip.audio().interpretation.bpm - projectBPM) >= 0.1)
        return false;

    const double sourceDuration = clip.getSourceLength(projectBPM);
    if (sourceDuration <= 0.0 || clip.audio().interpretation.totalBeats <= 0.0)
        return true;

    const double projectDefaultBeats = sourceDuration * projectBPM / 60.0;
    return std::abs(clip.audio().interpretation.totalBeats - projectDefaultBeats) < 0.01;
}

bool seedSourceMetadataFromCachedDetection(ClipInfo& clip, double projectBPM) {
    if (!clip.isAudio() || clip.audio().source.filePath.isEmpty() ||
        !sourceInterpretationBpmLooksDefaulted(clip, projectBPM)) {
        return false;
    }

    auto& thumbs = AudioThumbnailManager::getInstance();
    double cachedBPM = thumbs.getCachedBPM(clip.audio().source.filePath);
    if (cachedBPM <= 0.0)
        return false;

    clip.audio().interpretation.bpm = cachedBPM;
    double fileDuration = 0.0;
    if (juce::File(clip.audio().source.filePath).existsAsFile()) {
        if (auto* thumbnail = thumbs.getThumbnail(clip.audio().source.filePath)) {
            fileDuration = thumbnail->getTotalLength();
        }
    }
    if (fileDuration <= 0.0)
        fileDuration = clip.getSourceLength(projectBPM);
    if (fileDuration > 0.0) {
        if (clip.audio().source.durationSeconds <= 0.0)
            clip.audio().source.durationSeconds = fileDuration;
        clip.audio().interpretation.totalBeats = fileDuration * cachedBPM / 60.0;
    }

    return true;
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

juce::File externalEditFileForClip(const ClipInfo& clip, const juce::File& editsDir,
                                   const juce::File& sourceFile) {
    auto safeName = juce::File::createLegalFileName(clip.name);
    if (safeName.isEmpty()) {
        safeName = sourceFile.getFileNameWithoutExtension();
    }
    if (safeName.isEmpty()) {
        safeName = "audio_clip";
    }
    return editsDir.getNonexistentChildFile(safeName, sourceFile.getFileExtension(), false);
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

        const auto path = file.getFullPathName();
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
    clip.audio().source.filePath = audioFilePath;
    // Don't seed source.durationSeconds from the clip's timeline length —
    // they're different quantities. ClipSynchronizer's setSourceMetadata
    // populates this from TE's loopInfo once the engine clip exists; readers
    // (thumbnail renderer, inspector panels) fall back to the
    // AudioThumbnailManager when the field is still zero.
    clip.offset = 0.0;
    clip.speedRatio = 1.0;

    const double bpm = isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();

    clip.setPlacementBeats(startBeats, lengthBeats);
    clip.deriveTimesFromBeats(bpm);

    // Set loopStart to offset (0), loopLength to the clip's source extent
    clip.loopStart = 0.0;
    clip.setLoopLengthFromTimeline(clip.getTimelineLength(bpm));

    // Scanner output in the media DB is a hint, but user-saved source
    // interpretation is explicit library metadata and should restore when the
    // same file is imported again.
    std::optional<magda::media::EffectiveMetadata> savedMetadata;
    if (audioFilePath.isNotEmpty() && juce::File(audioFilePath).existsAsFile()) {
        savedMetadata = magda::media::getUserMetadataForFile(
            std::filesystem::path(audioFilePath.toStdString()));
        if (savedMetadata) {
            if (savedMetadata->bpm && isValidBpm(*savedMetadata->bpm)) {
                clip.audio().interpretation.bpm = *savedMetadata->bpm;
            }
            if (savedMetadata->totalBeats && *savedMetadata->totalBeats > 0.0) {
                clip.audio().interpretation.totalBeats = *savedMetadata->totalBeats;
                clip.audio().interpretation.totalBeatsLocked = true;
            }
            if (savedMetadata->keyRoot && !savedMetadata->keyRoot->empty()) {
                clip.audio().interpretation.keyRoot = *savedMetadata->keyRoot;
            }
            if (savedMetadata->keyScale && !savedMetadata->keyScale->empty()) {
                clip.audio().interpretation.keyScale = *savedMetadata->keyScale;
            }
        }
        const auto savedMarkers = magda::media::getUserWarpMarkersForFile(
            std::filesystem::path(audioFilePath.toStdString()));
        if (savedMarkers) {
            clip.warpMarkers.clear();
            clip.warpMarkers.reserve(savedMarkers->size());
            for (const auto& marker : *savedMarkers) {
                clip.warpMarkers.push_back({marker.sourceSec, marker.beat});
            }
            clip.warpEnabled = true;
        }
    }

    if (view == ClipView::Arrangement) {
        clips_[clip.id] = clip;
    } else {
        // Session clips loop by default and follow project tempo
        clip.loopEnabled = true;
        clip.autoTempo = true;
        // Source loop beats live in the source-beat domain. Use 0 as "full
        // source" until Tracktion loopInfo populates source interpretation metadata.
        clip.loopLengthBeats = 0.0;
        clips_[clip.id] = clip;
    }

    if (savedMetadata && savedMetadata->beatMode) {
        auto& savedClip = clips_[clip.id];
        savedClip.autoTempo = *savedMetadata->beatMode;
        if (savedClip.autoTempo) {
            savedClip.loopEnabled = true;
            savedClip.analogPitch = false;
            savedClip.speedRatio = 1.0;
            if (savedClip.audio().interpretation.totalBeats > 0.0 &&
                savedClip.loopLengthBeats <= 0.0) {
                savedClip.loopLengthBeats = savedClip.audio().interpretation.totalBeats;
            }
            savedClip.deriveTimesFromBeats(bpm);
        }
    }

    addToSessionSlotIndex(clips_[clip.id]);
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::ResolveOverlaps)
        resolveOverlaps(clip.id);
    notifyClipsChanged();

    // Tracktion loopInfo is authoritative when it carries real file metadata,
    // but it can also report project-default values for freshly inserted clips.
    // Run audio analysis as a fallback and let it replace only unset/defaulted
    // source interpretation values. Session-only.
    if (view == ClipView::Session && audioFilePath.isNotEmpty() &&
        juce::File(audioFilePath).existsAsFile()) {
        ClipId cid = clip.id;
        const double creationProjectBPM = bpm;
        auto applyDetectedBPM = [cid, creationProjectBPM](double detectedBPM) {
            if (detectedBPM <= 0.0)
                return;

            auto& mgr = ClipManager::getInstance();
            auto* c = mgr.getClip(cid);
            if (!c || !sourceInterpretationBpmLooksDefaulted(*c, creationProjectBPM))
                return;

            double fileDuration = c->audio().source.durationSeconds;
            if (auto* thumb =
                    AudioThumbnailManager::getInstance().getThumbnail(c->audio().source.filePath)) {
                if (thumb->getTotalLength() > 0.0)
                    fileDuration = thumb->getTotalLength();
            }
            if (fileDuration <= 0.0)
                fileDuration = c->getSourceLength(creationProjectBPM);

            AudioClipBeatsUpdate u;
            u.interpretationBpm = detectedBPM;
            if (fileDuration > 0.0) {
                u.sourceDurationSeconds = fileDuration;
                const double srcBeats = fileDuration * detectedBPM / 60.0;
                u.interpretationTotalBeats = srcBeats;
                if (c->autoTempo && c->loopLengthBeats <= 0.0)
                    u.loopLengthBeats = srcBeats;
            }

            double live = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
            mgr.applyAudioClipBeats(cid, u, live);
        };

        auto& thumbs = AudioThumbnailManager::getInstance();
        const double cachedBPM = thumbs.getCachedBPM(audioFilePath);
        if (cachedBPM > 0.0) {
            applyDetectedBPM(cachedBPM);
        } else {
            thumbs.requestBPMDetection(audioFilePath, [applyDetectedBPM](double detectedBPM) {
                if (detectedBPM <= 0.0)
                    return;
                applyDetectedBPM(detectedBPM);
            });
        }
    }

    return clip.id;
}

ClipId ClipManager::createAudioClip(TrackId trackId, double startTime, double length,
                                    const juce::String& audioFilePath, ClipView view,
                                    double projectBPM, ClipOverlapPolicy overlapPolicy) {
    const double bpm = isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();
    return createAudioClipBeats(trackId, startTime * bpm / 60.0, length * bpm / 60.0, audioFilePath,
                                view, bpm, overlapPolicy);
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
    clip.name = generateClipName(ClipType::MIDI);
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
        clip.loopLength = clip.getTimelineLength(tempo);
        clips_[clip.id] = clip;
    }

    addToSessionSlotIndex(clips_[clip.id]);
    if (view == ClipView::Arrangement && overlapPolicy == ClipOverlapPolicy::ResolveOverlaps)
        resolveOverlaps(clip.id);
    notifyClipsChanged();

    return clip.id;
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
    clips_.erase(it);
    notifyClipsChanged();
}

void ClipManager::restoreClip(const ClipInfo& clipInfo) {
    if (clips_.count(clipInfo.id))
        return;

    clips_[clipInfo.id] = clipInfo;
    addToSessionSlotIndex(clips_[clipInfo.id]);

    // Ensure nextClipId_ is beyond any restored clip IDs
    if (clipInfo.id >= nextClipId_) {
        nextClipId_ = clipInfo.id + 1;
    }

    notifyClipsChanged();
}

void ClipManager::forceNotifyClipsChanged() {
    notifyClipsChanged();
}

void ClipManager::forceNotifyClipPropertyChanged(ClipId clipId) {
    notifyClipPropertyChanged(clipId);
}

void ClipManager::forceNotifyMultipleClipPropertiesChanged(const std::vector<ClipId>& clipIds) {
    if (clipIds.empty())
        return;
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
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

    const juce::File sourceFile(clip->audio().source.filePath);
    if (!sourceFile.existsAsFile()) {
        errorMessage = "The clip source file could not be found.";
        return false;
    }

    auto editsDir = ProjectManager::getInstance().getExternalEditsDirectory();
    if (editsDir == juce::File() || !editsDir.createDirectory()) {
        errorMessage = "Could not create the project external-edits folder.";
        return false;
    }

    const auto editFile = externalEditFileForClip(*clip, editsDir, sourceFile);
    if (!sourceFile.copyFileTo(editFile) || !editFile.existsAsFile()) {
        errorMessage = "Could not copy the clip source into the project external-edits folder.";
        return false;
    }

    if (!launchExternalAudioEditor(editor, editFile)) {
        editFile.deleteFile();
        errorMessage = "Could not launch the configured external audio editor.";
        return false;
    }

    const auto oldPath = clip->audio().source.filePath;
    clip->audio().source.filePath = editFile.getFullPathName();
    AudioThumbnailManager::getInstance().invalidateFile(oldPath);
    AudioThumbnailManager::getInstance().invalidateFile(clip->audio().source.filePath);
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
    newClip.name = original->name + " Copy";

    if (newClip.view == ClipView::Arrangement) {
        // Beats are authoritative for clip positioning. Compute the new
        // position fully in beats, then derive seconds at the boundary.
        // The previous code only did this for MIDI; the audio path left the
        // duplicate's startBeats equal to the original's, so any later
        // beats-driven re-derivation snapped the duplicate back on top of
        // the original.
        double bpm = currentProjectTempoOrDefault();
        double clipLengthBeats = original->placement.lengthBeats;
        newClip.setPlacementBeats(original->placement.startBeat + clipLengthBeats, clipLengthBeats);
        newClip.deriveTimesFromBeats(bpm);
    } else {
        // Session clips always loop
        const double bpm = currentProjectTempoOrDefault();
        ClipOperations::setBeatPlacement(newClip, 0.0, newClip.placement.lengthBeats, bpm);
        newClip.loopEnabled = true;
        newClip.sceneIndex = -1;
    }
    clips_[newClip.id] = newClip;
    addToSessionSlotIndex(clips_[newClip.id]);

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
    newClip.name = original->name + " Copy";

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

void ClipManager::resetLoopedClipLength(ClipInfo& clip) {
    if (!clip.loopEnabled)
        return;

    if (clip.loopLengthBeats > 0.0) {
        const double bpm = currentProjectTempoOrDefault();
        ClipOperations::setBeatPlacement(clip, clip.placement.startBeat, clip.loopLengthBeats, bpm);
    } else if (clip.loopLength > 0.0) {
        const double bpm = currentProjectTempoOrDefault();
        ClipOperations::setTimelinePlacement(clip, clip.getTimelineStart(bpm),
                                             clip.sourceToTimeline(clip.loopLength), bpm);
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
        if (fromStart) {
            ClipOperations::resizeContainerFromLeft(*clip, newLength, bpm);
            // Non-loop mode: keep loopStart synced to offset
            if (!clip->loopEnabled && clip->isAudio()) {
                clip->loopStart = clip->offset;
            }
        } else {
            ClipOperations::resizeContainerFromRight(*clip, newLength, bpm);

            // In non-loop mode, clip length defines the source region — keep loopLength in sync
            if (!clip->loopEnabled && clip->isAudio()) {
                clip->loopLength = clip->timelineToSource(clip->getTimelineLength(bpm));
                if (clip->autoTempo && clip->audio().interpretation.bpm > 0.0) {
                    clip->loopLengthBeats =
                        clip->loopLength * clip->audio().interpretation.bpm / 60.0;
                }
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

    // Adjust offset for right clip (TE-aligned: offset is start position in source)
    if (rightClip.isAudio()) {
        // In autoTempo/warp mode, speedRatio is 1.0 but actual stretch is projectBPM/source
        // interpretation BPM. Use the tempo ratio to convert timeline seconds to source seconds.
        if (clip->audio().interpretation.bpm > 0.0) {
            double deltaBeats = leftLengthBeats;
            rightClip.offsetBeats += deltaBeats;
            rightClip.offset = rightClip.offsetBeats * 60.0 / clip->audio().interpretation.bpm;
        } else {
            rightClip.offset += leftLength * clip->speedRatio;
        }
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

            clip->midiNotes = leftNotes;
            rightClip.midiNotes = rightNotes;
        }
    }

    // Resize original clip to be left half
    ClipOperations::setBeatPlacement(*clip, clip->placement.startBeat, leftLengthBeats, bpm);
    clip->name = clip->name + " L";

    // Sync loop region after split
    if (clip->loopEnabled) {
        const bool autoTempoAudio =
            clip->isAudio() && clip->autoTempo && clip->audio().interpretation.bpm > 0.0;

        if (autoTempoAudio) {
            // TE auto-tempo arranger clips need the source loop range to stay in
            // the original source-beat domain. The right split's offsetBeats
            // carries the playback phase; truncating loopStartBeats to the
            // split point makes the right-side WaveNode render silence.
            const double srcBpm = clip->audio().interpretation.bpm;
            clip->loopStart = clip->loopStartBeats * 60.0 / srcBpm;
            clip->loopLength = clip->loopLengthBeats * 60.0 / srcBpm;
            rightClip.loopStart = rightClip.loopStartBeats * 60.0 / srcBpm;
            rightClip.loopLength = rightClip.loopLengthBeats * 60.0 / srcBpm;
        } else {
            // Looped clip: if the loop region is longer than the new clip length,
            // truncate it so each half only loops over its own portion.
            // Left clip
            double leftLenBeats = leftLengthBeats;
            if (clip->loopLengthBeats > leftLenBeats) {
                clip->loopLengthBeats = leftLenBeats;
                if (clip->isAudio()) {
                    double srcBpm = clip->audio().interpretation.bpm > 0.0
                                        ? clip->audio().interpretation.bpm
                                        : bpm;
                    clip->loopLength = clip->loopLengthBeats / srcBpm * 60.0;
                }
            }

            // Right clip
            double rightLenBeats = rightLengthBeats;
            if (rightClip.loopLengthBeats > rightLenBeats) {
                rightClip.loopLengthBeats = rightLenBeats;
                if (rightClip.isAudio()) {
                    double srcBpm = rightClip.audio().interpretation.bpm > 0.0
                                        ? rightClip.audio().interpretation.bpm
                                        : bpm;
                    if (rightClip.autoTempo && rightClip.audio().interpretation.bpm > 0.0) {
                        rightClip.loopStartBeats = rightClip.offsetBeats;
                        rightClip.loopStart =
                            rightClip.loopStartBeats * 60.0 / rightClip.audio().interpretation.bpm;
                    } else {
                        rightClip.loopStart = rightClip.offset;
                        rightClip.loopStartBeats = rightClip.loopStart * srcBpm / 60.0;
                    }
                    rightClip.loopLength = rightClip.loopLengthBeats / srcBpm * 60.0;
                }
            }
        }
    } else if (clip->isAudio()) {
        // Non-looped audio: sync loopStart/loopLength to actual source extent
        // Left clip: loopStart stays at original value, loopLength shrinks
        clip->loopLength = clip->timelineToSource(clip->getTimelineLength(bpm));
        clip->loopLengthBeats =
            clip->loopLength *
            (clip->audio().interpretation.bpm > 0.0 ? clip->audio().interpretation.bpm : bpm) /
            60.0;

        // Right clip: loopStart must match offset so TE's loop range covers the
        // correct source region (otherwise offset falls outside the loop range,
        // causing TE to wrap and produce doubled transients at the split point)
        if (rightClip.autoTempo && rightClip.audio().interpretation.bpm > 0.0) {
            rightClip.loopStartBeats = rightClip.offsetBeats;
            rightClip.loopStart =
                rightClip.loopStartBeats * 60.0 / rightClip.audio().interpretation.bpm;
        } else {
            rightClip.loopStart = rightClip.offset;
        }
        rightClip.loopLength = rightClip.timelineToSource(rightClip.getTimelineLength(bpm));
        double srcBpm =
            rightClip.audio().interpretation.bpm > 0.0 ? rightClip.audio().interpretation.bpm : bpm;
        if (!rightClip.autoTempo)
            rightClip.loopStartBeats = rightClip.loopStart * srcBpm / 60.0;
        rightClip.loopLengthBeats = rightClip.loopLength * srcBpm / 60.0;
    }

    // Time-stretched clips (autoTempo/warp): add small anti-click fades at the
    // split boundary.  The stretcher's overlapping analysis windows bleed audio
    // from beyond the boundary, which sounds like a doubled transient.  A short
    // fade masks this startup/shutdown artifact without being audible.
    if (clip->isAudio()) {
        constexpr double kSplitFadeSeconds = 0.005;  // 5 ms
        clip->fadeOut = kSplitFadeSeconds;
        rightClip.fadeIn = kSplitFadeSeconds;
    }

    // Add right clip to the clip pool
    clips_[rightClip.id] = rightClip;
    addToSessionSlotIndex(clips_[rightClip.id]);

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
        if (!enabled && clip->autoTempo) {
            notifyClipPropertyChanged(clipId);
            return;
        }
        clip->loopEnabled = enabled;

        // When enabling loop on MIDI clips, capture current length as loop region
        // Populate both beat and seconds fields so all existing code paths work
        if (enabled && clip->isMidi()) {
            double bpm = isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();
            if (clip->loopLengthBeats <= 0.0) {
                clip->loopLengthBeats = clip->getLengthInBeats(bpm);
            }
            clip->loopLength = clip->loopLengthBeats * 60.0 / bpm;
        }

        // When enabling loop on audio clips, transfer offset → loopStart
        // The user's current offset becomes the loop start point (phase resets to 0)
        if (enabled && clip->isAudio() && clip->audio().source.filePath.isNotEmpty()) {
            clip->loopStart = clip->offset;

            // Ensure loopLength is set (preserves source extent in loop mode)
            if (clip->loopLength <= 0.0) {
                const double bpm =
                    isValidBpm(projectBPM) ? projectBPM : currentProjectTempoOrDefault();
                clip->setLoopLengthFromTimeline(clip->getTimelineLength(bpm));
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
        if (!enabled && clip->isAudio() && clip->audio().source.filePath.isNotEmpty()) {
            clip->loopStart = clip->offset;

            double fileDuration = 0.0;
            if (auto* thumbnail = AudioThumbnailManager::getInstance().getThumbnail(
                    clip->audio().source.filePath)) {
                fileDuration = thumbnail->getTotalLength();
            }
            if (fileDuration <= 0.0)
                fileDuration = clip->audio().source.durationSeconds;

            const double speed = clip->speedRatio > 0.0 ? clip->speedRatio : 1.0;
            if (fileDuration > 0.0) {
                const double availableSource = juce::jmax(0.0, fileDuration - clip->offset);
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
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio() && clip->warpEnabled != enabled) {
            clip->warpEnabled = enabled;
            if (enabled)
                clip->analogPitch = false;  // Analog pitch is incompatible with warp
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoTempo(ClipId clipId, bool enabled, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            if (enabled)
                seedSourceMetadataFromCachedDetection(*clip, bpm);

            ClipOperations::setAutoTempo(*clip, enabled, bpm);

            // Ensure time-stretching is enabled when beat mode is on
            if (enabled && clip->timeStretchMode == 0)
                clip->timeStretchMode = 4;  // soundtouchBetter

            // Issue #1157: ClipOperations::setAutoTempo already wrote
            // lengthBeats from clip.length × bpm / 60 (the legitimate
            // one-time conversion at the autoTempo boundary). The previous
            // code then called resizeClip(lengthBeats × 60 / bpm) — a pure
            // beats→seconds→beats round-trip that accumulated FP drift each
            // toggle. Just refresh the seconds cache from beats and notify.
            refreshDerivedSeconds(clipId, bpm);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setOffset(ClipId clipId, double offset) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            // MIDI phase lives in midiOffset (beats) — caller passes beats directly
            clip->midiOffset = juce::jmax(0.0, offset);
        } else {
            if (clip->autoTempo) {
                const double interpBpm =
                    clip->audio().interpretation.bpm > 0.0 ? clip->audio().interpretation.bpm : 0.0;
                const double beats = interpBpm > 0.0 ? offset * interpBpm / 60.0 : 0.0;
                clip->setSourceOffsetBeats(beats, interpBpm);
            } else {
                clip->offset = juce::jmax(0.0, offset);
            }
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLoopPhase(ClipId clipId, double phase) {
    if (auto* clip = getClip(clipId)) {
        const bool loopActive = clip->loopEnabled || clip->autoTempo;
        if (clip->isAudio() && loopActive) {
            if (clip->autoTempo) {
                const double interpBpm =
                    clip->audio().interpretation.bpm > 0.0 ? clip->audio().interpretation.bpm : 0.0;
                // phase is in source seconds; offset = loopStart + phase
                const double offsetSecs = clip->getSourceLoopStart() + phase;
                const double beats = interpBpm > 0.0 ? offsetSecs * interpBpm / 60.0 : 0.0;
                clip->setSourceOffsetBeats(beats, interpBpm);
            } else {
                clip->offset = clip->loopStart + phase;
            }
            sanitizeAudioClip(*clip);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setLoopStart(ClipId clipId, double loopStart, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio() && clip->autoTempo) {
            const double interpBpm =
                clip->audio().interpretation.bpm > 0.0 ? clip->audio().interpretation.bpm : bpm;
            const double beats = interpBpm > 0.0 ? loopStart * interpBpm / 60.0 : 0.0;
            clip->setLoopStartBeats(beats, interpBpm);
            sanitizeAudioClip(*clip);
        } else {
            clip->loopStart = juce::jmax(0.0, loopStart);
            if (clip->isMidi()) {
                const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
                clip->loopStartBeats =
                    projectBpm > 0.0 ? (clip->loopStart * projectBpm) / 60.0 : 0.0;
            }
            if (clip->isAudio())
                sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLoopLength(ClipId clipId, double loopLength, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isMidi()) {
            clip->loopLength = juce::jmax(0.0, loopLength);
            // MIDI: keep loopLengthBeats in sync using project BPM
            const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
            clip->loopLengthBeats = (clip->loopLength * projectBpm) / 60.0;
        } else if (clip->isAudio()) {
            if (clip->autoTempo) {
                const double interpBpm =
                    clip->audio().interpretation.bpm > 0.0 ? clip->audio().interpretation.bpm : bpm;
                const double beats = interpBpm > 0.0 ? loopLength * interpBpm / 60.0 : 0.0;
                clip->setLoopLengthBeats(beats, interpBpm);
            } else {
                clip->loopLength = juce::jmax(0.0, loopLength);
            }
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setMidiLoopStartBeats(ClipId clipId, double loopStartBeats, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (!clip->isMidi())
            return;

        const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
        clip->loopStartBeats = juce::jmax(0.0, loopStartBeats);
        clip->loopStart = projectBpm > 0.0 ? (clip->loopStartBeats * 60.0) / projectBpm : 0.0;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setMidiLoopLengthBeats(ClipId clipId, double loopLengthBeats, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (!clip->isMidi())
            return;

        const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
        clip->loopLengthBeats = juce::jmax(0.0, loopLengthBeats);
        clip->loopLength = projectBpm > 0.0 ? (clip->loopLengthBeats * 60.0) / projectBpm : 0.0;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::relocateLoopRegion(ClipId clipId, double loopStart, double loopLength,
                                     double bpm) {
    if (auto* clip = getClip(clipId)) {
        const double oldLoopStart = clip->getSourceLoopStart();

        if (clip->isAudio() && clip->autoTempo) {
            const double interpBpm =
                clip->audio().interpretation.bpm > 0.0 ? clip->audio().interpretation.bpm : bpm;
            const double startBeats = interpBpm > 0.0 ? loopStart * interpBpm / 60.0 : 0.0;
            const double lenBeats = interpBpm > 0.0 ? loopLength * interpBpm / 60.0 : 0.0;
            clip->setLoopStartBeats(startBeats, interpBpm);
            clip->setLoopLengthBeats(lenBeats, interpBpm);

            // Composite intent: snap offset to new loopStart when it moved
            const bool loopStartMoved = std::abs(clip->loopStart - oldLoopStart) > 1e-9;
            if (loopStartMoved) {
                clip->setSourceOffsetBeats(startBeats, interpBpm);
            }
            sanitizeAudioClip(*clip);
        } else {
            clip->loopStart = juce::jmax(0.0, loopStart);
            clip->loopLength = juce::jmax(0.0, loopLength);

            if (clip->isAudio()) {
                // Composite intent: reset phase to 0 by snapping offset to new loopStart
                const bool loopStartMoved = std::abs(clip->loopStart - oldLoopStart) > 1e-9;
                if (loopStartMoved)
                    clip->offset = clip->loopStart;
                sanitizeAudioClip(*clip);
            } else if (clip->isMidi()) {
                const double projectBpm = isValidBpm(bpm) ? bpm : currentProjectTempoOrDefault();
                clip->loopLengthBeats = (clip->loopLength * projectBpm) / 60.0;
            }
        }

        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLengthBeats(ClipId clipId, double newBeats, double bpm) {
    auto* clip = getClip(clipId);
    if (!clip || !clip->isAudio() || !clip->autoTempo || bpm <= 0.0)
        return;

    // Issue #1157: the beat-length slider edits USER INTENT only — how many
    // timeline beats the clip occupies. Source-file metadata (source interpretation BPM /
    // source interpretation total beats) is NOT touched here. Stretch is determined by TE from
    // (projectBPM / source interpretation BPM) at sync time.
    //
    AudioClipBeatsUpdate u;
    u.lengthBeats = newBeats;

    applyAudioClipBeats(clipId, u, bpm);
}

void ClipManager::recordUserBpm(ClipId clipId, double bpm) {
    if (!isValidBpm(bpm)) {
        return;
    }
    const auto* clip = getClip(clipId);
    if (!clip || !clip->isAudio()) {
        return;
    }
    const auto& filePath = clip->audio().source.filePath;
    if (filePath.isEmpty()) {
        return;
    }
    magda::media::setUserBpmForFile(std::filesystem::path(filePath.toStdString()), bpm);
}

void ClipManager::recordUserKey(ClipId clipId, const std::string& root) {
    const auto* clip = getClip(clipId);
    if (clip == nullptr || !clip->isAudio()) {
        return;
    }
    const auto& filePath = clip->audio().source.filePath;
    if (filePath.isEmpty()) {
        return;
    }
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
        return !clip->midiNotes.empty() || !clip->midiCCData.empty() ||
               !clip->midiPitchBendData.empty();
    }
    if (!clip->isAudio()) {
        return false;
    }
    const auto& filePath = clip->audio().source.filePath;
    if (filePath.isEmpty()) {
        return false;
    }
    return juce::File(filePath).existsAsFile();
}

bool ClipManager::saveClipToLibrary(ClipId clipId,
                                    std::optional<std::vector<ClipInfo::WarpMarker>> warpMarkers) {
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

        const juce::File midiDir(juce::String(ctx.midiClipsDir().string()));
        if (!midiDir.createDirectory()) {
            return false;
        }

        const auto outFile = midiLibraryFileForClip(*clip, midiDir);
        const double tempo = currentProjectTempoOrDefault();
        if (!magda::daw::MidiFileWriter::writeToFile(outFile, clip->midiNotes, clip->midiCCData,
                                                     clip->midiPitchBendData, tempo, clip->name)) {
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

    if (!clip->isAudio()) {
        return false;
    }
    const auto& filePath = clip->audio().source.filePath;
    if (filePath.isEmpty()) {
        return false;
    }
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

    std::optional<double> bpm;
    if (isValidBpm(clip->audio().interpretation.bpm)) {
        bpm = clip->audio().interpretation.bpm;
    }
    std::optional<double> totalBeats;
    if (clip->audio().interpretation.totalBeats > 0.0) {
        totalBeats = clip->audio().interpretation.totalBeats;
    }
    const std::optional<bool> beatMode = clip->autoTempo;

    std::optional<std::string> keyRoot;
    if (!clip->audio().interpretation.keyRoot.empty()) {
        keyRoot = clip->audio().interpretation.keyRoot;
    }

    std::optional<std::vector<magda::media::WarpMarkerMetadata>> mediaMarkers;
    if (clip->warpEnabled) {
        const auto& sourceMarkers = warpMarkers ? *warpMarkers : clip->warpMarkers;
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

void ClipManager::applyAudioClipBeats(ClipId clipId, const AudioClipBeatsUpdate& update,
                                      double projectBPM) {
    auto* clip = getClip(clipId);
    if (!clip || !clip->isAudio() || !clip->autoTempo)
        return;

    // (1) Source interpretation metadata. BPM and total beats describe the
    // same fixed-duration source, so inspector edits may update both together.
    if (update.interpretationBpm)
        clip->audio().interpretation.bpm = juce::jmax(0.0, *update.interpretationBpm);
    if (update.interpretationTotalBeats) {
        clip->audio().interpretation.totalBeats = juce::jmax(0.0, *update.interpretationTotalBeats);
        if (update.lockInterpretationTotalBeats)
            clip->audio().interpretation.totalBeatsLocked = true;
    }
    if (update.sourceDurationSeconds && clip->audio().source.durationSeconds <= 0.0)
        clip->audio().source.durationSeconds = juce::jmax(0.0, *update.sourceDurationSeconds);
    if (clip->audio().source.durationSeconds <= 0.0 && clip->audio().interpretation.bpm > 0.0 &&
        clip->audio().interpretation.totalBeats > 0.0) {
        clip->audio().source.durationSeconds =
            clip->audio().interpretation.totalBeats * 60.0 / clip->audio().interpretation.bpm;
    }

    // (2) User-intent fields — beat-domain canonicals.
    if (update.lengthBeats) {
        double minBeats =
            isValidBpm(projectBPM) ? (ClipInfo::MIN_CLIP_LENGTH * projectBPM / 60.0) : 0.0;
        clip->setPlacementBeats(clip->placement.startBeat,
                                juce::jmax(minBeats, *update.lengthBeats));
    }
    if (update.loopStartBeats)
        clip->loopStartBeats = juce::jmax(0.0, *update.loopStartBeats);
    if (update.loopLengthBeats) {
        clip->loopLengthBeats = juce::jmax(0.0, *update.loopLengthBeats);
    }
    if (update.offsetBeats)
        clip->offsetBeats = juce::jmax(0.0, *update.offsetBeats);
    if (update.startBeats)
        clip->setPlacementBeats(juce::jmax(0.0, *update.startBeats), clip->placement.lengthBeats);

    // If the source interpretation changed, preserve the selected source region in seconds.
    // loopStartBeats/loopLengthBeats/offsetBeats are derived from source seconds unless the
    // current update explicitly edits those beat-domain fields.
    if ((update.interpretationBpm || update.interpretationTotalBeats) &&
        clip->audio().interpretation.bpm > 0.0) {
        double sourceBpm = clip->audio().interpretation.bpm;
        if (!update.loopStartBeats)
            clip->loopStartBeats = clip->loopStart * sourceBpm / 60.0;
        if (!update.loopLengthBeats && clip->loopLength > 0.0)
            clip->loopLengthBeats = clip->loopLength * sourceBpm / 60.0;
        if (!update.offsetBeats)
            clip->offsetBeats = clip->offset * sourceBpm / 60.0;
    }

    // (3) Recompute the seconds cache from beats atomically.
    refreshDerivedSeconds(clipId, projectBPM);

    notifyClipPropertyChanged(clipId);
}

void ClipManager::refreshDerivedSeconds(ClipId clipId, double projectBPM) {
    auto* clip = getClip(clipId);
    if (!clip)
        return;

    // TE requires speedRatio == 1.0 in autoTempo mode.
    if (clip->autoTempo)
        clip->speedRatio = 1.0;

    // Timeline-domain seconds (length, startTime): depend on PROJECT BPM.
    if (isValidBpm(projectBPM)) {
        if (clip->placement.lengthBeats > 0.0)
            clip->length = clip->placement.lengthBeats * 60.0 / projectBPM;
        clip->startTime = clip->placement.startBeat * 60.0 / projectBPM;
        clip->startBeats = clip->placement.startBeat;
        clip->lengthBeats = clip->placement.lengthBeats;
    }

    // Source-domain seconds (offset, loopStart, loopLength) for autoTempo
    // audio: depend on SOURCE BPM (a property of the file, not the project).
    // For MIDI clips loopLengthBeats lives in the project-beat domain, so
    // loopLength uses projectBPM. When source interpretation BPM is unknown (detection
    // pending), leave the source-domain seconds alone — readers fall back to
    // the lengthBeats × 60 / projectBPM path.
    if (clip->isAudio() && clip->autoTempo && clip->audio().interpretation.bpm > 0.0) {
        clip->offset = clip->offsetBeats * 60.0 / clip->audio().interpretation.bpm;
        clip->loopStart = clip->loopStartBeats * 60.0 / clip->audio().interpretation.bpm;
        if (clip->loopLengthBeats > 0.0)
            clip->loopLength = clip->loopLengthBeats * 60.0 / clip->audio().interpretation.bpm;
    } else if (clip->isMidi() && isValidBpm(projectBPM) && clip->loopLengthBeats > 0.0) {
        clip->loopLength = clip->loopLengthBeats * 60.0 / projectBPM;
    }
}

void ClipManager::setSpeedRatio(ClipId clipId, double speedRatio) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            const double bpm = currentProjectTempoOrDefault();
            double oldSourceExtent = clip->timelineToSource(clip->getTimelineLength(bpm));
            clip->speedRatio = juce::jlimit(ClipOperations::MIN_SPEED_RATIO,
                                            ClipOperations::MAX_SPEED_RATIO, speedRatio);
            double newSourceExtent = clip->timelineToSource(clip->getTimelineLength(bpm));

            // Keep loopLength in sync when the loop covers the full source extent
            // (non-looped clips, or looped clips where the loop wasn't user-shortened)
            if (!clip->loopEnabled || std::abs(clip->loopLength - oldSourceExtent) < 0.001) {
                clip->loopLength = newSourceExtent;
            }
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setTimeStretchMode(ClipId clipId, int mode) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->timeStretchMode = mode;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Pitch
// ============================================================================

void ClipManager::setAutoPitch(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->autoPitch = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAnalogPitch(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->analogPitch = enabled;
            if (enabled && !clip->autoTempo && !clip->warpEnabled) {
                // Analog pitch is sample-rate style playback: pitch and speed
                // are the same factor, and the selected source span stays fixed.
                const double pitchFactor = std::pow(2.0, clip->pitchChange / 12.0);
                const double bpm = currentProjectTempoOrDefault();
                const double sourceContent = clip->timelineToSource(clip->getTimelineLength(bpm));
                clip->speedRatio = pitchFactor;
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
        if (clip->isAudio()) {
            clip->autoPitchMode = juce::jlimit(0, 2, mode);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setPitchChange(ClipId clipId, float semitones) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            const double bpm = currentProjectTempoOrDefault();
            const double sourceContent = clip->timelineToSource(clip->getTimelineLength(bpm));
            clip->pitchChange = juce::jlimit(-48.0f, 48.0f, semitones);

            if (clip->isAnalogPitchActive()) {
                const double newFactor = std::pow(2.0, clip->pitchChange / 12.0);
                clip->speedRatio = newFactor;
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
        if (clip->isAudio()) {
            clip->transpose = juce::jlimit(-24, 24, semitones);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Beat Detection
// ============================================================================

void ClipManager::setAutoDetectBeats(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->autoDetectBeats = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setBeatSensitivity(ClipId clipId, float sensitivity) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->beatSensitivity = juce::jlimit(0.0f, 1.0f, sensitivity);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Playback
// ============================================================================

void ClipManager::setIsReversed(ClipId clipId, bool reversed) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->isReversed = reversed;
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
        if (clip->isAudio()) {
            clip->fadeIn = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOut(ClipId clipId, double seconds) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->fadeOut = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->fadeInType = juce::jlimit(1, 4, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->fadeOutType = juce::jlimit(1, 4, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->fadeInBehaviour = juce::jlimit(0, 1, behaviour);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->fadeOutBehaviour = juce::jlimit(0, 1, behaviour);
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

void ClipManager::setLaunchFadeSamples(ClipId clipId, int samples) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->launchFadeSamples = juce::jlimit(0, 16384, samples);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Channels
// ============================================================================

void ClipManager::setLeftChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->leftChannelActive = active;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setRightChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (clip->isAudio()) {
            clip->rightChannelActive = active;
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
        clip->gridAutoGrid = autoGrid;
        clip->gridNumerator = numerator;
        clip->gridDenominator = denominator;
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

ClipInfo* ClipManager::getClip(ClipId clipId) {
    auto it = clips_.find(clipId);
    return (it != clips_.end()) ? &it->second : nullptr;
}

const ClipInfo* ClipManager::getClip(ClipId clipId) const {
    auto it = clips_.find(clipId);
    return (it != clips_.end()) ? &it->second : nullptr;
}

std::vector<ClipInfo> ClipManager::getArrangementClips() const {
    std::vector<ClipInfo> result;
    result.reserve(clips_.size());
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Arrangement)
            result.push_back(clip);
    }
    return result;
}

std::vector<ClipInfo> ClipManager::getSessionClips() const {
    std::vector<ClipInfo> result;
    result.reserve(clips_.size());
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Session)
            result.push_back(clip);
    }
    return result;
}

std::vector<ClipInfo> ClipManager::getClips() const {
    std::vector<ClipInfo> result;
    result.reserve(clips_.size());
    for (const auto& [id, clip] : clips_)
        result.push_back(clip);
    return result;
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId) const {
    std::vector<ClipId> result;
    for (const auto& [id, clip] : clips_) {
        if (clip.trackId == trackId)
            result.push_back(clip.id);
    }
    std::sort(result.begin(), result.end(), [this](ClipId a, ClipId b) {
        const auto* clipA = getClip(a);
        const auto* clipB = getClip(b);
        const double bpm = currentProjectTempoOrDefault();
        return clipA && clipB && clipA->getTimelineStart(bpm) < clipB->getTimelineStart(bpm);
    });
    return result;
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId, ClipView view) const {
    std::vector<ClipId> result;
    for (const auto& [id, clip] : clips_) {
        if (clip.trackId == trackId && clip.view == view)
            result.push_back(clip.id);
    }
    if (view == ClipView::Arrangement) {
        std::sort(result.begin(), result.end(), [this](ClipId a, ClipId b) {
            const auto* clipA = getClip(a);
            const auto* clipB = getClip(b);
            const double bpm = currentProjectTempoOrDefault();
            return clipA && clipB && clipA->getTimelineStart(bpm) < clipB->getTimelineStart(bpm);
        });
    }
    return result;
}

ClipId ClipManager::getClipAtPosition(TrackId trackId, double time) const {
    const double bpm = currentProjectTempoOrDefault();
    for (const auto& [id, clip] : clips_) {
        const double clipStart = clip.getTimelineStart(bpm);
        const double clipEnd = clip.getTimelineEnd(bpm);
        if (clip.view == ClipView::Arrangement && clip.trackId == trackId && time >= clipStart &&
            time < clipEnd) {
            return clip.id;
        }
    }
    return INVALID_CLIP_ID;
}

std::vector<ClipId> ClipManager::getClipsInRange(TrackId trackId, double startTime,
                                                 double endTime) const {
    std::vector<ClipId> result;
    const double bpm = currentProjectTempoOrDefault();
    for (const auto& [id, clip] : clips_) {
        const double clipStart = clip.getTimelineStart(bpm);
        const double clipEnd = clip.getTimelineEnd(bpm);
        if (clip.view == ClipView::Arrangement && clip.trackId == trackId && clipStart < endTime &&
            clipEnd > startTime) {
            result.push_back(clip.id);
        }
    }
    return result;
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
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void ClipManager::removeListener(ClipManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

// ============================================================================
// Project Management
// ============================================================================

void ClipManager::clearAllClips() {
    clips_.clear();
    sessionSlotIndex_.clear();
    selectedClipId_ = INVALID_CLIP_ID;
    nextClipId_ = 1;
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
    const auto* dominant = getClip(dominantClipId);
    if (!dominant || dominant->view != ClipView::Arrangement) {
        return;
    }

    const double dStartB = dominant->placement.startBeat;
    const double dEndB = dStartB + dominant->placement.lengthBeats;
    if (!(dEndB > dStartB))
        return;
    const TrackId trackId = dominant->trackId;
    const double bpm = currentProjectTempoOrDefault();

    // Collect IDs to delete and clips to resize (avoid iterator invalidation)
    std::vector<ClipId> toDelete;

    struct ResizeOp {
        ClipId id;
        double newLengthBeats;
        bool fromLeft;  // true = trim left edge (move start forward)
    };
    std::vector<ResizeOp> toResize;

    for (const auto& [cid, clip] : clips_) {
        if (clip.view != ClipView::Arrangement || clip.id == dominantClipId ||
            clip.trackId != trackId) {
            continue;
        }

        const double cStartB = clip.placement.startBeat;
        const double cEndB = cStartB + clip.placement.lengthBeats;

        // Check for overlap (beat-domain — the seconds mirrors can be stale after
        // BPM changes or beat-mode edits, so never use them here).
        if (cStartB >= dEndB || cEndB <= dStartB) {
            continue;
        }

        if (cStartB >= dStartB && cEndB <= dEndB) {
            // C fully covered by D → delete
            toDelete.push_back(clip.id);
        } else if (cStartB < dStartB && cEndB <= dEndB) {
            // C overlaps from left → trim right edge to dStartB
            toResize.push_back({clip.id, dStartB - cStartB, false});
        } else if (cStartB >= dStartB && cEndB > dEndB) {
            // C overlaps from right → trim left edge to dEndB
            toResize.push_back({clip.id, cEndB - dEndB, true});
        } else if (cStartB < dStartB && cEndB > dEndB) {
            // C fully contains D → keep left portion, trim right edge to dStartB
            toResize.push_back({clip.id, dStartB - cStartB, false});
        }
    }

    for (auto id : toDelete) {
        deleteClip(id);
    }

    for (const auto& op : toResize) {
        if (auto* clip = getClip(op.id)) {
            // ClipOperations::resizeContainerFromLeft/Right take seconds + bpm
            // and re-derive the beat placement internally. Convert from the
            // beat-domain target length here so the seconds boundary stays
            // confined to the resize helpers.
            const double newLengthSeconds = op.newLengthBeats * 60.0 / bpm;
            if (op.fromLeft) {
                ClipOperations::resizeContainerFromLeft(*clip, newLengthSeconds, bpm);
            } else {
                ClipOperations::resizeContainerFromRight(*clip, newLengthSeconds, bpm);
            }
            notifyClipPropertyChanged(op.id);
        }
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

void ClipManager::notifyClipsChanged() {
    // Make a copy because listeners may be removed during iteration
    // (e.g., ClipComponent destroyed when TrackContentPanel rebuilds)
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipsChanged();
        }
    }
}

void ClipManager::notifyClipPropertyChanged(ClipId clipId) {
    if (batchDepth_ > 0) {
        // Coalesce: record once, fire at end of outermost batch.
        if (std::find(batchedClipIds_.begin(), batchedClipIds_.end(), clipId) ==
            batchedClipIds_.end()) {
            batchedClipIds_.push_back(clipId);
        }
        return;
    }
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPropertyChanged(clipId);
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
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPropertiesChanged(ids);
        }
    }
}

void ClipManager::notifyClipSelectionChanged(ClipId clipId) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipSelectionChanged(clipId);
        }
    }
}

void ClipManager::notifyClipPlaybackStateChanged(ClipId clipId) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPlaybackStateChanged(clipId);
        }
    }
}

void ClipManager::notifyClipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPlaybackRequested(clipId, request);
        }
    }
}

void ClipManager::notifyClipDragPreview(ClipId clipId, double previewStartTime,
                                        double previewLength) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipDragPreview(clipId, previewStartTime, previewLength);
        }
    }
}

juce::String ClipManager::generateClipName(ClipType type) const {
    int count = 1;
    for (const auto& [id, clip] : clips_) {
        if (clip.getType() == type)
            count++;
    }

    if (type == ClipType::Audio) {
        return "Audio " + juce::String(count);
    } else {
        return "MIDI " + juce::String(count);
    }
}

void ClipManager::sanitizeAudioClip(ClipInfo& clip) {
    if (!clip.isAudio() || clip.audio().source.filePath.isEmpty())
        return;

    auto* thumbnail =
        AudioThumbnailManager::getInstance().getThumbnail(clip.audio().source.filePath);
    double fileDuration = thumbnail ? thumbnail->getTotalLength() : 0.0;
    if (fileDuration <= 0.0)
        fileDuration = clip.audio().source.durationSeconds;
    if (fileDuration <= 0.0)
        return;

    clip.loopStart = juce::jlimit(0.0, fileDuration, clip.loopStart);

    const double availableFromLoop = fileDuration - clip.loopStart;
    if (clip.loopLength > availableFromLoop) {
        const double oldLoopLength = clip.loopLength;
        clip.loopLength = juce::jmax(0.0, availableFromLoop);
        if (clip.autoTempo && oldLoopLength > 0.0) {
            clip.loopLengthBeats *= clip.loopLength / oldLoopLength;
        }
    }

    clip.offset = juce::jlimit(0.0, fileDuration, clip.offset);

    if (!clip.loopEnabled && !clip.autoTempo) {
        const double bpm = currentProjectTempoOrDefault();
        const double currentLength = clip.getTimelineLength(bpm);
        const double available = fileDuration - clip.offset;
        const double maxLength = available / clip.speedRatio;
        if (currentLength > maxLength) {
            ClipOperations::setTimelinePlacement(clip, clip.getTimelineStart(bpm),
                                                 juce::jmax(ClipInfo::MIN_CLIP_LENGTH, maxLength),
                                                 bpm);
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

    // Find the earliest start time to use as reference
    clipboardReferenceTime_ = std::numeric_limits<double>::max();
    const double bpm = currentProjectTempoOrDefault();
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboardReferenceTime_ =
                std::min(clipboardReferenceTime_, clip->getTimelineStart(bpm));
        }
    }

    // Copy clips maintaining relative positions
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboard_.push_back(*clip);
        }
    }

    DBG("CLIPBOARD: Copied " << clipboard_.size() << " clip(s)");
}

void ClipManager::copyTimeRangeToClipboard(double startTime, double endTime,
                                           const std::vector<TrackId>& trackIds, double tempoBPM) {
    clipboard_.clear();
    clipboardReferenceTime_ = startTime;

    if (startTime >= endTime)
        return;

    for (const auto& [id, clip] : clips_) {
        if (clip.view != ClipView::Arrangement)
            continue;
        // Filter by track if trackIds is non-empty
        if (!trackIds.empty()) {
            if (std::find(trackIds.begin(), trackIds.end(), clip.trackId) == trackIds.end())
                continue;
        }

        // Check overlap against beat-authoritative timeline placement. The seconds fields are
        // transitional caches and can be stale after beat-mode edits or BPM changes.
        double clipStart = clip.getTimelineStart(tempoBPM);
        double clipLength = clip.getTimelineLength(tempoBPM);
        double clipEnd = clipStart + clipLength;
        if (clipStart >= endTime || clipEnd <= startTime)
            continue;

        double overlapStart = std::max(clipStart, startTime);
        double overlapEnd = std::min(clipEnd, endTime);

        ClipInfo trimmed = clip;
        ClipOperations::setTimelinePlacement(trimmed, overlapStart, overlapEnd - overlapStart,
                                             tempoBPM);

        if (clip.isAudio()) {
            // Adjust offset for the trimmed start position
            double trimFromLeft = overlapStart - clipStart;
            if (clip.audio().interpretation.bpm > 0.0 && tempoBPM > 0.0) {
                // autoTempo: work in beats, derive seconds
                double deltaBeats = trimFromLeft * tempoBPM / 60.0;
                trimmed.offsetBeats = clip.offsetBeats + deltaBeats;
                trimmed.offset = trimmed.offsetBeats * 60.0 / clip.audio().interpretation.bpm;
            } else {
                trimmed.offset = clip.offset + trimFromLeft * clip.speedRatio;
            }
            // Sync loop fields for non-looped clips
            if (!trimmed.loopEnabled) {
                if (trimmed.autoTempo && trimmed.audio().interpretation.bpm > 0.0) {
                    trimmed.loopStartBeats = trimmed.offsetBeats;
                    trimmed.loopStart =
                        trimmed.loopStartBeats * 60.0 / trimmed.audio().interpretation.bpm;
                } else {
                    trimmed.loopStart = trimmed.offset;
                }
                trimmed.loopLength = trimmed.timelineToSource(trimmed.getTimelineLength(tempoBPM));
            }
        } else if (clip.isMidi() && !clip.midiNotes.empty()) {
            // Filter MIDI notes to those within the overlap range
            double bps = tempoBPM / 60.0;
            // Notes are in beats relative to clip start. Convert overlap bounds to beats.
            double overlapStartBeat = (overlapStart - clipStart) * bps;
            double overlapEndBeat = (overlapEnd - clipStart) * bps;

            std::vector<MidiNote> filteredNotes;
            for (const auto& note : clip.midiNotes) {
                if (note.startBeat >= overlapStartBeat && note.startBeat < overlapEndBeat) {
                    MidiNote adjusted = note;
                    adjusted.startBeat -= overlapStartBeat;
                    filteredNotes.push_back(adjusted);
                }
            }
            trimmed.midiNotes = filteredNotes;
        }

        clipboard_.push_back(trimmed);
    }
}

std::vector<ClipId> ClipManager::pasteFromClipboard(double pasteTime, TrackId targetTrackId,
                                                    ClipView targetView, int targetSceneIndex) {
    std::vector<ClipId> newClips;

    if (clipboard_.empty()) {
        return newClips;
    }

    // Calculate offset from reference time to paste time
    const double bpm = currentProjectTempoOrDefault();
    double timeOffset = pasteTime - clipboardReferenceTime_;

    // Track which scene slots have been used during this paste (for multi-clip session paste)
    std::unordered_map<TrackId, int> trackSceneMap;

    for (const auto& clipData : clipboard_) {
        // Calculate new start time maintaining relative position
        double newStartTime = clipData.getTimelineStart(bpm) + timeOffset;
        double clipLength = clipData.getTimelineLength(bpm);

        // Determine target track
        TrackId newTrackId = (targetTrackId != INVALID_TRACK_ID) ? targetTrackId : clipData.trackId;

        // Create new clip based on type, using targetView instead of clipData.view
        ClipId newClipId = INVALID_CLIP_ID;
        if (clipData.isAudio()) {
            if (clipData.audio().source.filePath.isNotEmpty()) {
                newClipId = createAudioClip(newTrackId, newStartTime, clipLength,
                                            clipData.audio().source.filePath, targetView, 0.0,
                                            ClipOverlapPolicy::ResolveOverlaps);
            }
        } else {
            // For MIDI clips, create empty then copy notes
            newClipId = createMidiClip(newTrackId, newStartTime, clipLength, targetView,
                                       ClipOverlapPolicy::ResolveOverlaps);
        }

        if (newClipId != INVALID_CLIP_ID) {
            // Copy properties
            auto* newClip = getClip(newClipId);
            if (newClip) {
                newClip->name = clipData.name + " (copy)";
                newClip->colour = clipData.colour;
                newClip->loopEnabled = clipData.loopEnabled;

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

                if (clipData.isAudio() && !crossViewToSession) {
                    newClip->offset = clipData.offset;
                    newClip->offsetBeats = clipData.offsetBeats;
                    newClip->loopStart = clipData.loopStart;
                    newClip->loopLength = clipData.loopLength;
                }

                // Audio playback — preserve session defaults for cross-view paste
                if (!crossViewToSession) {
                    newClip->autoTempo = clipData.autoTempo;
                    newClip->loopStartBeats = clipData.loopStartBeats;
                    newClip->loopLengthBeats = clipData.loopLengthBeats;
                    const double pastedLengthBeats = clipData.placement.lengthBeats > 0.0
                                                         ? clipData.placement.lengthBeats
                                                         : clipData.lengthBeats;
                    if (pastedLengthBeats > 0.0)
                        newClip->setPlacementBeats(newClip->placement.startBeat, pastedLengthBeats);
                    // Don't overwrite startBeats — createMidiClip/createAudioClip already
                    // computed the correct value from newStartTime
                }
                if (!crossViewToSession) {
                    newClip->warpEnabled = clipData.warpEnabled;
                    newClip->timeStretchMode = clipData.timeStretchMode;
                }

                // Source file metadata describes audio files only.
                if (clipData.isAudio()) {
                    if (clipData.audio().interpretation.bpm > 0.0)
                        newClip->audio().interpretation.bpm = clipData.audio().interpretation.bpm;
                    if (clipData.audio().interpretation.totalBeats > 0.0)
                        newClip->audio().interpretation.totalBeats =
                            clipData.audio().interpretation.totalBeats;
                }

                // Pitch
                newClip->autoPitch = clipData.autoPitch;
                newClip->analogPitch = clipData.analogPitch;
                newClip->pitchChange = clipData.pitchChange;
                newClip->transpose = clipData.transpose;

                // Mix
                newClip->volumeDB = clipData.volumeDB;
                newClip->gainDB = clipData.gainDB;
                newClip->pan = clipData.pan;

                // Playback
                newClip->isReversed = clipData.isReversed;
                if (!crossViewToSession)
                    newClip->speedRatio = clipData.speedRatio;

                // Channels
                newClip->leftChannelActive = clipData.leftChannelActive;
                newClip->rightChannelActive = clipData.rightChannelActive;

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

                    if (!crossViewToSession) {
                        // Reset extended loops to base loop length for
                        // session→session pastes
                        if (clipData.loopEnabled && clipData.loopLengthBeats > 0.0 &&
                            clipData.lengthBeats > clipData.loopLengthBeats) {
                            newClip->lengthBeats = clipData.loopLengthBeats;
                            newClip->loopLengthBeats = clipData.loopLengthBeats;
                            const double bpm = currentProjectTempoOrDefault();
                            const double newLength = clipData.loopLengthBeats * 60.0 / bpm;
                            ClipOperations::setTimelinePlacement(
                                *newClip, newClip->getTimelineStart(bpm), newLength, bpm);
                            newClip->loopLength = newClip->getTimelineLength(bpm);
                        } else if (clipData.loopEnabled && clipData.loopLength > 0.0 &&
                                   clipData.getTimelineLength(currentProjectTempoOrDefault()) >
                                       clipData.sourceToTimeline(clipData.loopLength)) {
                            const double bpm = currentProjectTempoOrDefault();
                            ClipOperations::setTimelinePlacement(
                                *newClip, newClip->getTimelineStart(bpm),
                                clipData.sourceToTimeline(clipData.loopLength), bpm);
                            newClip->loopLength = clipData.loopLength;
                        }
                    }
                }

                if (newClip->view == ClipView::Arrangement)
                    resolveOverlaps(newClipId);
                forceNotifyClipPropertyChanged(newClipId);
            }

            newClips.push_back(newClipId);
        }
    }

    if (!newClips.empty())
        notifyClipsChanged();

    return newClips;
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

void ClipManager::clearClipboard() {
    clipboard_.clear();
    clipboardReferenceTime_ = 0.0;
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
