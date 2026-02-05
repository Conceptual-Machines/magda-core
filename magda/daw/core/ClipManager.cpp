#include "ClipManager.hpp"

#include <algorithm>
#include <cmath>

#include "ClipOperations.hpp"
#include "TrackManager.hpp"
#include "audio/AudioThumbnailManager.hpp"

namespace magda {

ClipManager& ClipManager::getInstance() {
    static ClipManager instance;
    return instance;
}

// ============================================================================
// Clip Creation
// ============================================================================

ClipId ClipManager::createAudioClip(TrackId trackId, double startTime, double length,
                                    const juce::String& audioFilePath, ClipView view,
                                    double projectBPM) {
    juce::ignoreUnused(projectBPM);
    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.type = ClipType::Audio;
    clip.view = view;
    clip.name = generateClipName(ClipType::Audio);
    clip.colour = ClipInfo::getDefaultColor(
        static_cast<int>(arrangementClips_.size() + sessionClips_.size()));
    clip.startTime = startTime;
    clip.length = length;
    clip.audioFilePath = audioFilePath;
    clip.offset = 0.0;
    clip.speedRatio = 1.0;

    // Set loopStart to offset (0), loopLength to the clip's source extent
    clip.loopStart = 0.0;
    clip.setLoopLengthFromTimeline(length);

    // Add to appropriate array based on view
    if (view == ClipView::Arrangement) {
        arrangementClips_.push_back(clip);
    } else {
        // Session clips loop by default
        clip.loopEnabled = true;
        clip.length = length;
        sessionClips_.push_back(clip);
    }

    notifyClipsChanged();

    const char* viewStr = (view == ClipView::Arrangement) ? "arrangement" : "session";
    DBG("Created audio clip: " << clip.name << " (id=" << clip.id << ", track=" << trackId
                               << ", view=" << viewStr << ")");
    return clip.id;
}

ClipId ClipManager::createMidiClip(TrackId trackId, double startTime, double length,
                                   ClipView view) {
    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.type = ClipType::MIDI;
    clip.view = view;
    clip.name = generateClipName(ClipType::MIDI);
    clip.colour = ClipInfo::getDefaultColor(
        static_cast<int>(arrangementClips_.size() + sessionClips_.size()));
    clip.startTime = startTime;
    clip.length = length;

    // Add to appropriate array based on view
    if (view == ClipView::Arrangement) {
        arrangementClips_.push_back(clip);
    } else {
        // Session clips loop by default
        clip.loopEnabled = true;
        sessionClips_.push_back(clip);
    }

    notifyClipsChanged();

    const char* viewStr = (view == ClipView::Arrangement) ? "arrangement" : "session";
    DBG("Created MIDI clip: " << clip.name << " (id=" << clip.id << ", track=" << trackId
                              << ", view=" << viewStr << ")");
    return clip.id;
}

void ClipManager::deleteClip(ClipId clipId) {
    // Try arrangement clips first
    auto it = std::find_if(arrangementClips_.begin(), arrangementClips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });

    if (it != arrangementClips_.end()) {
        DBG("Deleted arrangement clip: " << it->name << " (id=" << clipId << ")");

        // Clear selection if this was selected
        if (selectedClipId_ == clipId) {
            selectedClipId_ = INVALID_CLIP_ID;
            notifyClipSelectionChanged(INVALID_CLIP_ID);
        }

        arrangementClips_.erase(it);
        notifyClipsChanged();
        return;
    }

    // Try session clips
    it = std::find_if(sessionClips_.begin(), sessionClips_.end(),
                      [clipId](const ClipInfo& c) { return c.id == clipId; });

    if (it != sessionClips_.end()) {
        DBG("Deleted session clip: " << it->name << " (id=" << clipId << ")");

        // Clear selection if this was selected
        if (selectedClipId_ == clipId) {
            selectedClipId_ = INVALID_CLIP_ID;
            notifyClipSelectionChanged(INVALID_CLIP_ID);
        }

        sessionClips_.erase(it);
        notifyClipsChanged();
    }
}

void ClipManager::restoreClip(const ClipInfo& clipInfo) {
    // Check if a clip with this ID already exists in either array
    auto checkExists = [&clipInfo](const std::vector<ClipInfo>& clips) {
        return std::find_if(clips.begin(), clips.end(), [&clipInfo](const ClipInfo& c) {
                   return c.id == clipInfo.id;
               }) != clips.end();
    };

    if (checkExists(arrangementClips_) || checkExists(sessionClips_)) {
        DBG("Warning: Clip with id=" << clipInfo.id << " already exists, skipping restore");
        return;
    }

    // Add to appropriate array based on view
    if (clipInfo.view == ClipView::Arrangement) {
        arrangementClips_.push_back(clipInfo);
    } else {
        sessionClips_.push_back(clipInfo);
    }

    // Ensure nextClipId_ is beyond any restored clip IDs
    if (clipInfo.id >= nextClipId_) {
        nextClipId_ = clipInfo.id + 1;
    }

    notifyClipsChanged();
    DBG("Restored clip: " << clipInfo.name << " (id=" << clipInfo.id << ")");
}

void ClipManager::forceNotifyClipsChanged() {
    notifyClipsChanged();
}

void ClipManager::forceNotifyClipPropertyChanged(ClipId clipId) {
    notifyClipPropertyChanged(clipId);
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
        // Offset the duplicate to the right on the timeline
        newClip.startTime = original->startTime + original->length;
        arrangementClips_.push_back(newClip);
    } else {
        // Session clips don't use timeline positioning
        newClip.startTime = 0.0;
        sessionClips_.push_back(newClip);
    }

    notifyClipsChanged();

    DBG("Duplicated clip: " << newClip.name << " (id=" << newClip.id << ")");
    return newClip.id;
}

ClipId ClipManager::duplicateClipAt(ClipId clipId, double startTime, TrackId trackId) {
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

    // Add to same array as original
    if (newClip.view == ClipView::Arrangement) {
        newClip.startTime = startTime;
        arrangementClips_.push_back(newClip);
    } else {
        // Session clips don't use timeline positioning
        newClip.startTime = 0.0;
        sessionClips_.push_back(newClip);
    }

    notifyClipsChanged();

    DBG("Duplicated clip at " << startTime << ": " << newClip.name << " (id=" << newClip.id << ")");
    return newClip.id;
}

// ============================================================================
// Clip Manipulation
// ============================================================================

void ClipManager::moveClip(ClipId clipId, double newStartTime, double /*tempo*/) {
    if (auto* clip = getClip(clipId)) {
        ClipOperations::moveContainer(*clip, newStartTime);
        // Notes maintain their relative position within the clip (startBeat unchanged)
        // so they move with the clip on the timeline
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::moveClipToTrack(ClipId clipId, TrackId newTrackId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->trackId != newTrackId) {
            clip->trackId = newTrackId;
            notifyClipsChanged();  // Track assignment change affects layout
        }
    }
}

void ClipManager::resizeClip(ClipId clipId, double newLength, bool fromStart, double tempo) {
    if (auto* clip = getClip(clipId)) {
        if (fromStart) {
            ClipOperations::resizeContainerFromLeft(*clip, newLength, tempo);
        } else {
            ClipOperations::resizeContainerFromRight(*clip, newLength, tempo);
        }
        notifyClipPropertyChanged(clipId);
    }
}

ClipId ClipManager::splitClip(ClipId clipId, double splitTime, double tempo) {
    auto* clip = getClip(clipId);
    if (!clip) {
        return INVALID_CLIP_ID;
    }

    // Validate split position is within clip
    if (splitTime <= clip->startTime || splitTime >= clip->getEndTime()) {
        return INVALID_CLIP_ID;
    }

    // Calculate lengths
    double leftLength = splitTime - clip->startTime;
    double rightLength = clip->getEndTime() - splitTime;

    // Create right half as new clip
    ClipInfo rightClip = *clip;
    rightClip.id = nextClipId_++;
    rightClip.name = clip->name + " R";
    rightClip.startTime = splitTime;
    rightClip.length = rightLength;

    // Adjust offset for right clip (TE-aligned: offset is start position in source)
    if (rightClip.type == ClipType::Audio) {
        rightClip.offset += leftLength / clip->speedRatio;
    }

    // Handle MIDI clip splitting - DESTRUCTIVE (each clip owns its notes)
    if (rightClip.type == ClipType::MIDI && !rightClip.midiNotes.empty()) {
        const double beatsPerSecond = tempo / 60.0;
        double splitBeat = leftLength * beatsPerSecond;

        DBG("MIDI SPLIT (destructive):");
        DBG("  Split at beat: " << splitBeat);

        // Partition notes between left and right clips
        std::vector<MidiNote> leftNotes;
        std::vector<MidiNote> rightNotes;

        for (const auto& note : clip->midiNotes) {
            if (note.startBeat < splitBeat) {
                // Note belongs to left clip
                leftNotes.push_back(note);
            } else {
                // Note belongs to right clip - adjust position relative to right clip start
                MidiNote adjustedNote = note;
                adjustedNote.startBeat -= splitBeat;
                rightNotes.push_back(adjustedNote);
            }
        }

        // Update both clips with their respective notes
        clip->midiNotes = leftNotes;
        rightClip.midiNotes = rightNotes;

        DBG("  Left clip: " << leftNotes.size() << " notes");
        DBG("  Right clip: " << rightNotes.size() << " notes");
    }

    // Resize original clip to be left half
    clip->length = leftLength;
    clip->name = clip->name + " L";

    // Add right clip to same array as left clip
    if (rightClip.view == ClipView::Arrangement) {
        arrangementClips_.push_back(rightClip);
    } else {
        sessionClips_.push_back(rightClip);
    }

    notifyClipsChanged();

    DBG("Split clip " << clipId << " at " << splitTime << " -> new clip " << rightClip.id);
    return rightClip.id;
}

void ClipManager::trimClip(ClipId clipId, double newStartTime, double newLength) {
    if (auto* clip = getClip(clipId)) {
        clip->startTime = newStartTime;
        clip->length = newLength;
        notifyClipPropertyChanged(clipId);
    }
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
    juce::ignoreUnused(projectBPM);
    if (auto* clip = getClip(clipId)) {
        clip->loopEnabled = enabled;

        // When enabling loop on audio clips, transfer offset → loopStart
        // The user's current offset becomes the loop start point (phase resets to 0)
        if (enabled && clip->type == ClipType::Audio && clip->audioFilePath.isNotEmpty()) {
            clip->loopStart = clip->offset;

            // Ensure loopLength is set (preserves source extent in loop mode)
            if (clip->loopLength <= 0.0) {
                clip->setLoopLengthFromTimeline(clip->length);
            }

            sanitizeAudioClip(*clip);
        }

        // When disabling loop on audio clips, clamp length to actual file content
        if (!enabled && clip->type == ClipType::Audio && clip->audioFilePath.isNotEmpty()) {
            auto* thumbnail =
                AudioThumbnailManager::getInstance().getThumbnail(clip->audioFilePath);
            if (thumbnail) {
                clip->clampLengthToSource(thumbnail->getTotalLength());
            }
        }

        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipMidiOffset(ClipId clipId, double offsetBeats) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type != ClipType::MIDI) {
            DBG("setClipMidiOffset: Clip " << clipId << " is not a MIDI clip");
            return;
        }
        clip->midiOffset = juce::jmax(0.0, offsetBeats);
        notifyClipPropertyChanged(clipId);
        DBG("setClipMidiOffset: clip " << clipId << " offset=" << clip->midiOffset);
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

void ClipManager::setClipWarpEnabled(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && clip->warpEnabled != enabled) {
            clip->warpEnabled = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setOffset(ClipId clipId, double offset) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->offset = juce::jmax(0.0, offset);
            sanitizeAudioClip(*clip);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setLoopPhase(ClipId clipId, double phase) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && clip->loopEnabled) {
            clip->offset = clip->loopStart + phase;
            sanitizeAudioClip(*clip);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setLoopStart(ClipId clipId, double loopStart, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->loopStart = juce::jmax(0.0, loopStart);
            if (clip->autoTempo && bpm > 0.0) {
                clip->loopStartBeats = (clip->loopStart * bpm) / 60.0;
            }
            sanitizeAudioClip(*clip);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setLoopLength(ClipId clipId, double loopLength, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->loopLength = juce::jmax(0.0, loopLength);
            if (clip->autoTempo && bpm > 0.0) {
                clip->loopLengthBeats = (clip->loopLength * bpm) / 60.0;
            }
            sanitizeAudioClip(*clip);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setSpeedRatio(ClipId clipId, double speedRatio) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->speedRatio = juce::jlimit(ClipOperations::MIN_SPEED_RATIO,
                                            ClipOperations::MAX_SPEED_RATIO, speedRatio);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setTimeStretchMode(ClipId clipId, int mode) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
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
        if (clip->type == ClipType::Audio) {
            clip->autoPitch = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoPitchMode(ClipId clipId, int mode) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->autoPitchMode = juce::jlimit(0, 2, mode);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setPitchChange(ClipId clipId, float semitones) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->pitchChange = juce::jlimit(-48.0f, 48.0f, semitones);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setTranspose(ClipId clipId, int semitones) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
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
        if (clip->type == ClipType::Audio) {
            clip->autoDetectBeats = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setBeatSensitivity(ClipId clipId, float sensitivity) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
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
        if (clip->type == ClipType::Audio) {
            clip->isReversed = reversed;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Per-Clip Mix
// ============================================================================

void ClipManager::setClipGainDB(ClipId clipId, float dB) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->gainDB = juce::jlimit(-60.0f, 24.0f, dB);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setClipPan(ClipId clipId, float pan) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
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
        if (clip->type == ClipType::Audio) {
            clip->fadeIn = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOut(ClipId clipId, double seconds) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeOut = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeInType = juce::jlimit(0, 3, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeOutType = juce::jlimit(0, 3, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeInBehaviour = juce::jlimit(0, 1, behaviour);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeOutBehaviour = juce::jlimit(0, 1, behaviour);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoCrossfade(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->autoCrossfade = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Channels
// ============================================================================

void ClipManager::setLeftChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->leftChannelActive = active;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setRightChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->rightChannelActive = active;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Content-Level Operations (Editor Operations)
// ============================================================================

void ClipManager::trimAudioLeft(ClipId clipId, double trimAmount, double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::trimAudioFromLeft(*clip, trimAmount, fileDuration);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::trimAudioRight(ClipId clipId, double trimAmount, double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::trimAudioFromRight(*clip, trimAmount, fileDuration);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioLeft(ClipId clipId, double newLength, double oldLength,
                                   double originalSpeedRatio) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::stretchAudioFromLeft(*clip, newLength, oldLength, originalSpeedRatio);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioRight(ClipId clipId, double newLength, double oldLength,
                                    double originalSpeedRatio) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::stretchAudioFromRight(*clip, newLength, oldLength, originalSpeedRatio);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::addMidiNote(ClipId clipId, const MidiNote& note) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->midiNotes.push_back(note);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::removeMidiNote(ClipId clipId, int noteIndex) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI && noteIndex >= 0 &&
            noteIndex < static_cast<int>(clip->midiNotes.size())) {
            clip->midiNotes.erase(clip->midiNotes.begin() + noteIndex);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::clearMidiNotes(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->midiNotes.clear();
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Access
// ============================================================================

ClipInfo* ClipManager::getClip(ClipId clipId) {
    // Search arrangement clips first
    auto it = std::find_if(arrangementClips_.begin(), arrangementClips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });
    if (it != arrangementClips_.end()) {
        return &(*it);
    }

    // Search session clips
    it = std::find_if(sessionClips_.begin(), sessionClips_.end(),
                      [clipId](const ClipInfo& c) { return c.id == clipId; });
    return (it != sessionClips_.end()) ? &(*it) : nullptr;
}

const ClipInfo* ClipManager::getClip(ClipId clipId) const {
    // Search arrangement clips first
    auto it = std::find_if(arrangementClips_.begin(), arrangementClips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });
    if (it != arrangementClips_.end()) {
        return &(*it);
    }

    // Search session clips
    it = std::find_if(sessionClips_.begin(), sessionClips_.end(),
                      [clipId](const ClipInfo& c) { return c.id == clipId; });
    return (it != sessionClips_.end()) ? &(*it) : nullptr;
}

// TODO: Returns clips by value, copying potentially large structures. Callers should
// migrate to getArrangementClips() or getSessionClips() which return const references.
std::vector<ClipInfo> ClipManager::getClips() const {
    std::vector<ClipInfo> result;
    result.reserve(arrangementClips_.size() + sessionClips_.size());
    result.insert(result.end(), arrangementClips_.begin(), arrangementClips_.end());
    result.insert(result.end(), sessionClips_.begin(), sessionClips_.end());
    return result;
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId) const {
    std::vector<ClipId> result;
    // Only return arrangement clips (session clips use slot-based queries)
    for (const auto& clip : arrangementClips_) {
        if (clip.trackId == trackId) {
            result.push_back(clip.id);
        }
    }
    // Sort by start time
    std::sort(result.begin(), result.end(), [this](ClipId a, ClipId b) {
        const auto* clipA = getClip(a);
        const auto* clipB = getClip(b);
        return clipA && clipB && clipA->startTime < clipB->startTime;
    });
    return result;
}

ClipId ClipManager::getClipAtPosition(TrackId trackId, double time) const {
    // Only check arrangement clips (timeline-based positioning)
    for (const auto& clip : arrangementClips_) {
        if (clip.trackId == trackId && clip.containsTime(time)) {
            return clip.id;
        }
    }
    return INVALID_CLIP_ID;
}

std::vector<ClipId> ClipManager::getClipsInRange(TrackId trackId, double startTime,
                                                 double endTime) const {
    std::vector<ClipId> result;
    // Only check arrangement clips (timeline-based positioning)
    for (const auto& clip : arrangementClips_) {
        if (clip.trackId == trackId && clip.overlaps(startTime, endTime)) {
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

ClipId ClipManager::getClipInSlot(TrackId trackId, int sceneIndex) const {
    // Only check session clips (scene-based positioning)
    for (const auto& clip : sessionClips_) {
        if (clip.trackId == trackId && clip.sceneIndex == sceneIndex) {
            return clip.id;
        }
    }
    return INVALID_CLIP_ID;
}

void ClipManager::setClipSceneIndex(ClipId clipId, int sceneIndex) {
    if (auto* clip = getClip(clipId)) {
        clip->sceneIndex = sceneIndex;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::triggerClip(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        // Toggle mode: if clip is already playing, stop it instead
        if (clip->launchMode == LaunchMode::Toggle && (clip->isPlaying || clip->isQueued)) {
            stopClip(clipId);
            return;
        }

        // Trigger mode: if clip is already playing, re-trigger from start
        // The scheduler will handle deactivating the old TE clip and creating a new one

        // Stop other clips on same track (only check session clips since triggers are session-only)
        for (auto& otherClip : sessionClips_) {
            if (otherClip.trackId == clip->trackId && otherClip.id != clipId) {
                if (otherClip.isPlaying || otherClip.isQueued) {
                    otherClip.isPlaying = false;
                    otherClip.isQueued = false;
                    notifyClipPlaybackStateChanged(otherClip.id);
                }
            }
        }

        // Only set isQueued - the scheduler will set isPlaying when audio actually starts
        clip->isQueued = true;
        clip->isPlaying = false;
        notifyClipPlaybackStateChanged(clipId);
    }
}

void ClipManager::setClipPlayingState(ClipId clipId, bool playing) {
    if (auto* clip = getClip(clipId)) {
        if (playing) {
            clip->isPlaying = true;
            clip->isQueued = false;  // No longer queued, now actually playing
        } else {
            clip->isPlaying = false;
            clip->isQueued = false;
        }
        notifyClipPlaybackStateChanged(clipId);
    }
}

void ClipManager::stopClip(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        clip->isPlaying = false;
        clip->isQueued = false;
        notifyClipPlaybackStateChanged(clipId);
    }
}

void ClipManager::stopAllClips() {
    // Stop all session clips (only session clips have playback state)
    for (auto& clip : sessionClips_) {
        if (clip.isPlaying || clip.isQueued) {
            clip.isPlaying = false;
            clip.isQueued = false;
            notifyClipPlaybackStateChanged(clip.id);
        }
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
    arrangementClips_.clear();
    sessionClips_.clear();
    selectedClipId_ = INVALID_CLIP_ID;
    nextClipId_ = 1;
    notifyClipsChanged();
}

void ClipManager::createTestClips() {
    // Create random test clips on existing tracks for development
    auto& trackManager = TrackManager::getInstance();
    const auto& tracks = trackManager.getTracks();

    if (tracks.empty()) {
        DBG("No tracks available for test clips");
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

    DBG("Created test clips on " << tracks.size() << " tracks");
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
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPropertyChanged(clipId);
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
    // Count clips of same type in both arrays
    for (const auto& clip : arrangementClips_) {
        if (clip.type == type) {
            count++;
        }
    }
    for (const auto& clip : sessionClips_) {
        if (clip.type == type) {
            count++;
        }
    }

    if (type == ClipType::Audio) {
        return "Audio " + juce::String(count);
    } else {
        return "MIDI " + juce::String(count);
    }
}

void ClipManager::sanitizeAudioClip(ClipInfo& clip) {
    if (clip.type != ClipType::Audio || clip.audioFilePath.isEmpty())
        return;

    auto* thumbnail = AudioThumbnailManager::getInstance().getThumbnail(clip.audioFilePath);
    if (!thumbnail)
        return;

    double fileDuration = thumbnail->getTotalLength();
    if (fileDuration <= 0.0)
        return;

    // Clamp loopStart to file bounds
    clip.loopStart = juce::jlimit(0.0, fileDuration, clip.loopStart);

    // Clamp loopLength so loop region doesn't exceed file
    double availableFromLoop = fileDuration - clip.loopStart;
    if (clip.loopLength > availableFromLoop) {
        clip.loopLength = juce::jmax(0.0, availableFromLoop);
    }

    // Clamp offset to file bounds
    clip.offset = juce::jlimit(0.0, fileDuration, clip.offset);

    // Non-loop mode: clamp clip length to available source
    if (!clip.loopEnabled) {
        clip.clampLengthToSource(fileDuration);
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
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboardReferenceTime_ = std::min(clipboardReferenceTime_, clip->startTime);
        }
    }

    // Copy clips maintaining relative positions
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboard_.push_back(*clip);
        }
    }

    std::cout << "CLIPBOARD: Copied " << clipboard_.size() << " clip(s)" << std::endl;
}

std::vector<ClipId> ClipManager::pasteFromClipboard(double pasteTime, TrackId targetTrackId) {
    std::vector<ClipId> newClips;

    if (clipboard_.empty()) {
        return newClips;
    }

    // Calculate offset from reference time to paste time
    double timeOffset = pasteTime - clipboardReferenceTime_;

    for (const auto& clipData : clipboard_) {
        // Calculate new start time maintaining relative position
        double newStartTime = clipData.startTime + timeOffset;

        // Determine target track
        TrackId newTrackId = (targetTrackId != INVALID_TRACK_ID) ? targetTrackId : clipData.trackId;

        // Create new clip based on type
        ClipId newClipId = INVALID_CLIP_ID;
        if (clipData.type == ClipType::Audio) {
            if (clipData.audioFilePath.isNotEmpty()) {
                newClipId = createAudioClip(newTrackId, newStartTime, clipData.length,
                                            clipData.audioFilePath, clipData.view);
            }
        } else {
            // For MIDI clips, create empty then copy notes
            newClipId = createMidiClip(newTrackId, newStartTime, clipData.length, clipData.view);
        }

        if (newClipId != INVALID_CLIP_ID) {
            // Copy properties
            auto* newClip = getClip(newClipId);
            if (newClip) {
                newClip->name = clipData.name + " (copy)";
                newClip->colour = clipData.colour;
                newClip->loopEnabled = clipData.loopEnabled;

                // Copy MIDI notes if MIDI clip
                if (clipData.type == ClipType::MIDI) {
                    newClip->midiNotes = clipData.midiNotes;
                    newClip->midiOffset = clipData.midiOffset;  // Preserve offset for split clips
                }

                // Copy audio properties (TE-aligned)
                if (clipData.type == ClipType::Audio) {
                    newClip->offset = clipData.offset;
                    newClip->loopStart = clipData.loopStart;
                    newClip->loopLength = clipData.loopLength;
                    newClip->speedRatio = clipData.speedRatio;
                }

                forceNotifyClipPropertyChanged(newClipId);
            }

            newClips.push_back(newClipId);
        }
    }

    std::cout << "CLIPBOARD: Pasted " << newClips.size() << " clip(s) at " << pasteTime << "s"
              << std::endl;

    return newClips;
}

void ClipManager::cutToClipboard(const std::unordered_set<ClipId>& clipIds) {
    // Copy to clipboard
    copyToClipboard(clipIds);

    // Delete original clips
    for (auto clipId : clipIds) {
        deleteClip(clipId);
    }

    std::cout << "CLIPBOARD: Cut " << clipIds.size() << " clip(s)" << std::endl;
}

bool ClipManager::hasClipsInClipboard() const {
    return !clipboard_.empty();
}

void ClipManager::clearClipboard() {
    clipboard_.clear();
    clipboardReferenceTime_ = 0.0;
}

}  // namespace magda
