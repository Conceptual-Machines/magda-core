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
    track.midiInputDevice = "all";       // MIDI listens to all inputs
    track.audioInputDevice = "";         // Audio input disabled by default (enable via UI)
    // midiOutputDevice left empty - requires specific device selection

    TrackId trackId = track.id;
    tracks_.push_back(track);
    notifyTracksChanged();

    DBG("Created track: " << track.name << " (id=" << trackId << ", type=" << getTrackTypeName(type)
                          << ")");

    // Initialize MIDI routing for this track if audioEngine is available
    if (audioEngine_) {
        if (auto* midiBridge = audioEngine_->getMidiBridge()) {
            midiBridge->setTrackMidiInput(trackId, "all");
            midiBridge->startMonitoring(trackId);
        }
        // Route MIDI inputs at the TE level (creates InputDeviceInstance destinations
        // needed for recording and live monitoring through synth plugins)
        if (auto* audioBridge = audioEngine_->getAudioBridge()) {
            audioBridge->setTrackMidiInput(trackId, "all");
        }
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
    if (audioEngine_) {
        for (const auto& track : tracks_) {
            if (!track.midiInputDevice.isEmpty()) {
                if (auto* midiBridge = audioEngine_->getMidiBridge()) {
                    midiBridge->setTrackMidiInput(track.id, track.midiInputDevice);
                    midiBridge->startMonitoring(track.id);
                }
                if (auto* audioBridge = audioEngine_->getAudioBridge()) {
                    audioBridge->setTrackMidiInput(track.id, track.midiInputDevice);
                }
                DBG("Synced MIDI routing for track " << track.id << ": " << track.midiInputDevice);
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

    DBG("TrackManager::setTrackMidiInput - trackId=" << trackId << " deviceId='" << deviceId
                                                     << "'");

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
// Device Management Within Chains
// ============================================================================

DeviceId TrackManager::addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                                        const DeviceInfo& device) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        DeviceInfo newDevice = device;
        newDevice.id = nextDeviceId_++;
        chain->elements.push_back(makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        DBG("Added device: " << newDevice.name << " (id=" << newDevice.id << ") to chain "
                             << chainId);
        return newDevice.id;
    }
    return INVALID_DEVICE_ID;
}

DeviceId TrackManager::addDeviceToChainByPath(const ChainNodePath& chainPath,
                                              const DeviceInfo& device) {
    // The chainPath should end with a Chain step
    DBG("addDeviceToChainByPath called with path steps=" << chainPath.steps.size());

    if (chainPath.steps.empty()) {
        DBG("addDeviceToChainByPath FAILED - empty path!");
        return INVALID_DEVICE_ID;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
        DBG("addDeviceToChainByPath FAILED - path doesn't end with Chain step!");
        return INVALID_DEVICE_ID;
    }

    // Build the parent rack path (everything except the last Chain step)
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i < chainPath.steps.size() - 1; ++i) {
        rackPath.steps.push_back(chainPath.steps[i]);
    }

    // Get the parent rack
    if (auto* rack = getRackByPath(rackPath)) {
        // Find the chain within the rack
        ChainInfo* chain = nullptr;
        for (auto& c : rack->chains) {
            if (c.id == chainId) {
                chain = &c;
                break;
            }
        }

        if (!chain) {
            DBG("addDeviceToChainByPath FAILED - chain not found in rack!");
            return INVALID_DEVICE_ID;
        }

        // Add the device
        DeviceInfo newDevice = device;
        newDevice.id = nextDeviceId_++;
        chain->elements.push_back(makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(chainPath.trackId);
        DBG("Added device via path: " << newDevice.name << " (id=" << newDevice.id << ") to chain "
                                      << chainId);
        return newDevice.id;
    }

    DBG("addDeviceToChainByPath FAILED - rack not found via path!");
    return INVALID_DEVICE_ID;
}

DeviceId TrackManager::addDeviceToChainByPath(const ChainNodePath& chainPath,
                                              const DeviceInfo& device, int insertIndex) {
    // Similar to the non-indexed version but inserts at a specific position
    if (chainPath.steps.empty()) {
        DBG("addDeviceToChainByPath (indexed) FAILED - empty path!");
        return INVALID_DEVICE_ID;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
        DBG("addDeviceToChainByPath (indexed) FAILED - path doesn't end with Chain step!");
        return INVALID_DEVICE_ID;
    }

    // Build the parent rack path (everything except the last Chain step)
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i < chainPath.steps.size() - 1; ++i) {
        rackPath.steps.push_back(chainPath.steps[i]);
    }

    // Get the parent rack
    if (auto* rack = getRackByPath(rackPath)) {
        // Find the chain within the rack
        ChainInfo* chain = nullptr;
        for (auto& c : rack->chains) {
            if (c.id == chainId) {
                chain = &c;
                break;
            }
        }

        if (!chain) {
            DBG("addDeviceToChainByPath (indexed) FAILED - chain not found in rack!");
            return INVALID_DEVICE_ID;
        }

        // Add the device at the specified index
        DeviceInfo newDevice = device;
        newDevice.id = nextDeviceId_++;

        // Clamp insert index to valid range
        int maxIndex = static_cast<int>(chain->elements.size());
        insertIndex = std::clamp(insertIndex, 0, maxIndex);

        chain->elements.insert(chain->elements.begin() + insertIndex, makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(chainPath.trackId);
        DBG("Added device via path: " << newDevice.name << " (id=" << newDevice.id << ") to chain "
                                      << chainId << " at index " << insertIndex);
        return newDevice.id;
    }

    DBG("addDeviceToChainByPath (indexed) FAILED - rack not found via path!");
    return INVALID_DEVICE_ID;
}

void TrackManager::removeDeviceFromChain(TrackId trackId, RackId rackId, ChainId chainId,
                                         DeviceId deviceId) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        auto& elements = chain->elements;
        auto it = std::find_if(elements.begin(), elements.end(), [deviceId](const ChainElement& e) {
            return magda::isDevice(e) && magda::getDevice(e).id == deviceId;
        });
        if (it != elements.end()) {
            DBG("Removed device: " << magda::getDevice(*it).name << " (id=" << deviceId
                                   << ") from chain " << chainId);
            elements.erase(it);
            notifyTrackDevicesChanged(trackId);
        }
    }
}

void TrackManager::moveDeviceInChain(TrackId trackId, RackId rackId, ChainId chainId,
                                     DeviceId deviceId, int newIndex) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        auto& elements = chain->elements;
        auto it = std::find_if(elements.begin(), elements.end(), [deviceId](const ChainElement& e) {
            return magda::isDevice(e) && magda::getDevice(e).id == deviceId;
        });
        if (it != elements.end()) {
            int currentIndex = static_cast<int>(std::distance(elements.begin(), it));
            if (currentIndex != newIndex && newIndex >= 0 &&
                newIndex < static_cast<int>(elements.size())) {
                ChainElement element = std::move(*it);
                elements.erase(it);
                elements.insert(elements.begin() + newIndex, std::move(element));
                notifyTrackDevicesChanged(trackId);
            }
        }
    }
}

void TrackManager::moveElementInChainByPath(const ChainNodePath& chainPath, int fromIndex,
                                            int toIndex) {
    // The chainPath should end with a Chain step
    if (chainPath.steps.empty()) {
        DBG("moveElementInChainByPath FAILED - empty path!");
        return;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
        DBG("moveElementInChainByPath FAILED - path doesn't end with Chain step!");
        return;
    }

    // Build the parent rack path (everything except the last Chain step)
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i < chainPath.steps.size() - 1; ++i) {
        rackPath.steps.push_back(chainPath.steps[i]);
    }

    // Get the parent rack (mutable)
    RackInfo* rack = getRackByPath(rackPath);
    if (!rack) {
        DBG("moveElementInChainByPath FAILED - rack not found via path!");
        return;
    }

    // Find the chain within the rack
    ChainInfo* chain = nullptr;
    for (auto& c : rack->chains) {
        if (c.id == chainId) {
            chain = &c;
            break;
        }
    }

    if (!chain) {
        DBG("moveElementInChainByPath FAILED - chain not found in rack!");
        return;
    }

    auto& elements = chain->elements;
    int size = static_cast<int>(elements.size());

    if (fromIndex >= 0 && fromIndex < size && toIndex >= 0 && toIndex < size &&
        fromIndex != toIndex) {
        ChainElement element = std::move(elements[fromIndex]);
        elements.erase(elements.begin() + fromIndex);
        elements.insert(elements.begin() + toIndex, std::move(element));
        notifyTrackDevicesChanged(chainPath.trackId);
    }
}

DeviceInfo* TrackManager::getDeviceInChain(TrackId trackId, RackId rackId, ChainId chainId,
                                           DeviceId deviceId) {
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        for (auto& element : chain->elements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                return &magda::getDevice(element);
            }
        }
    }
    return nullptr;
}

void TrackManager::setDeviceInChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                            DeviceId deviceId, bool bypassed) {
    if (auto* device = getDeviceInChain(trackId, rackId, chainId, deviceId)) {
        device->bypassed = bypassed;
        notifyTrackDevicesChanged(trackId);
    }
}

// Helper to get chain from a path that ends with Chain step
static ChainInfo* getChainFromPath(TrackManager& tm, const ChainNodePath& chainPath) {
    if (chainPath.steps.empty())
        return nullptr;

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
        return nullptr;
    }

    // Build the parent rack path
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i < chainPath.steps.size() - 1; ++i) {
        rackPath.steps.push_back(chainPath.steps[i]);
    }

    // Get the parent rack and find the chain
    if (auto* rack = tm.getRackByPath(rackPath)) {
        for (auto& c : rack->chains) {
            if (c.id == chainId) {
                return &c;
            }
        }
    }
    return nullptr;
}

void TrackManager::removeDeviceFromChainByPath(const ChainNodePath& devicePath) {
    // Handle top-level device (uses topLevelDeviceId field)
    if (devicePath.topLevelDeviceId != INVALID_DEVICE_ID) {
        auto* track = getTrack(devicePath.trackId);
        if (!track)
            return;
        auto& elements = track->chainElements;
        auto it =
            std::find_if(elements.begin(), elements.end(), [&devicePath](const ChainElement& e) {
                return magda::isDevice(e) && magda::getDevice(e).id == devicePath.topLevelDeviceId;
            });
        if (it != elements.end()) {
            DBG("Removed top-level device: " << magda::getDevice(*it).name
                                             << " (id=" << devicePath.topLevelDeviceId << ")");
            elements.erase(it);
            notifyTrackDevicesChanged(devicePath.trackId);
        }
        return;
    }

    // Handle nested device (uses steps vector ending with Device step)
    if (devicePath.steps.empty())
        return;

    DeviceId deviceId = INVALID_DEVICE_ID;
    if (devicePath.steps.back().type == ChainStepType::Device) {
        deviceId = devicePath.steps.back().id;
    } else {
        DBG("removeDeviceFromChainByPath FAILED - path doesn't end with Device step!");
        return;
    }

    // Build chain path (everything except last Device step)
    ChainNodePath chainPath;
    chainPath.trackId = devicePath.trackId;
    for (size_t i = 0; i < devicePath.steps.size() - 1; ++i) {
        chainPath.steps.push_back(devicePath.steps[i]);
    }

    if (auto* chain = getChainFromPath(*this, chainPath)) {
        auto& elements = chain->elements;
        auto it = std::find_if(elements.begin(), elements.end(), [deviceId](const ChainElement& e) {
            return magda::isDevice(e) && magda::getDevice(e).id == deviceId;
        });
        if (it != elements.end()) {
            DBG("Removed nested device via path: " << magda::getDevice(*it).name
                                                   << " (id=" << deviceId << ")");
            elements.erase(it);
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

DeviceInfo* TrackManager::getDeviceInChainByPath(const ChainNodePath& devicePath) {
    // Handle top-level device (legacy path format with topLevelDeviceId)
    if (devicePath.topLevelDeviceId != INVALID_DEVICE_ID) {
        auto* track = getTrack(devicePath.trackId);
        if (!track)
            return nullptr;
        for (auto& element : track->chainElements) {
            if (magda::isDevice(element) &&
                magda::getDevice(element).id == devicePath.topLevelDeviceId) {
                return &magda::getDevice(element);
            }
        }
        return nullptr;
    }

    // devicePath ends with a Device step
    if (devicePath.steps.empty()) {
        return nullptr;
    }

    DeviceId deviceId = INVALID_DEVICE_ID;
    if (devicePath.steps.back().type == ChainStepType::Device) {
        deviceId = devicePath.steps.back().id;
    } else {
        return nullptr;
    }

    // Build chain path (all steps except the last Device step)
    ChainNodePath chainPath;
    chainPath.trackId = devicePath.trackId;
    for (size_t i = 0; i < devicePath.steps.size() - 1; ++i) {
        chainPath.steps.push_back(devicePath.steps[i]);
    }

    // If chainPath is empty, device is at top-level of track
    if (chainPath.steps.empty()) {
        auto* track = getTrack(devicePath.trackId);
        if (!track)
            return nullptr;
        for (auto& element : track->chainElements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                return &magda::getDevice(element);
            }
        }
        return nullptr;
    }

    // Otherwise, device is inside a chain
    if (auto* chain = getChainFromPath(*this, chainPath)) {
        for (auto& element : chain->elements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                return &magda::getDevice(element);
            }
        }
    }
    return nullptr;
}

void TrackManager::setDeviceInChainBypassedByPath(const ChainNodePath& devicePath, bool bypassed) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->bypassed = bypassed;
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceGainDb(const ChainNodePath& devicePath, float gainDb) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->gainDb = gainDb;
        // Convert dB to linear: 10^(dB/20)
        device->gainValue = std::pow(10.0f, gainDb / 20.0f);
        notifyDevicePropertyChanged(device->id);
    }
}

void TrackManager::setDeviceLevel(const ChainNodePath& devicePath, float level) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->gainValue = level;
        // Convert linear to dB: 20 * log10(level)
        device->gainDb = (level > 0.0f) ? 20.0f * std::log10(level) : -100.0f;
        notifyDevicePropertyChanged(device->id);
    }
}

void TrackManager::updateDeviceParameters(DeviceId deviceId,
                                          const std::vector<ParameterInfo>& params) {
    // Search all tracks for the device and update its parameters
    for (auto& track : tracks_) {
        for (auto& element : track.chainElements) {
            if (std::holds_alternative<DeviceInfo>(element)) {
                auto& device = std::get<DeviceInfo>(element);
                if (device.id == deviceId) {
                    device.parameters = params;
                    // Don't notify - this is called during device loading, not user interaction
                    return;
                }
            }
        }
    }
}

void TrackManager::setDeviceVisibleParameters(DeviceId deviceId,
                                              const std::vector<int>& visibleParams) {
    // Search all tracks for the device and update visible parameters
    for (auto& track : tracks_) {
        for (auto& element : track.chainElements) {
            if (std::holds_alternative<DeviceInfo>(element)) {
                auto& device = std::get<DeviceInfo>(element);
                if (device.id == deviceId) {
                    device.visibleParameters = visibleParams;
                    // Don't notify - this is called during device loading, not user interaction
                    return;
                }
            }
        }
    }
}

void TrackManager::setDeviceParameterValue(const ChainNodePath& devicePath, int paramIndex,
                                           float value) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (paramIndex >= 0 && paramIndex < static_cast<int>(device->parameters.size())) {
            device->parameters[static_cast<size_t>(paramIndex)].currentValue = value;
            // Use granular notification - only sync this one parameter, not all 543
            notifyDeviceParameterChanged(device->id, paramIndex, value);
        }
    }
}

void TrackManager::setDeviceParameterValueFromPlugin(const ChainNodePath& devicePath,
                                                     int paramIndex, float value) {
    // This method is called when the plugin's native UI changes a parameter.
    // It updates the DeviceInfo but does NOT call notifyDevicePropertyChanged()
    // to avoid triggering AudioBridge sync (which would cause a feedback loop).
    //
    // Instead, we notify UI listeners directly about the parameter change.

    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (paramIndex >= 0 && paramIndex < static_cast<int>(device->parameters.size())) {
            device->parameters[static_cast<size_t>(paramIndex)].currentValue = value;

            // Notify listeners about parameter change (for UI updates)
            notifyDeviceParameterChanged(device->id, paramIndex, value);

            // Also notify modulation system for display updates
            notifyModulationChanged();
        }
    }
}

RackId TrackManager::addRackToChain(TrackId trackId, RackId parentRackId, ChainId chainId,
                                    const juce::String& name) {
    if (auto* chain = getChain(trackId, parentRackId, chainId)) {
        RackInfo nestedRack;
        nestedRack.id = nextRackId_++;
        nestedRack.name = name.isEmpty() ? "Rack " + juce::String(nestedRack.id) : name;

        // Add a default chain to the nested rack
        ChainInfo defaultChain;
        defaultChain.id = nextChainId_++;
        defaultChain.name = "Chain 1";
        nestedRack.chains.push_back(std::move(defaultChain));

        RackId newRackId = nestedRack.id;
        chain->elements.push_back(makeRackElement(std::move(nestedRack)));

        notifyTrackDevicesChanged(trackId);
        DBG("Added nested rack: " << name << " (id=" << newRackId << ") to chain " << chainId);
        return newRackId;
    }
    return INVALID_RACK_ID;
}

RackId TrackManager::addRackToChainByPath(const ChainNodePath& chainPath,
                                          const juce::String& name) {
    // The chainPath should end with a Chain step - we add a rack to that chain
    DBG("addRackToChainByPath called with path steps=" << chainPath.steps.size());
    for (size_t i = 0; i < chainPath.steps.size(); ++i) {
        DBG("  step[" << i << "]: type=" << static_cast<int>(chainPath.steps[i].type)
                      << ", id=" << chainPath.steps[i].id);
    }

    if (chainPath.steps.empty()) {
        DBG("addRackToChainByPath FAILED - empty path!");
        return INVALID_RACK_ID;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
        DBG("addRackToChainByPath FAILED - path doesn't end with Chain step!");
        return INVALID_RACK_ID;
    }

    // Build the parent rack path (everything except the last Chain step)
    ChainNodePath rackPath;
    rackPath.trackId = chainPath.trackId;
    for (size_t i = 0; i < chainPath.steps.size() - 1; ++i) {
        rackPath.steps.push_back(chainPath.steps[i]);
    }

    // Get the parent rack
    if (auto* rack = getRackByPath(rackPath)) {
        // Find the chain within the rack
        ChainInfo* chain = nullptr;
        for (auto& c : rack->chains) {
            if (c.id == chainId) {
                chain = &c;
                break;
            }
        }

        if (!chain) {
            DBG("addRackToChainByPath FAILED - chain not found in rack!");
            return INVALID_RACK_ID;
        }

        // Create the nested rack
        RackInfo nestedRack;
        nestedRack.id = nextRackId_++;
        nestedRack.name = name.isEmpty() ? "Rack " + juce::String(nestedRack.id) : name;

        // Add a default chain to the nested rack
        ChainInfo defaultChain;
        defaultChain.id = nextChainId_++;
        defaultChain.name = "Chain 1";
        nestedRack.chains.push_back(std::move(defaultChain));

        RackId newRackId = nestedRack.id;
        chain->elements.push_back(makeRackElement(std::move(nestedRack)));

        notifyTrackDevicesChanged(chainPath.trackId);
        DBG("Added nested rack via path: " << nestedRack.name << " (id=" << newRackId
                                           << ") to chain " << chainId);
        return newRackId;
    }

    DBG("addRackToChainByPath FAILED - rack not found via path!");
    return INVALID_RACK_ID;
}

void TrackManager::removeRackFromChain(TrackId trackId, RackId parentRackId, ChainId chainId,
                                       RackId nestedRackId) {
    DBG("removeRackFromChain: trackId=" << trackId << " parentRackId=" << parentRackId
                                        << " chainId=" << chainId
                                        << " nestedRackId=" << nestedRackId);
    if (auto* chain = getChain(trackId, parentRackId, chainId)) {
        DBG("  found chain with " << chain->elements.size() << " elements");
        auto& elements = chain->elements;
        for (auto it = elements.begin(); it != elements.end(); ++it) {
            if (magda::isRack(*it)) {
                DBG("    checking rack element id=" << magda::getRack(*it).id);
                if (magda::getRack(*it).id == nestedRackId) {
                    elements.erase(it);
                    notifyTrackDevicesChanged(trackId);
                    DBG("Removed nested rack: " << nestedRackId << " from chain " << chainId);
                    return;
                }
            }
        }
        DBG("  nested rack not found in chain elements");
    } else {
        DBG("  FAILED: chain not found");
    }
}

void TrackManager::removeRackFromChainByPath(const ChainNodePath& rackPath) {
    // rackPath ends with a Rack step - we need to find the parent chain and remove this rack
    DBG("removeRackFromChainByPath: path steps=" << rackPath.steps.size());
    for (size_t i = 0; i < rackPath.steps.size(); ++i) {
        DBG("  step[" << i << "]: type=" << static_cast<int>(rackPath.steps[i].type)
                      << ", id=" << rackPath.steps[i].id);
    }

    if (rackPath.steps.size() < 2) {
        DBG("removeRackFromChainByPath FAILED - path too short (need at least Chain > Rack)!");
        return;
    }

    // Extract rackId from the last step (should be Rack type)
    RackId rackId = INVALID_RACK_ID;
    if (rackPath.steps.back().type == ChainStepType::Rack) {
        rackId = rackPath.steps.back().id;
    } else {
        DBG("removeRackFromChainByPath FAILED - path doesn't end with Rack step!");
        return;
    }

    // Build the parent chain path (everything except the last Rack step)
    ChainNodePath chainPath;
    chainPath.trackId = rackPath.trackId;
    for (size_t i = 0; i < rackPath.steps.size() - 1; ++i) {
        chainPath.steps.push_back(rackPath.steps[i]);
    }

    // Get the parent chain using path-based lookup
    if (auto* chain = getChainFromPath(*this, chainPath)) {
        DBG("  found chain via path with " << chain->elements.size() << " elements");
        auto& elements = chain->elements;
        for (auto it = elements.begin(); it != elements.end(); ++it) {
            if (magda::isRack(*it)) {
                DBG("    checking rack element id=" << magda::getRack(*it).id);
                if (magda::getRack(*it).id == rackId) {
                    elements.erase(it);
                    notifyTrackDevicesChanged(rackPath.trackId);
                    DBG("Removed nested rack via path: " << rackId);
                    return;
                }
            }
        }
        DBG("  nested rack not found in chain elements");
    } else {
        DBG("  FAILED: chain not found via path!");
    }
}

// ============================================================================
// Macro Management
// ============================================================================

void TrackManager::setRackMacroValue(const ChainNodePath& rackPath, int macroIndex, float value) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        rack->macros[macroIndex].value = juce::jlimit(0.0f, 1.0f, value);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackMacroTarget(const ChainNodePath& rackPath, int macroIndex,
                                      MacroTarget target) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        rack->macros[macroIndex].target = target;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackMacroName(const ChainNodePath& rackPath, int macroIndex,
                                    const juce::String& name) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        rack->macros[macroIndex].name = name;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackMacroLinkAmount(const ChainNodePath& rackPath, int macroIndex,
                                          MacroTarget target, float amount) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        if (auto* link = rack->macros[macroIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            MacroLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            rack->macros[macroIndex].links.push_back(newLink);
        }
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::addRackMacroPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        addMacroPage(rack->macros);
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::removeRackMacroPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (removeMacroPage(rack->macros)) {
            notifyTrackDevicesChanged(rackPath.trackId);
        }
    }
}

// ============================================================================
// Mod Management
// ============================================================================

void TrackManager::setRackModAmount(const ChainNodePath& rackPath, int modIndex, float amount) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].amount = juce::jlimit(0.0f, 1.0f, amount);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModTarget(const ChainNodePath& rackPath, int modIndex, ModTarget target) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].target = target;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModLinkAmount(const ChainNodePath& rackPath, int modIndex,
                                        ModTarget target, float amount) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        if (auto* link = rack->mods[modIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            ModLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            rack->mods[modIndex].links.push_back(newLink);
        }
        // Also update legacy amount if target matches
        if (rack->mods[modIndex].target == target) {
            rack->mods[modIndex].amount = amount;
        }
    }
}

void TrackManager::setRackModName(const ChainNodePath& rackPath, int modIndex,
                                  const juce::String& name) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].name = name;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModType(const ChainNodePath& rackPath, int modIndex, ModType type) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].type = type;
        // Update name to default for new type if it was default
        auto defaultOldName = ModInfo::getDefaultName(modIndex, rack->mods[modIndex].type);
        if (rack->mods[modIndex].name == defaultOldName) {
            rack->mods[modIndex].name = ModInfo::getDefaultName(modIndex, type);
        }
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModWaveform(const ChainNodePath& rackPath, int modIndex,
                                      LFOWaveform waveform) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].waveform = waveform;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModRate(const ChainNodePath& rackPath, int modIndex, float rate) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].rate = rate;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModPhaseOffset(const ChainNodePath& rackPath, int modIndex,
                                         float phaseOffset) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModTempoSync(const ChainNodePath& rackPath, int modIndex,
                                       bool tempoSync) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].tempoSync = tempoSync;
    }
}

void TrackManager::setRackModSyncDivision(const ChainNodePath& rackPath, int modIndex,
                                          SyncDivision division) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].syncDivision = division;
    }
}

void TrackManager::setRackModTriggerMode(const ChainNodePath& rackPath, int modIndex,
                                         LFOTriggerMode mode) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].triggerMode = mode;
    }
}

void TrackManager::setRackModCurvePreset(const ChainNodePath& rackPath, int modIndex,
                                         CurvePreset preset) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].curvePreset = preset;
    }
}

void TrackManager::addRackMod(const ChainNodePath& rackPath, int slotIndex, ModType type,
                              LFOWaveform waveform) {
    if (auto* rack = getRackByPath(rackPath)) {
        // Add a single mod at the specified slot index
        if (slotIndex >= 0 && slotIndex <= static_cast<int>(rack->mods.size())) {
            ModInfo newMod(slotIndex);
            newMod.type = type;
            newMod.waveform = waveform;
            // Use "Curve" name for custom waveform
            if (waveform == LFOWaveform::Custom) {
                newMod.name = "Curve " + juce::String(slotIndex + 1);
            } else {
                newMod.name = ModInfo::getDefaultName(slotIndex, type);
            }
            rack->mods.insert(rack->mods.begin() + slotIndex, newMod);

            // Update IDs for mods after the inserted one
            for (int i = slotIndex + 1; i < static_cast<int>(rack->mods.size()); ++i) {
                rack->mods[i].id = i;
            }

            // Don't notify - caller handles UI update to avoid panel closing
        }
    }
}

void TrackManager::removeRackMod(const ChainNodePath& rackPath, int modIndex) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(rack->mods.size())) {
            rack->mods.erase(rack->mods.begin() + modIndex);

            // Update IDs for remaining mods
            for (int i = modIndex; i < static_cast<int>(rack->mods.size()); ++i) {
                rack->mods[i].id = i;
                rack->mods[i].name = ModInfo::getDefaultName(i, rack->mods[i].type);
            }

            notifyTrackDevicesChanged(rackPath.trackId);
        }
    }
}

void TrackManager::setRackModEnabled(const ChainNodePath& rackPath, int modIndex, bool enabled) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(rack->mods.size())) {
            rack->mods[modIndex].enabled = enabled;
            notifyTrackDevicesChanged(rackPath.trackId);
        }
    }
}

void TrackManager::addRackModPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        addModPage(rack->mods);
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::removeRackModPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (removeModPage(rack->mods)) {
            notifyTrackDevicesChanged(rackPath.trackId);
        }
    }
}

// ============================================================================
// Device Mod Management
// ============================================================================

void TrackManager::setDeviceModAmount(const ChainNodePath& devicePath, int modIndex, float amount) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].amount = juce::jlimit(0.0f, 1.0f, amount);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setDeviceModTarget(const ChainNodePath& devicePath, int modIndex,
                                      ModTarget target) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        if (target.isValid()) {
            // Add link with default amount
            device->mods[modIndex].addLink(target, 0.5f);
        }
        // Also set legacy target for backward compatibility
        device->mods[modIndex].target = target;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::removeDeviceModLink(const ChainNodePath& devicePath, int modIndex,
                                       ModTarget target) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].removeLink(target);
        // Clear legacy target if it matches
        if (device->mods[modIndex].target == target) {
            device->mods[modIndex].target = ModTarget{};
        }
    }
}

void TrackManager::setDeviceModLinkAmount(const ChainNodePath& devicePath, int modIndex,
                                          ModTarget target, float amount) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        if (auto* link = device->mods[modIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            ModLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            device->mods[modIndex].links.push_back(newLink);
        }
        // Also update legacy amount if target matches
        if (device->mods[modIndex].target == target) {
            device->mods[modIndex].amount = amount;
        }
    }
}

void TrackManager::setDeviceModName(const ChainNodePath& devicePath, int modIndex,
                                    const juce::String& name) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].name = name;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setDeviceModType(const ChainNodePath& devicePath, int modIndex, ModType type) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        auto oldType = device->mods[modIndex].type;
        device->mods[modIndex].type = type;
        // Update name to default for new type if it was default
        auto defaultOldName = ModInfo::getDefaultName(modIndex, oldType);
        if (device->mods[modIndex].name == defaultOldName) {
            device->mods[modIndex].name = ModInfo::getDefaultName(modIndex, type);
        }
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setDeviceModWaveform(const ChainNodePath& devicePath, int modIndex,
                                        LFOWaveform waveform) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].waveform = waveform;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setDeviceModRate(const ChainNodePath& devicePath, int modIndex, float rate) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].rate = rate;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setDeviceModPhaseOffset(const ChainNodePath& devicePath, int modIndex,
                                           float phaseOffset) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setDeviceModTempoSync(const ChainNodePath& devicePath, int modIndex,
                                         bool tempoSync) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].tempoSync = tempoSync;
    }
}

void TrackManager::setDeviceModSyncDivision(const ChainNodePath& devicePath, int modIndex,
                                            SyncDivision division) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].syncDivision = division;
    }
}

void TrackManager::setDeviceModTriggerMode(const ChainNodePath& devicePath, int modIndex,
                                           LFOTriggerMode mode) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].triggerMode = mode;
    }
}

void TrackManager::setDeviceModCurvePreset(const ChainNodePath& devicePath, int modIndex,
                                           CurvePreset preset) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(device->mods.size())) {
            return;
        }
        device->mods[modIndex].curvePreset = preset;
    }
}

void TrackManager::addDeviceMod(const ChainNodePath& devicePath, int slotIndex, ModType type,
                                LFOWaveform waveform) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        // Add a single mod at the specified slot index
        if (slotIndex >= 0 && slotIndex <= static_cast<int>(device->mods.size())) {
            ModInfo newMod(slotIndex);
            newMod.type = type;
            newMod.waveform = waveform;
            // Use "Curve" name for custom waveform
            if (waveform == LFOWaveform::Custom) {
                newMod.name = "Curve " + juce::String(slotIndex + 1);
            } else {
                newMod.name = ModInfo::getDefaultName(slotIndex, type);
            }
            device->mods.insert(device->mods.begin() + slotIndex, newMod);

            // Update IDs for mods after the inserted one
            for (int i = slotIndex + 1; i < static_cast<int>(device->mods.size()); ++i) {
                device->mods[i].id = i;
            }

            // Don't notify - caller handles UI update to avoid panel closing
        }
    }
}

void TrackManager::removeDeviceMod(const ChainNodePath& devicePath, int modIndex) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(device->mods.size())) {
            device->mods.erase(device->mods.begin() + modIndex);

            // Update IDs for remaining mods
            for (int i = modIndex; i < static_cast<int>(device->mods.size()); ++i) {
                device->mods[i].id = i;
                device->mods[i].name = ModInfo::getDefaultName(i, device->mods[i].type);
            }

            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

void TrackManager::setDeviceModEnabled(const ChainNodePath& devicePath, int modIndex,
                                       bool enabled) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(device->mods.size())) {
            device->mods[modIndex].enabled = enabled;
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

void TrackManager::addDeviceModPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        addModPage(device->mods);
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::removeDeviceModPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (removeModPage(device->mods)) {
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

void TrackManager::updateAllMods(double deltaTime, double bpm, bool transportJustStarted,
                                 bool transportJustLooped) {
    // Lambda to update a single mod's phase and value
    auto updateMod = [deltaTime, bpm, transportJustStarted, transportJustLooped](ModInfo& mod) {
        // Skip disabled mods - set value to 0 so they don't affect modulation
        if (!mod.enabled) {
            mod.value = 0.0f;
            mod.triggered = false;
            return;
        }

        if (mod.type == ModType::LFO) {
            // Check for trigger (phase reset)
            bool shouldTrigger = false;
            switch (mod.triggerMode) {
                case LFOTriggerMode::Free:
                    // Never reset
                    break;
                case LFOTriggerMode::Transport:
                    // Reset on transport start or loop
                    if (transportJustStarted || transportJustLooped) {
                        shouldTrigger = true;
                    }
                    break;
                case LFOTriggerMode::MIDI:
                    // STUB: Will trigger on MIDI note-on when infrastructure ready
                    break;
                case LFOTriggerMode::Audio:
                    // STUB: Will trigger on audio transient when infrastructure ready
                    break;
            }

            if (shouldTrigger) {
                mod.phase = 0.0f;  // Reset to start
                mod.triggered = true;
            } else {
                mod.triggered = false;
            }

            // Calculate effective rate (Hz or tempo-synced)
            float effectiveRate = mod.rate;
            if (mod.tempoSync) {
                effectiveRate = ModulatorEngine::calculateSyncRateHz(mod.syncDivision, bpm);
            }

            // Update phase (wraps at 1.0)
            mod.phase += static_cast<float>(effectiveRate * deltaTime);
            while (mod.phase >= 1.0f) {
                mod.phase -= 1.0f;
            }
            // Apply phase offset when generating waveform
            float effectivePhase = std::fmod(mod.phase + mod.phaseOffset, 1.0f);
            // Generate waveform output using ModulatorEngine (handles Custom curves too)
            mod.value = ModulatorEngine::generateWaveformForMod(mod, effectivePhase);
        }
    };

    // Recursive lambda to update mods in chain elements
    std::function<void(ChainElement&)> updateElementMods = [&](ChainElement& element) {
        if (isDevice(element)) {
            // Update device mods
            DeviceInfo& device = magda::getDevice(element);
            for (auto& mod : device.mods) {
                updateMod(mod);
            }
        } else if (isRack(element)) {
            // Update rack mods
            RackInfo& rack = magda::getRack(element);
            for (auto& mod : rack.mods) {
                updateMod(mod);
            }
            // Recursively update mods in nested chains
            for (auto& chain : rack.chains) {
                for (auto& chainElement : chain.elements) {
                    updateElementMods(chainElement);
                }
            }
        }
    };

    // Update mods in all tracks
    for (auto& track : tracks_) {
        // Update mods in all chain elements
        for (auto& element : track.chainElements) {
            updateElementMods(element);
        }
    }

    // DO NOT call notifyModulationChanged() here - that causes 60 FPS UI rebuilds
    // ParamSlotComponent will read mod.value directly during its paint cycle
}

// ============================================================================
// Device Macro Management
// ============================================================================

void TrackManager::setDeviceMacroValue(const ChainNodePath& devicePath, int macroIndex,
                                       float value) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        device->macros[macroIndex].value = juce::jlimit(0.0f, 1.0f, value);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setDeviceMacroTarget(const ChainNodePath& devicePath, int macroIndex,
                                        MacroTarget target) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }

        // Add to links vector if not already present
        if (!device->macros[macroIndex].getLink(target)) {
            MacroLink newLink;
            newLink.target = target;
            newLink.amount = 0.5f;  // Default amount
            device->macros[macroIndex].links.push_back(newLink);
        }
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::removeDeviceMacroLink(const ChainNodePath& devicePath, int macroIndex,
                                         MacroTarget target) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        device->macros[macroIndex].removeLink(target);
    }
}

void TrackManager::setDeviceMacroLinkAmount(const ChainNodePath& devicePath, int macroIndex,
                                            MacroTarget target, float amount) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        if (auto* link = device->macros[macroIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            MacroLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            device->macros[macroIndex].links.push_back(newLink);
        }
    }
}

void TrackManager::setDeviceMacroName(const ChainNodePath& devicePath, int macroIndex,
                                      const juce::String& name) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        device->macros[macroIndex].name = name;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::addDeviceMacroPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        addMacroPage(device->macros);
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::removeDeviceMacroPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (removeMacroPage(device->macros)) {
            notifyTrackDevicesChanged(devicePath.trackId);
        }
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

    // Scan all tracks
    for (const auto& track : tracks_) {
        maxTrackId = std::max(maxTrackId, track.id);

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
