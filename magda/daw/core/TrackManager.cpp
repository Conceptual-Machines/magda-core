#include "TrackManager.hpp"

#include <algorithm>

#include "../audio/AudioBridge.hpp"
#include "../audio/MidiBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "ModulatorEngine.hpp"
#include "RackInfo.hpp"

namespace magda {

TrackManager& TrackManager::getInstance() {
    static TrackManager instance;
    return instance;
}

TrackManager::TrackManager() {
    // Start with empty project - no default tracks
    // User can create tracks manually or load from project file
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

    // Set default routing
    track.audioOutputDevice = "master";  // Audio always routes to master
    track.audioInputDevice = "";         // Audio input disabled by default (enable via UI)
    // midiOutputDevice left empty - requires specific device selection

    // Assign aux bus index for Aux tracks; aux tracks never receive MIDI
    if (type == TrackType::Aux) {
        track.auxBusIndex = nextAuxBusIndex_++;
        track.midiInputDevice = "";  // Aux tracks don't receive MIDI
    } else {
        track.midiInputDevice = "all";  // MIDI listens to all inputs
    }

    TrackId trackId = track.id;
    tracks_.push_back(track);
    notifyTracksChanged();

    DBG("Created track: " << track.name << " (id=" << trackId << ", type=" << getTrackTypeName(type)
                          << ")");

    // Initialize MIDI routing for this track if audioEngine is available
    // Aux tracks never receive MIDI; other tracks rely on selection-based routing
    if (audioEngine_ && type != TrackType::Aux) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            midiBridge->setTrackMidiInput(trackId, "all");
            midiBridge->startMonitoring(trackId);
        }
        // Don't auto-route MIDI at the TE level for every new track.
        // AudioBridge::updateMidiRoutingForSelection() will handle this
        // based on whether the track is selected or record-armed.
    }

    return trackId;
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

TrackId TrackManager::duplicateTrack(TrackId trackId) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it == tracks_.end()) {
        return INVALID_TRACK_ID;
    }

    TrackInfo newTrack = *it;
    newTrack.id = nextTrackId_++;
    newTrack.name = it->name + " Copy";
    newTrack.childIds.clear();  // Don't duplicate children references

    TrackId newId = newTrack.id;

    // Insert after the original
    auto insertPos = it + 1;
    tracks_.insert(insertPos, newTrack);

    // If the original had a parent, add the copy to the same parent
    if (newTrack.hasParent()) {
        if (auto* parent = getTrack(newTrack.parentId)) {
            parent->childIds.push_back(newId);
        }
    }

    notifyTracksChanged();
    DBG("Duplicated track: " << newTrack.name << " (id=" << newId << ")");
    return newId;
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

    // Auto-route child's audio output to the group track
    track->audioOutputDevice = "track:" + juce::String(groupId);
    notifyTrackPropertyChanged(trackId);

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

    // Revert audio output to master when removed from group
    track->audioOutputDevice = "master";
    notifyTrackPropertyChanged(trackId);

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
        // Allow up to +6dB gain (10^(6/20) ≈ 2.0)
        track->volume = juce::jlimit(0.0f, 2.0f, volume);
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

void TrackManager::setAudioEngine(AudioEngine* audioEngine) {
    audioEngine_ = audioEngine;

    // Sync existing tracks' MIDI routing (in case tracks were created before engine was set)
    // Only set up MidiBridge monitoring; TE-level MIDI routing is handled by
    // AudioBridge::updateMidiRoutingForSelection() based on selection/arm state.
    if (audioEngine_) {
        for (const auto& track : tracks_) {
            if (!track.midiInputDevice.isEmpty() && track.type != TrackType::Aux) {
                if (auto* midiBridge = audioEngine_->getMidiBridge()) {
                    midiBridge->setTrackMidiInput(track.id, track.midiInputDevice);
                    midiBridge->startMonitoring(track.id);
                }
                DBG("Synced MIDI monitoring for track " << track.id << ": "
                                                        << track.midiInputDevice);
            }
        }
    }
}

void TrackManager::previewNote(TrackId trackId, int noteNumber, int velocity, bool isNoteOn) {
    DBG("TrackManager::previewNote - Track=" << trackId << ", Note=" << noteNumber << ", Velocity="
                                             << velocity << ", On=" << (isNoteOn ? "YES" : "NO"));

    // Forward to engine wrapper for playback through track's instruments
    if (audioEngine_) {
        auto* track = getTrack(trackId);
        if (track) {
            DBG("TrackManager: Found track, forwarding to engine");
            // Convert TrackId to engine track ID string
            audioEngine_->previewNoteOnTrack(std::to_string(trackId), noteNumber, velocity,
                                             isNoteOn);
        } else {
            DBG("TrackManager: WARNING - Track not found!");
        }
    } else {
        DBG("TrackManager: WARNING - No audio engine!");
    }
}

// ============================================================================
// Track Routing Setters
// ============================================================================

void TrackManager::setTrackMidiInput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    // Aux tracks never receive MIDI
    if (track->type == TrackType::Aux) {
        DBG("Cannot set MIDI input on aux track " << trackId);
        return;
    }

    DBG("TrackManager::setTrackMidiInput - trackId=" << trackId << " deviceId='" << deviceId
                                                     << "'");

    // Audio and MIDI input are mutually exclusive — clear audio input when enabling MIDI
    if (!deviceId.isEmpty() && !track->audioInputDevice.isEmpty()) {
        DBG("  -> Clearing audio input (mutually exclusive with MIDI)");
        setTrackAudioInput(trackId, "");
    }

    // Update track state
    track->midiInputDevice = deviceId;

    // Forward to MidiBridge for MIDI activity monitoring (UI indicators)
    if (audioEngine_) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            if (deviceId.isEmpty()) {
                midiBridge->clearTrackMidiInput(trackId);
                midiBridge->stopMonitoring(trackId);
            } else {
                midiBridge->setTrackMidiInput(trackId, deviceId);
                midiBridge->startMonitoring(trackId);
            }
        }

        // Forward to AudioBridge for Tracktion Engine MIDI routing (actual plugin input)
        if (auto* audioBridge = audioEngine_->getAudioBridge()) {
            // Convert our deviceId to AudioBridge format
            // "all" stays as "all", empty clears routing, otherwise use the device ID
            audioBridge->setTrackMidiInput(trackId, deviceId);
        }
    }

    // Notify listeners (inspector, track headers will update)
    notifyTrackPropertyChanged(trackId);
}

void TrackManager::setTrackMidiOutput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    DBG("TrackManager::setTrackMidiOutput - trackId=" << trackId << " deviceId='" << deviceId
                                                      << "'");

    // Update track state
    track->midiOutputDevice = deviceId;

    // TODO: Forward to MidiBridge when MIDI output routing is implemented

    // Notify listeners
    notifyTrackPropertyChanged(trackId);
}

void TrackManager::setTrackAudioInput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    DBG("TrackManager::setTrackAudioInput - trackId=" << trackId << " deviceId='" << deviceId
                                                      << "'");

    // Audio and MIDI input are mutually exclusive — clear MIDI input when enabling audio
    if (!deviceId.isEmpty() && !track->midiInputDevice.isEmpty()) {
        DBG("  -> Clearing MIDI input (mutually exclusive with audio)");
        setTrackMidiInput(trackId, "");
    }

    // Update track state
    track->audioInputDevice = deviceId;

    // Forward to AudioBridge for actual routing
    if (audioEngine_) {
        if (auto* audioBridge = audioEngine_->getAudioBridge()) {
            audioBridge->setTrackAudioInput(trackId, deviceId);
        }
    }

    // Notify listeners
    notifyTrackPropertyChanged(trackId);
}

void TrackManager::setTrackAudioOutput(TrackId trackId, const juce::String& routing) {
    auto* track = getTrack(trackId);
    if (!track) {
        return;
    }

    DBG("TrackManager::setTrackAudioOutput - trackId=" << trackId << " routing='" << routing
                                                       << "'");

    // Update track state
    track->audioOutputDevice = routing;

    // Forward to AudioBridge for actual routing
    if (audioEngine_) {
        if (auto* audioBridge = audioEngine_->getAudioBridge()) {
            audioBridge->setTrackAudioOutput(trackId, routing);
        }
    }

    // Notify listeners
    notifyTrackPropertyChanged(trackId);
}

// ============================================================================
// Send Management
// ============================================================================

void TrackManager::addSend(TrackId sourceTrackId, TrackId destAuxTrackId) {
    auto* source = getTrack(sourceTrackId);
    auto* dest = getTrack(destAuxTrackId);
    if (!source || !dest || dest->type != TrackType::Aux || dest->auxBusIndex < 0) {
        DBG("addSend failed: invalid source or destination");
        return;
    }

    // Check if send already exists
    for (const auto& send : source->sends) {
        if (send.busIndex == dest->auxBusIndex) {
            return;  // Already exists
        }
    }

    SendInfo send;
    send.busIndex = dest->auxBusIndex;
    send.level = 1.0f;
    send.preFader = false;
    send.destTrackId = destAuxTrackId;
    source->sends.push_back(send);

    notifyTrackDevicesChanged(sourceTrackId);
    DBG("Added send from track " << sourceTrackId << " to aux track " << destAuxTrackId << " (bus "
                                 << dest->auxBusIndex << ")");
}

void TrackManager::removeSend(TrackId sourceTrackId, int busIndex) {
    auto* source = getTrack(sourceTrackId);
    if (!source) {
        return;
    }

    auto& sends = source->sends;
    sends.erase(std::remove_if(sends.begin(), sends.end(),
                               [busIndex](const SendInfo& s) { return s.busIndex == busIndex; }),
                sends.end());

    notifyTrackDevicesChanged(sourceTrackId);
}

void TrackManager::setSendLevel(TrackId sourceTrackId, int busIndex, float level) {
    auto* source = getTrack(sourceTrackId);
    if (!source) {
        return;
    }

    for (auto& send : source->sends) {
        if (send.busIndex == busIndex) {
            send.level = level;
            notifyTrackPropertyChanged(sourceTrackId);
            return;
        }
    }
}

// ============================================================================
// Signal Chain Management (Unified)
// ============================================================================

const std::vector<ChainElement>& TrackManager::getChainElements(TrackId trackId) const {
    static const std::vector<ChainElement> empty;
    if (const auto* track = getTrack(trackId)) {
        return track->chainElements;
    }
    return empty;
}

void TrackManager::moveNode(TrackId trackId, int fromIndex, int toIndex) {
    DBG("TrackManager::moveNode trackId=" << trackId << " from=" << fromIndex << " to=" << toIndex);
    if (auto* track = getTrack(trackId)) {
        auto& elements = track->chainElements;
        int size = static_cast<int>(elements.size());
        DBG("  elements.size()=" << size);

        if (fromIndex >= 0 && fromIndex < size && toIndex >= 0 && toIndex < size &&
            fromIndex != toIndex) {
            DBG("  performing move!");
            ChainElement element = std::move(elements[fromIndex]);
            elements.erase(elements.begin() + fromIndex);
            elements.insert(elements.begin() + toIndex, std::move(element));
            notifyTrackDevicesChanged(trackId);
        } else {
            DBG("  NOT moving: invalid indices or same position");
        }
    }
}

// ============================================================================
// Device Management on Track
// ============================================================================

DeviceId TrackManager::addDeviceToTrack(TrackId trackId, const DeviceInfo& device) {
    if (auto* track = getTrack(trackId)) {
        if (track->type == TrackType::Aux && device.isInstrument) {
            DBG("Cannot add instrument plugin to aux track");
            return INVALID_DEVICE_ID;
        }
        DeviceInfo newDevice = device;
        newDevice.id = nextDeviceId_++;
        track->chainElements.push_back(makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        DBG("Added device: " << newDevice.name << " (id=" << newDevice.id << ") to track "
                             << trackId);
        return newDevice.id;
    }
    return INVALID_DEVICE_ID;
}

DeviceId TrackManager::addDeviceToTrack(TrackId trackId, const DeviceInfo& device,
                                        int insertIndex) {
    if (auto* track = getTrack(trackId)) {
        if (track->type == TrackType::Aux && device.isInstrument) {
            DBG("Cannot add instrument plugin to aux track");
            return INVALID_DEVICE_ID;
        }
        DeviceInfo newDevice = device;
        newDevice.id = nextDeviceId_++;

        // Clamp insert index to valid range
        int maxIndex = static_cast<int>(track->chainElements.size());
        insertIndex = std::clamp(insertIndex, 0, maxIndex);

        // Insert at specified position
        track->chainElements.insert(track->chainElements.begin() + insertIndex,
                                    makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        DBG("Added device: " << newDevice.name << " (id=" << newDevice.id << ") to track "
                             << trackId << " at index " << insertIndex);
        return newDevice.id;
    }
    return INVALID_DEVICE_ID;
}

void TrackManager::removeDeviceFromTrack(TrackId trackId, DeviceId deviceId) {
    if (auto* track = getTrack(trackId)) {
        auto& elements = track->chainElements;
        auto it = std::find_if(elements.begin(), elements.end(), [deviceId](const ChainElement& e) {
            return magda::isDevice(e) && magda::getDevice(e).id == deviceId;
        });
        if (it != elements.end()) {
            DBG("Removed device: " << magda::getDevice(*it).name << " (id=" << deviceId
                                   << ") from track " << trackId);
            elements.erase(it);
            notifyTrackDevicesChanged(trackId);
        }
    }
}

void TrackManager::setDeviceBypassed(TrackId trackId, DeviceId deviceId, bool bypassed) {
    if (auto* device = getDevice(trackId, deviceId)) {
        device->bypassed = bypassed;
        notifyTrackDevicesChanged(trackId);
    }
}

DeviceInfo* TrackManager::getDevice(TrackId trackId, DeviceId deviceId) {
    if (auto* track = getTrack(trackId)) {
        for (auto& element : track->chainElements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                return &magda::getDevice(element);
            }
        }
    }
    return nullptr;
}

// ============================================================================
// Rack Management on Track
// ============================================================================

RackId TrackManager::addRackToTrack(TrackId trackId, const juce::String& name) {
    if (auto* track = getTrack(trackId)) {
        RackInfo rack;
        rack.id = nextRackId_++;
        rack.name = name.isEmpty() ? ("Rack " + juce::String(rack.id)) : name;

        // Add a default chain to the new rack
        ChainInfo defaultChain;
        defaultChain.id = nextChainId_++;
        defaultChain.name = "Chain 1";
        rack.chains.push_back(std::move(defaultChain));

        RackId newRackId = rack.id;
        track->chainElements.push_back(makeRackElement(std::move(rack)));
        notifyTrackDevicesChanged(trackId);
        DBG("Added rack: " << name << " (id=" << newRackId << ") to track " << trackId);
        return newRackId;
    }
    return INVALID_RACK_ID;
}

void TrackManager::removeRackFromTrack(TrackId trackId, RackId rackId) {
    if (auto* track = getTrack(trackId)) {
        auto& elements = track->chainElements;
        auto it = std::find_if(elements.begin(), elements.end(), [rackId](const ChainElement& e) {
            return magda::isRack(e) && magda::getRack(e).id == rackId;
        });
        if (it != elements.end()) {
            DBG("Removed rack: " << magda::getRack(*it).name << " (id=" << rackId << ") from track "
                                 << trackId);
            elements.erase(it);
            notifyTrackDevicesChanged(trackId);
        }
    }
}

RackInfo* TrackManager::getRack(TrackId trackId, RackId rackId) {
    if (auto* track = getTrack(trackId)) {
        for (auto& element : track->chainElements) {
            if (magda::isRack(element) && magda::getRack(element).id == rackId) {
                return &magda::getRack(element);
            }
        }
    }
    return nullptr;
}

const RackInfo* TrackManager::getRack(TrackId trackId, RackId rackId) const {
    if (const auto* track = getTrack(trackId)) {
        for (const auto& element : track->chainElements) {
            if (magda::isRack(element) && magda::getRack(element).id == rackId) {
                return &magda::getRack(element);
            }
        }
    }
    return nullptr;
}

void TrackManager::setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) {
    if (auto* rack = getRack(trackId, rackId)) {
        rack->bypassed = bypassed;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setRackExpanded(TrackId trackId, RackId rackId, bool expanded) {
    if (auto* rack = getRack(trackId, rackId)) {
        rack->expanded = expanded;
        notifyTrackDevicesChanged(trackId);
    }
}

// ============================================================================
// Chain Management
// ============================================================================

RackInfo* TrackManager::getRackByPath(const ChainNodePath& rackPath) {
    auto* track = getTrack(rackPath.trackId);
    if (!track) {
        return nullptr;
    }

    RackInfo* currentRack = nullptr;
    ChainInfo* currentChain = nullptr;

    for (const auto& step : rackPath.steps) {
        switch (step.type) {
            case ChainStepType::Rack: {
                if (currentChain == nullptr) {
                    // Top-level rack in track's chainElements
                    for (auto& element : track->chainElements) {
                        if (magda::isRack(element)) {
                            if (magda::getRack(element).id == step.id) {
                                currentRack = &magda::getRack(element);
                                break;
                            }
                        }
                    }
                } else {
                    // Nested rack within a chain
                    for (auto& element : currentChain->elements) {
                        if (magda::isRack(element)) {
                            if (magda::getRack(element).id == step.id) {
                                currentRack = &magda::getRack(element);
                                currentChain = nullptr;  // Reset chain context
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case ChainStepType::Chain: {
                if (currentRack != nullptr) {
                    for (auto& chain : currentRack->chains) {
                        if (chain.id == step.id) {
                            currentChain = &chain;
                            break;
                        }
                    }
                }
                break;
            }
            case ChainStepType::Device:
                // Devices don't contain racks, skip
                break;
        }
    }

    return currentRack;
}

const RackInfo* TrackManager::getRackByPath(const ChainNodePath& rackPath) const {
    // const version - delegates to non-const via const_cast (safe since we return const*)
    return const_cast<TrackManager*>(this)->getRackByPath(rackPath);
}

ChainId TrackManager::addChainToRack(const ChainNodePath& rackPath, const juce::String& name) {
    if (auto* rack = getRackByPath(rackPath)) {
        ChainInfo chain;
        chain.id = nextChainId_++;
        chain.name = name.isEmpty()
                         ? ("Chain " + juce::String(static_cast<int>(rack->chains.size()) + 1))
                         : name;
        rack->chains.push_back(chain);
        notifyTrackDevicesChanged(rackPath.trackId);
        return chain.id;
    }
    return INVALID_CHAIN_ID;
}

void TrackManager::removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) {
    if (auto* rack = getRack(trackId, rackId)) {
        auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            DBG("Removed chain: " << it->name << " (id=" << chainId << ") from rack " << rackId);
            chains.erase(it);
            notifyTrackDevicesChanged(trackId);
        }
    }
}

void TrackManager::removeChainByPath(const ChainNodePath& chainPath) {
    // The chainPath should end with a Chain step - we need to find the parent rack
    if (chainPath.steps.empty()) {
        DBG("removeChainByPath FAILED - empty path!");
        return;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
        DBG("removeChainByPath FAILED - path doesn't end with Chain step!");
        return;
    }

    // Build path to parent rack (all steps except the last Chain step)
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i < chainPath.steps.size() - 1; ++i) {
        rackPath.steps.push_back(chainPath.steps[i]);
    }

    // Find the rack and remove the chain
    if (auto* rack = getRackByPath(rackPath)) {
        auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            DBG("Removed chain via path: " << it->name << " (id=" << chainId << ")");
            chains.erase(it);
            notifyTrackDevicesChanged(chainPath.trackId);
        }
    } else {
        DBG("removeChainByPath FAILED - rack not found via path!");
    }
}

ChainInfo* TrackManager::getChain(TrackId trackId, RackId rackId, ChainId chainId) {
    if (auto* rack = getRack(trackId, rackId)) {
        auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            return &(*it);
        }
    }
    return nullptr;
}

const ChainInfo* TrackManager::getChain(TrackId trackId, RackId rackId, ChainId chainId) const {
    if (const auto* rack = getRack(trackId, rackId)) {
        const auto& chains = rack->chains;
        auto it = std::find_if(chains.begin(), chains.end(),
                               [chainId](const ChainInfo& c) { return c.id == chainId; });
        if (it != chains.end()) {
            return &(*it);
        }
    }
    return nullptr;
}

void TrackManager::setChainOutput(TrackId trackId, RackId rackId, ChainId chainId,
                                  int outputIndex) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->outputIndex = outputIndex;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->muted = muted;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->solo = solo;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volume) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->volume = juce::jlimit(-60.0f, 6.0f, volume);  // dB range
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->pan = juce::jlimit(-1.0f, 1.0f, pan);
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setChainExpanded(TrackId trackId, RackId rackId, ChainId chainId,
                                    bool expanded) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        chain->expanded = expanded;
        notifyTrackDevicesChanged(trackId);
    }
}

// ============================================================================
// Path Resolution
// ============================================================================

TrackManager::ResolvedPath TrackManager::resolvePath(const ChainNodePath& path) const {
    ResolvedPath result;

    const auto* track = getTrack(path.trackId);
    if (!track) {
        return result;
    }

    // Handle top-level device (legacy)
    if (path.topLevelDeviceId != INVALID_DEVICE_ID) {
        for (const auto& element : track->chainElements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == path.topLevelDeviceId) {
                result.valid = true;
                result.device = &magda::getDevice(element);
                result.displayPath = result.device->name;
                return result;
            }
        }
        return result;
    }

    // Walk through the path steps
    juce::StringArray pathNames;
    const RackInfo* currentRack = nullptr;
    const ChainInfo* currentChain = nullptr;

    for (size_t i = 0; i < path.steps.size(); ++i) {
        const auto& step = path.steps[i];

        switch (step.type) {
            case ChainStepType::Rack: {
                if (currentChain == nullptr) {
                    // Top-level rack in track's chainElements
                    for (const auto& element : track->chainElements) {
                        if (magda::isRack(element) && magda::getRack(element).id == step.id) {
                            currentRack = &magda::getRack(element);
                            pathNames.add(currentRack->name);
                            break;
                        }
                    }
                } else {
                    // Nested rack within a chain
                    for (const auto& element : currentChain->elements) {
                        if (magda::isRack(element) && magda::getRack(element).id == step.id) {
                            currentRack = &magda::getRack(element);
                            currentChain = nullptr;  // Reset chain context
                            pathNames.add(currentRack->name);
                            break;
                        }
                    }
                }
                break;
            }
            case ChainStepType::Chain: {
                if (currentRack != nullptr) {
                    for (const auto& chain : currentRack->chains) {
                        if (chain.id == step.id) {
                            currentChain = &chain;
                            pathNames.add(chain.name);
                            break;
                        }
                    }
                }
                break;
            }
            case ChainStepType::Device: {
                if (currentChain != nullptr) {
                    for (const auto& element : currentChain->elements) {
                        if (magda::isDevice(element) && magda::getDevice(element).id == step.id) {
                            result.device = &magda::getDevice(element);
                            pathNames.add(result.device->name);
                            break;
                        }
                    }
                }
                break;
            }
        }
    }

    // Set result based on what we found
    if (!path.steps.empty()) {
        result.displayPath = pathNames.joinIntoString(" > ");
        result.rack = currentRack;
        result.chain = currentChain;
        result.valid = !pathNames.isEmpty();
    }

    return result;
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

void TrackManager::setSelectedChain(TrackId trackId, RackId rackId, ChainId chainId) {
    selectedChainTrackId_ = trackId;
    selectedChainRackId_ = rackId;
    selectedChainId_ = chainId;
}

void TrackManager::clearSelectedChain() {
    selectedChainTrackId_ = INVALID_TRACK_ID;
    selectedChainRackId_ = INVALID_RACK_ID;
    selectedChainId_ = INVALID_CHAIN_ID;
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
    nextDeviceId_ = 1;
    nextRackId_ = 1;
    nextChainId_ = 1;
    nextAuxBusIndex_ = 0;
    notifyTracksChanged();
}

void TrackManager::refreshIdCountersFromTracks() {
    int maxTrackId = 0;
    int maxDeviceId = 0;
    int maxRackId = 0;
    int maxChainId = 0;

    // Helper lambda to scan a chain element (device or rack)
    auto scanChainElement = [&](const ChainElement& element, auto& self) -> void {
        if (std::holds_alternative<DeviceInfo>(element)) {
            const auto& device = std::get<DeviceInfo>(element);
            maxDeviceId = std::max(maxDeviceId, device.id);
        } else if (std::holds_alternative<std::unique_ptr<RackInfo>>(element)) {
            const auto& rackPtr = std::get<std::unique_ptr<RackInfo>>(element);
            if (rackPtr) {
                maxRackId = std::max(maxRackId, rackPtr->id);

                // Scan all chains in the rack
                for (const auto& chain : rackPtr->chains) {
                    maxChainId = std::max(maxChainId, chain.id);

                    // Recursively scan elements in this chain
                    for (const auto& chainElement : chain.elements) {
                        self(chainElement, self);
                    }
                }
            }
        }
    };

    int maxAuxBusIndex = -1;

    // Scan all tracks
    for (const auto& track : tracks_) {
        maxTrackId = std::max(maxTrackId, track.id);

        if (track.auxBusIndex >= 0) {
            maxAuxBusIndex = std::max(maxAuxBusIndex, track.auxBusIndex);
        }

        // Scan the track's chain elements
        for (const auto& element : track.chainElements) {
            scanChainElement(element, scanChainElement);
        }
    }

    // Update counters to max + 1
    nextTrackId_ = maxTrackId + 1;
    nextDeviceId_ = maxDeviceId + 1;
    nextRackId_ = maxRackId + 1;
    nextChainId_ = maxChainId + 1;
    nextAuxBusIndex_ = maxAuxBusIndex + 1;
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

void TrackManager::notifyTrackDevicesChanged(TrackId trackId) {
    for (auto* listener : listeners_) {
        listener->trackDevicesChanged(trackId);
    }
}

void TrackManager::notifyDeviceModifiersChanged(TrackId trackId) {
    for (auto* listener : listeners_) {
        listener->deviceModifiersChanged(trackId);
    }
}

void TrackManager::notifyDevicePropertyChanged(DeviceId deviceId) {
    for (auto* listener : listeners_) {
        listener->devicePropertyChanged(deviceId);
    }
}

void TrackManager::notifyDeviceParameterChanged(DeviceId deviceId, int paramIndex, float newValue) {
    for (auto* listener : listeners_) {
        listener->deviceParameterChanged(deviceId, paramIndex, newValue);
    }
}

void TrackManager::notifyMacroValueChanged(TrackId trackId, bool isRack, int id, int macroIndex,
                                           float value) {
    for (auto* listener : listeners_) {
        listener->macroValueChanged(trackId, isRack, id, macroIndex, value);
    }
}

void TrackManager::updateRackMods(const RackInfo& rack, double deltaTime) {
    // TODO: Recursively update mods in rack, chains, and nested racks
    (void)rack;
    (void)deltaTime;
}

void TrackManager::notifyModulationChanged() {
    // Notify all listeners that modulation values have changed
    // This triggers parameter indicator repaints
    for (auto* listener : listeners_) {
        listener->tracksChanged();
    }
}

juce::String TrackManager::generateTrackName() const {
    return juce::String(tracks_.size() + 1) + " Track";
}

}  // namespace magda
