#include "TrackManager.hpp"

#include <algorithm>

namespace magda {

TrackManager& TrackManager::getInstance() {
    static TrackManager instance;
    return instance;
}

TrackManager::TrackManager() {
    // Create 11 default tracks to match test meter levels (6 to -60 dB)
    createDefaultTracks(11);
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
    DBG("getRackByPath: trackId=" << rackPath.trackId << ", steps=" << rackPath.steps.size());
    for (size_t i = 0; i < rackPath.steps.size(); ++i) {
        const auto& step = rackPath.steps[i];
        DBG("  step[" << i << "]: type=" << static_cast<int>(step.type) << ", id=" << step.id);
    }

    auto* track = getTrack(rackPath.trackId);
    if (!track) {
        DBG("  -> track not found!");
        return nullptr;
    }

    RackInfo* currentRack = nullptr;
    ChainInfo* currentChain = nullptr;

    for (const auto& step : rackPath.steps) {
        switch (step.type) {
            case ChainStepType::Rack: {
                if (currentChain == nullptr) {
                    // Top-level rack in track's chainElements
                    DBG("  Looking for top-level rack id=" << step.id << " in track with "
                                                           << track->chainElements.size()
                                                           << " elements");
                    for (auto& element : track->chainElements) {
                        if (magda::isRack(element)) {
                            DBG("    checking rack id=" << magda::getRack(element).id);
                            if (magda::getRack(element).id == step.id) {
                                currentRack = &magda::getRack(element);
                                DBG("    -> FOUND top-level rack");
                                break;
                            }
                        }
                    }
                } else {
                    // Nested rack within a chain
                    DBG("  Looking for nested rack id=" << step.id << " in chain with "
                                                        << currentChain->elements.size()
                                                        << " elements");
                    for (auto& element : currentChain->elements) {
                        if (magda::isRack(element)) {
                            DBG("    checking nested rack id=" << magda::getRack(element).id);
                            if (magda::getRack(element).id == step.id) {
                                currentRack = &magda::getRack(element);
                                currentChain = nullptr;  // Reset chain context
                                DBG("    -> FOUND nested rack");
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case ChainStepType::Chain: {
                if (currentRack != nullptr) {
                    DBG("  Looking for chain id=" << step.id << " in rack with "
                                                  << currentRack->chains.size() << " chains");
                    for (auto& chain : currentRack->chains) {
                        DBG("    checking chain id=" << chain.id);
                        if (chain.id == step.id) {
                            currentChain = &chain;
                            DBG("    -> FOUND chain");
                            break;
                        }
                    }
                } else {
                    DBG("  Chain step but no currentRack!");
                }
                break;
            }
            case ChainStepType::Device:
                // Devices don't contain racks, skip
                break;
        }
    }

    DBG("  -> returning rack: " << (currentRack ? "found" : "NULL"));
    return currentRack;
}

const RackInfo* TrackManager::getRackByPath(const ChainNodePath& rackPath) const {
    // const version - delegates to non-const via const_cast (safe since we return const*)
    return const_cast<TrackManager*>(this)->getRackByPath(rackPath);
}

ChainId TrackManager::addChainToRack(const ChainNodePath& rackPath, const juce::String& name) {
    DBG("addChainToRack called with path:");
    if (auto* rack = getRackByPath(rackPath)) {
        ChainInfo chain;
        chain.id = nextChainId_++;
        chain.name = name.isEmpty()
                         ? ("Chain " + juce::String(static_cast<int>(rack->chains.size()) + 1))
                         : name;
        rack->chains.push_back(chain);
        notifyTrackDevicesChanged(rackPath.trackId);
        DBG("Added chain: " << chain.name << " (id=" << chain.id << ") to rack via path");
        return chain.id;
    }
    DBG("addChainToRack FAILED - rack not found via path!");
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
    // devicePath ends with a Device step
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
            DBG("Removed device via path: " << magda::getDevice(*it).name << " (id=" << deviceId
                                            << ")");
            elements.erase(it);
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

DeviceInfo* TrackManager::getDeviceInChainByPath(const ChainNodePath& devicePath) {
    // devicePath ends with a Device step
    if (devicePath.steps.empty())
        return nullptr;

    DeviceId deviceId = INVALID_DEVICE_ID;
    if (devicePath.steps.back().type == ChainStepType::Device) {
        deviceId = devicePath.steps.back().id;
    } else {
        return nullptr;
    }

    // Build chain path
    ChainNodePath chainPath;
    chainPath.trackId = devicePath.trackId;
    for (size_t i = 0; i < devicePath.steps.size() - 1; ++i) {
        chainPath.steps.push_back(devicePath.steps[i]);
    }

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

    // Add test devices and rack to track 1 for development
    if (!tracks_.empty()) {
        auto& track1 = tracks_[0];

        // Add a device
        DeviceInfo device;
        device.id = nextDeviceId_++;
        device.name = "Pro-Q 3";
        device.manufacturer = "FabFilter";
        device.format = PluginFormat::VST3;
        track1.chainElements.push_back(makeDeviceElement(device));

        // Add a rack with one chain
        RackInfo rack;
        rack.id = nextRackId_++;
        rack.name = "FX Rack";

        ChainInfo chain;
        chain.id = nextChainId_++;
        chain.name = "Chain 1";

        // Add a device to the chain
        DeviceInfo chainDevice;
        chainDevice.id = nextDeviceId_++;
        chainDevice.name = "Pro-C 2";
        chainDevice.manufacturer = "FabFilter";
        chainDevice.format = PluginFormat::VST3;
        chain.elements.push_back(makeDeviceElement(chainDevice));

        rack.chains.push_back(std::move(chain));
        track1.chainElements.push_back(makeRackElement(std::move(rack)));
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

void TrackManager::notifyTrackDevicesChanged(TrackId trackId) {
    for (auto* listener : listeners_) {
        listener->trackDevicesChanged(trackId);
    }
}

juce::String TrackManager::generateTrackName() const {
    return juce::String(tracks_.size() + 1) + " Track";
}

}  // namespace magda
