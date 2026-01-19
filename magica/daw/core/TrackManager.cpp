#include "TrackManager.hpp"

#include <algorithm>

namespace magica {

TrackManager& TrackManager::getInstance() {
    static TrackManager instance;
    return instance;
}

TrackManager::TrackManager() {
    // App starts with no tracks - user can add via Track > Add Track
}

// ============================================================================
// Track Operations
// ============================================================================

TrackId TrackManager::createTrack(const juce::String& name, TrackType type) {
    TrackInfo track;
    track.id = nextTrackId_++;
    track.type = type;
    track.name = name.isEmpty() ? generateTrackName() : name;
    track.colour = TrackInfo::getDefaultColor(static_cast<int>(tracks_.size()));

    tracks_.push_back(track);
    notifyTracksChanged();

    DBG("Created track: " << track.name << " (id=" << track.id
                          << ", type=" << getTrackTypeName(type) << ")");
    return track.id;
}

TrackId TrackManager::createGroupTrack(const juce::String& name) {
    juce::String groupName = name.isEmpty() ? "Group" : name;
    return createTrack(groupName, TrackType::Group);
}

void TrackManager::deleteTrack(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (!track)
        return;

    // If this track has a parent, remove it from parent's children
    if (track->hasParent()) {
        if (auto* parent = getTrack(track->parentId)) {
            auto& children = parent->childIds;
            children.erase(std::remove(children.begin(), children.end(), trackId), children.end());
        }
    }

    // If this is a group, recursively delete all children
    if (track->isGroup()) {
        // Copy the children list since we'll be modifying it
        auto childrenCopy = track->childIds;
        for (auto childId : childrenCopy) {
            deleteTrack(childId);
        }
    }

    // Remove the track itself
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it != tracks_.end()) {
        DBG("Deleted track: " << it->name << " (id=" << trackId << ")");
        tracks_.erase(it);
        notifyTracksChanged();
    }
}

void TrackManager::restoreTrack(const TrackInfo& trackInfo) {
    // Check if a track with this ID already exists
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [&trackInfo](const TrackInfo& t) { return t.id == trackInfo.id; });

    if (it != tracks_.end()) {
        DBG("Warning: Track with id=" << trackInfo.id << " already exists, skipping restore");
        return;
    }

    tracks_.push_back(trackInfo);

    // Ensure nextTrackId_ is beyond any restored track IDs
    if (trackInfo.id >= nextTrackId_) {
        nextTrackId_ = trackInfo.id + 1;
    }

    // If track has a parent, add it back to parent's children
    if (trackInfo.hasParent()) {
        if (auto* parent = getTrack(trackInfo.parentId)) {
            if (std::find(parent->childIds.begin(), parent->childIds.end(), trackInfo.id) ==
                parent->childIds.end()) {
                parent->childIds.push_back(trackInfo.id);
            }
        }
    }

    notifyTracksChanged();
    DBG("Restored track: " << trackInfo.name << " (id=" << trackInfo.id << ")");
}

void TrackManager::duplicateTrack(TrackId trackId) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it != tracks_.end()) {
        TrackInfo newTrack = *it;
        newTrack.id = nextTrackId_++;
        newTrack.name = it->name + " Copy";
        newTrack.childIds.clear();  // Don't duplicate children references

        // Insert after the original
        auto insertPos = it + 1;
        tracks_.insert(insertPos, newTrack);

        // If the original had a parent, add the copy to the same parent
        if (newTrack.hasParent()) {
            if (auto* parent = getTrack(newTrack.parentId)) {
                parent->childIds.push_back(newTrack.id);
            }
        }

        notifyTracksChanged();
        DBG("Duplicated track: " << newTrack.name << " (id=" << newTrack.id << ")");
    }
}

void TrackManager::moveTrack(TrackId trackId, int newIndex) {
    int currentIndex = getTrackIndex(trackId);
    if (currentIndex < 0 || newIndex < 0 || newIndex >= static_cast<int>(tracks_.size())) {
        return;
    }

    if (currentIndex != newIndex) {
        TrackInfo track = tracks_[currentIndex];
        tracks_.erase(tracks_.begin() + currentIndex);
        tracks_.insert(tracks_.begin() + newIndex, track);
        notifyTracksChanged();
    }
}

// ============================================================================
// Hierarchy Operations
// ============================================================================

void TrackManager::addTrackToGroup(TrackId trackId, TrackId groupId) {
    auto* track = getTrack(trackId);
    auto* group = getTrack(groupId);

    if (!track || !group || !group->isGroup()) {
        DBG("addTrackToGroup failed: invalid track or group");
        return;
    }

    // Prevent adding a group to itself or to its descendants
    if (trackId == groupId)
        return;
    auto descendants = getAllDescendants(trackId);
    if (std::find(descendants.begin(), descendants.end(), groupId) != descendants.end()) {
        DBG("Cannot add group to its own descendant");
        return;
    }

    // Remove from current parent if any
    removeTrackFromGroup(trackId);

    // Add to new parent
    track->parentId = groupId;
    group->childIds.push_back(trackId);

    notifyTracksChanged();
    DBG("Added track " << track->name << " to group " << group->name);
}

void TrackManager::removeTrackFromGroup(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (!track || !track->hasParent())
        return;

    if (auto* parent = getTrack(track->parentId)) {
        auto& children = parent->childIds;
        children.erase(std::remove(children.begin(), children.end(), trackId), children.end());
    }

    track->parentId = INVALID_TRACK_ID;
    notifyTracksChanged();
}

TrackId TrackManager::createTrackInGroup(TrackId groupId, const juce::String& name,
                                         TrackType type) {
    auto* group = getTrack(groupId);
    if (!group || !group->isGroup()) {
        DBG("createTrackInGroup failed: invalid group");
        return INVALID_TRACK_ID;
    }

    TrackId newId = createTrack(name, type);
    addTrackToGroup(newId, groupId);
    return newId;
}

std::vector<TrackId> TrackManager::getChildTracks(TrackId groupId) const {
    const auto* group = getTrack(groupId);
    if (!group)
        return {};
    return group->childIds;
}

std::vector<TrackId> TrackManager::getTopLevelTracks() const {
    std::vector<TrackId> result;
    for (const auto& track : tracks_) {
        if (track.isTopLevel()) {
            result.push_back(track.id);
        }
    }
    return result;
}

std::vector<TrackId> TrackManager::getAllDescendants(TrackId trackId) const {
    std::vector<TrackId> result;
    const auto* track = getTrack(trackId);
    if (!track)
        return result;

    // BFS to collect all descendants
    std::vector<TrackId> toProcess = track->childIds;
    while (!toProcess.empty()) {
        TrackId current = toProcess.back();
        toProcess.pop_back();
        result.push_back(current);

        if (const auto* child = getTrack(current)) {
            for (auto grandchildId : child->childIds) {
                toProcess.push_back(grandchildId);
            }
        }
    }
    return result;
}

// ============================================================================
// Access
// ============================================================================

TrackInfo* TrackManager::getTrack(TrackId trackId) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });
    return (it != tracks_.end()) ? &(*it) : nullptr;
}

const TrackInfo* TrackManager::getTrack(TrackId trackId) const {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });
    return (it != tracks_.end()) ? &(*it) : nullptr;
}

int TrackManager::getTrackIndex(TrackId trackId) const {
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].id == trackId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ============================================================================
// Track Property Setters
// ============================================================================

void TrackManager::setTrackName(TrackId trackId, const juce::String& name) {
    if (auto* track = getTrack(trackId)) {
        track->name = name;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackColour(TrackId trackId, juce::Colour colour) {
    if (auto* track = getTrack(trackId)) {
        track->colour = colour;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackVolume(TrackId trackId, float volume) {
    if (auto* track = getTrack(trackId)) {
        track->volume = juce::jlimit(0.0f, 1.0f, volume);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackPan(TrackId trackId, float pan) {
    if (auto* track = getTrack(trackId)) {
        track->pan = juce::jlimit(-1.0f, 1.0f, pan);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackMuted(TrackId trackId, bool muted) {
    if (auto* track = getTrack(trackId)) {
        track->muted = muted;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackSoloed(TrackId trackId, bool soloed) {
    if (auto* track = getTrack(trackId)) {
        track->soloed = soloed;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackRecordArmed(TrackId trackId, bool armed) {
    if (auto* track = getTrack(trackId)) {
        track->recordArmed = armed;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackType(TrackId trackId, TrackType type) {
    if (auto* track = getTrack(trackId)) {
        // Don't allow changing type if track has children (group tracks)
        if (track->hasChildren() && type != TrackType::Group) {
            DBG("Cannot change type of group track with children");
            return;
        }
        track->type = type;
        notifyTrackPropertyChanged(trackId);
    }
}

// ============================================================================
// View Settings
// ============================================================================

void TrackManager::setTrackVisible(TrackId trackId, ViewMode mode, bool visible) {
    if (auto* track = getTrack(trackId)) {
        track->viewSettings.setVisible(mode, visible);
        // Use tracksChanged since visibility affects which tracks are displayed
        notifyTracksChanged();
    }
}

void TrackManager::setTrackLocked(TrackId trackId, ViewMode mode, bool locked) {
    if (auto* track = getTrack(trackId)) {
        track->viewSettings.setLocked(mode, locked);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackCollapsed(TrackId trackId, ViewMode mode, bool collapsed) {
    if (auto* track = getTrack(trackId)) {
        track->viewSettings.setCollapsed(mode, collapsed);
        // Use tracksChanged since collapsing affects which child tracks are displayed
        notifyTracksChanged();
    }
}

void TrackManager::setTrackHeight(TrackId trackId, ViewMode mode, int height) {
    if (auto* track = getTrack(trackId)) {
        track->viewSettings.setHeight(mode, juce::jmax(20, height));
        notifyTrackPropertyChanged(trackId);
    }
}

// ============================================================================
// Query Tracks by View
// ============================================================================

std::vector<TrackId> TrackManager::getVisibleTracks(ViewMode mode) const {
    std::vector<TrackId> result;
    for (const auto& track : tracks_) {
        if (track.isVisibleIn(mode)) {
            result.push_back(track.id);
        }
    }
    return result;
}

std::vector<TrackId> TrackManager::getVisibleTopLevelTracks(ViewMode mode) const {
    std::vector<TrackId> result;
    for (const auto& track : tracks_) {
        if (track.isTopLevel() && track.isVisibleIn(mode)) {
            result.push_back(track.id);
        }
    }
    return result;
}

// ============================================================================
// Track Selection
// ============================================================================

void TrackManager::setSelectedTrack(TrackId trackId) {
    if (selectedTrackId_ != trackId) {
        selectedTrackId_ = trackId;
        notifyTrackSelectionChanged(trackId);
    }
}

// ============================================================================
// Master Channel
// ============================================================================

void TrackManager::setMasterVolume(float volume) {
    masterChannel_.volume = volume;
    notifyMasterChannelChanged();
}

void TrackManager::setMasterPan(float pan) {
    masterChannel_.pan = pan;
    notifyMasterChannelChanged();
}

void TrackManager::setMasterMuted(bool muted) {
    masterChannel_.muted = muted;
    notifyMasterChannelChanged();
}

void TrackManager::setMasterSoloed(bool soloed) {
    masterChannel_.soloed = soloed;
    notifyMasterChannelChanged();
}

void TrackManager::setMasterVisible(ViewMode mode, bool visible) {
    masterChannel_.viewSettings.setVisible(mode, visible);
    notifyMasterChannelChanged();
}

// ============================================================================
// Listener Management
// ============================================================================

void TrackManager::addListener(TrackManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void TrackManager::removeListener(TrackManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

// ============================================================================
// Initialization
// ============================================================================

void TrackManager::createDefaultTracks(int count) {
    clearAllTracks();
    for (int i = 0; i < count; ++i) {
        createTrack();
    }
}

void TrackManager::clearAllTracks() {
    tracks_.clear();
    nextTrackId_ = 1;
    notifyTracksChanged();
}

// ============================================================================
// Private Helpers
// ============================================================================

void TrackManager::notifyTracksChanged() {
    for (auto* listener : listeners_) {
        listener->tracksChanged();
    }
}

void TrackManager::notifyTrackPropertyChanged(int trackId) {
    for (auto* listener : listeners_) {
        listener->trackPropertyChanged(trackId);
    }
}

void TrackManager::notifyMasterChannelChanged() {
    for (auto* listener : listeners_) {
        listener->masterChannelChanged();
    }
}

void TrackManager::notifyTrackSelectionChanged(TrackId trackId) {
    for (auto* listener : listeners_) {
        listener->trackSelectionChanged(trackId);
    }
}

juce::String TrackManager::generateTrackName() const {
    return juce::String(tracks_.size() + 1) + " Track";
}

}  // namespace magica
