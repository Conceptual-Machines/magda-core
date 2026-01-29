#include "ClipManager.hpp"

#include <algorithm>

#include "TrackManager.hpp"

namespace magda {

ClipManager& ClipManager::getInstance() {
    static ClipManager instance;
    return instance;
}

// ============================================================================
// Clip Creation
// ============================================================================

ClipId ClipManager::createAudioClip(TrackId trackId, double startTime, double length,
                                    const juce::String& audioFilePath, ClipView view) {
    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.type = ClipType::Audio;
    clip.view = view;
    clip.name = generateClipName(ClipType::Audio);
    clip.colour = ClipInfo::getDefaultColor(static_cast<int>(clips_.size()));
    clip.startTime = startTime;
    clip.length = length;
    clip.audioSources.push_back(AudioSource{audioFilePath, 0.0, 0.0, length});

    clips_.push_back(clip);
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
    clip.colour = ClipInfo::getDefaultColor(static_cast<int>(clips_.size()));
    clip.startTime = startTime;
    clip.length = length;

    clips_.push_back(clip);
    notifyClipsChanged();

    const char* viewStr = (view == ClipView::Arrangement) ? "arrangement" : "session";
    DBG("Created MIDI clip: " << clip.name << " (id=" << clip.id << ", track=" << trackId
                              << ", view=" << viewStr << ")");
    return clip.id;
}

void ClipManager::deleteClip(ClipId clipId) {
    auto it = std::find_if(clips_.begin(), clips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });

    if (it != clips_.end()) {
        DBG("Deleted clip: " << it->name << " (id=" << clipId << ")");

        // Clear selection if this was selected
        if (selectedClipId_ == clipId) {
            selectedClipId_ = INVALID_CLIP_ID;
            notifyClipSelectionChanged(INVALID_CLIP_ID);
        }

        clips_.erase(it);
        notifyClipsChanged();
    }
}

void ClipManager::restoreClip(const ClipInfo& clipInfo) {
    // Check if a clip with this ID already exists
    auto it = std::find_if(clips_.begin(), clips_.end(),
                           [&clipInfo](const ClipInfo& c) { return c.id == clipInfo.id; });

    if (it != clips_.end()) {
        DBG("Warning: Clip with id=" << clipInfo.id << " already exists, skipping restore");
        return;
    }

    clips_.push_back(clipInfo);

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
    auto it = std::find_if(clips_.begin(), clips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });

    if (it == clips_.end()) {
        return INVALID_CLIP_ID;
    }

    ClipInfo newClip = *it;
    newClip.id = nextClipId_++;
    newClip.name = it->name + " Copy";
    // Offset the duplicate slightly to the right
    newClip.startTime = it->startTime + it->length;

    clips_.push_back(newClip);
    notifyClipsChanged();

    DBG("Duplicated clip: " << newClip.name << " (id=" << newClip.id << ")");
    return newClip.id;
}

ClipId ClipManager::duplicateClipAt(ClipId clipId, double startTime, TrackId trackId) {
    auto it = std::find_if(clips_.begin(), clips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });

    if (it == clips_.end()) {
        return INVALID_CLIP_ID;
    }

    ClipInfo newClip = *it;
    newClip.id = nextClipId_++;
    newClip.name = it->name + " Copy";
    newClip.startTime = startTime;

    // Use specified track or keep same track
    if (trackId != INVALID_TRACK_ID) {
        newClip.trackId = trackId;
    }

    clips_.push_back(newClip);
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

void ClipManager::resizeClip(ClipId clipId, double newLength, bool fromStart, double /*tempo*/) {
    if (auto* clip = getClip(clipId)) {
        if (fromStart) {
            ClipOperations::resizeContainerFromLeft(*clip, newLength);
        } else {
            ClipOperations::resizeContainerFromRight(*clip, newLength);
        }
        notifyClipPropertyChanged(clipId);
    }
}

ClipId ClipManager::splitClip(ClipId clipId, double splitTime) {
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

    // Adjust audio source offset for right clip
    if (rightClip.type == ClipType::Audio && !rightClip.audioSources.empty()) {
        rightClip.audioSources[0].offset += leftLength;
        rightClip.audioSources[0].length = rightLength;
    }

    // Resize original clip to be left half
    clip->length = leftLength;
    clip->name = clip->name + " L";
    if (clip->type == ClipType::Audio && !clip->audioSources.empty()) {
        clip->audioSources[0].length = leftLength;
    }

    clips_.push_back(rightClip);
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

void ClipManager::setClipLoopEnabled(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        clip->internalLoopEnabled = enabled;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipLoopLength(ClipId clipId, double lengthBeats) {
    if (auto* clip = getClip(clipId)) {
        clip->internalLoopLength = juce::jmax(0.25, lengthBeats);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setAudioSourcePosition(ClipId clipId, int sourceIndex, double position) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            clip->audioSources[sourceIndex].position = juce::jmax(0.0, position);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAudioSourceLength(ClipId clipId, int sourceIndex, double length) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            clip->audioSources[sourceIndex].length =
                juce::jmax(ClipOperations::MIN_SOURCE_LENGTH, length);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAudioSourceStretchFactor(ClipId clipId, int sourceIndex,
                                              double stretchFactor) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            clip->audioSources[sourceIndex].stretchFactor =
                juce::jlimit(ClipOperations::MIN_STRETCH_FACTOR, ClipOperations::MAX_STRETCH_FACTOR,
                             stretchFactor);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Content-Level Operations (Editor Operations)
// ============================================================================

void ClipManager::trimAudioSourceLeft(ClipId clipId, int sourceIndex, double trimAmount,
                                      double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            ClipOperations::trimSourceFromLeft(clip->audioSources[sourceIndex], trimAmount,
                                               fileDuration);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::trimAudioSourceRight(ClipId clipId, int sourceIndex, double trimAmount,
                                       double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            ClipOperations::trimSourceFromRight(clip->audioSources[sourceIndex], trimAmount,
                                                fileDuration);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioSourceLeft(ClipId clipId, int sourceIndex, double newLength,
                                         double oldLength, double originalPosition,
                                         double originalStretchFactor) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            ClipOperations::stretchSourceFromLeft(clip->audioSources[sourceIndex], newLength,
                                                  oldLength, originalPosition,
                                                  originalStretchFactor);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioSourceRight(ClipId clipId, int sourceIndex, double newLength,
                                          double oldLength, double originalStretchFactor) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            ClipOperations::stretchSourceFromRight(clip->audioSources[sourceIndex], newLength,
                                                   oldLength, originalStretchFactor, clip->length);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::moveAudioSource(ClipId clipId, int sourceIndex, double newPosition) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && sourceIndex >= 0 &&
            sourceIndex < static_cast<int>(clip->audioSources.size())) {
            ClipOperations::moveSource(clip->audioSources[sourceIndex], newPosition, clip->length);
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
    auto it = std::find_if(clips_.begin(), clips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });
    return (it != clips_.end()) ? &(*it) : nullptr;
}

const ClipInfo* ClipManager::getClip(ClipId clipId) const {
    auto it = std::find_if(clips_.begin(), clips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });
    return (it != clips_.end()) ? &(*it) : nullptr;
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId) const {
    std::vector<ClipId> result;
    for (const auto& clip : clips_) {
        // Only return arrangement clips (session clips use slot-based queries)
        if (clip.trackId == trackId && clip.view == ClipView::Arrangement) {
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
    for (const auto& clip : clips_) {
        // Only check arrangement clips (timeline-based positioning)
        if (clip.trackId == trackId && clip.view == ClipView::Arrangement &&
            clip.containsTime(time)) {
            return clip.id;
        }
    }
    return INVALID_CLIP_ID;
}

std::vector<ClipId> ClipManager::getClipsInRange(TrackId trackId, double startTime,
                                                 double endTime) const {
    std::vector<ClipId> result;
    for (const auto& clip : clips_) {
        // Only check arrangement clips (timeline-based positioning)
        if (clip.trackId == trackId && clip.view == ClipView::Arrangement &&
            clip.overlaps(startTime, endTime)) {
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
    if (selectedClipId_ != INVALID_CLIP_ID) {
        selectedClipId_ = INVALID_CLIP_ID;
        notifyClipSelectionChanged(INVALID_CLIP_ID);
    }
}

// ============================================================================
// Session View (Clip Launcher)
// ============================================================================

ClipId ClipManager::getClipInSlot(TrackId trackId, int sceneIndex) const {
    for (const auto& clip : clips_) {
        // Only check session clips (scene-based positioning)
        if (clip.trackId == trackId && clip.view == ClipView::Session &&
            clip.sceneIndex == sceneIndex) {
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
        // Stop other clips on same track
        for (auto& otherClip : clips_) {
            if (otherClip.trackId == clip->trackId && otherClip.id != clipId) {
                if (otherClip.isPlaying || otherClip.isQueued) {
                    otherClip.isPlaying = false;
                    otherClip.isQueued = false;
                    notifyClipPlaybackStateChanged(otherClip.id);
                }
            }
        }

        clip->isQueued = true;
        clip->isPlaying = true;  // For now, immediate trigger
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
    for (auto& clip : clips_) {
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
    clips_.clear();
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
    for (const auto& clip : clips_) {
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

}  // namespace magda
