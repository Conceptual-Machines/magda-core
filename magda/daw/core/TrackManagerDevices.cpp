#include <algorithm>
#include <map>

#include "../audio/AudioBridge.hpp"
#include "../audio/TracktionHelpers.hpp"
#include "../audio/plugin_manager/ExternalPluginStateUtil.hpp"
#include "../engine/AudioEngine.hpp"
#include "PluginPreferences.hpp"
#include "RackInfo.hpp"
#include "TrackManager.hpp"

namespace magda {

namespace {

struct PresetIdRemap {
    TrackId trackId = INVALID_TRACK_ID;
    std::map<DeviceId, DeviceId> devices;
    std::map<RackId, RackId> racks;
    std::map<ChainId, ChainId> chains;
};

bool targetPointsAtDevice(const ControlTarget& target, DeviceId deviceId) {
    return deviceId != INVALID_DEVICE_ID && target.devicePath.getDeviceId() == deviceId;
}

void retargetPresetLink(ControlTarget& target, DeviceId presetDeviceId,
                        const ChainNodePath& liveDevicePath) {
    if (targetPointsAtDevice(target, presetDeviceId))
        target.devicePath = liveDevicePath;
}

void retargetPresetLinks(MacroArray& macros, ModArray& mods, DeviceId presetDeviceId,
                         const ChainNodePath& liveDevicePath) {
    for (auto& macro : macros) {
        for (auto& link : macro.links)
            retargetPresetLink(link.target, presetDeviceId, liveDevicePath);
    }

    for (auto& mod : mods) {
        for (auto& link : mod.links)
            retargetPresetLink(link.target, presetDeviceId, liveDevicePath);
    }
}

template <typename Id> bool remapId(std::map<Id, Id> const& ids, int& value) {
    auto it = ids.find(value);
    if (it == ids.end())
        return false;
    value = it->second;
    return true;
}

void remapPresetPath(ChainNodePath& path, const PresetIdRemap& remap) {
    bool touched = false;

    if (path.isTrackLevel) {
        path.trackId = remap.trackId;
        return;
    }

    if (path.topLevelDeviceId != INVALID_DEVICE_ID)
        touched = remapId(remap.devices, path.topLevelDeviceId) || touched;

    for (auto& step : path.steps) {
        switch (step.type) {
            case ChainStepType::Rack:
                touched = remapId(remap.racks, step.id) || touched;
                break;
            case ChainStepType::Chain:
                touched = remapId(remap.chains, step.id) || touched;
                break;
            case ChainStepType::Device:
                touched = remapId(remap.devices, step.id) || touched;
                break;
            case ChainStepType::Segment:
                break;  // Segment steps carry no remappable ID
        }
    }

    if (touched)
        path.trackId = remap.trackId;
}

void remapPresetTarget(ControlTarget& target, const PresetIdRemap& remap) {
    remapPresetPath(target.devicePath, remap);
}

void remapPresetLinks(MacroArray& macros, ModArray& mods, const PresetIdRemap& remap) {
    for (auto& macro : macros) {
        for (auto& link : macro.links)
            remapPresetTarget(link.target, remap);
    }

    for (auto& mod : mods) {
        for (auto& link : mod.links)
            remapPresetTarget(link.target, remap);
    }
}

juce::String stripPresetRuntimePluginState(const juce::String& pluginState) {
    if (pluginState.isEmpty())
        return pluginState;

    auto xml = juce::parseXML(pluginState);
    if (!xml)
        return pluginState;

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
        return pluginState;

    stripTracktionIdsRecursive(state);
    stripModifierAssignmentsRecursive(state);

    if (auto strippedXml = state.createXml())
        return strippedXml->toString();

    return pluginState;
}

void remapPresetLinksRecursive(std::vector<ChainElement>& elements, const PresetIdRemap& remap);

void remapRackPresetLinks(RackInfo& rack, const PresetIdRemap& remap) {
    remapPresetLinks(rack.macros, rack.mods, remap);
    for (auto& chain : rack.chains)
        remapPresetLinksRecursive(chain.elements, remap);
}

void remapPresetLinksRecursive(std::vector<ChainElement>& elements, const PresetIdRemap& remap) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            remapPresetLinks(device.macros, device.mods, remap);
            device.pluginState = stripPresetRuntimePluginState(device.pluginState);
        } else if (magda::isRack(element)) {
            remapRackPresetLinks(magda::getRack(element), remap);
        }
    }
}

void collectDeviceIdMatches(std::vector<ChainElement>& elements, DeviceId deviceId,
                            std::vector<DeviceInfo*>& matches) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            if (device.id == deviceId)
                matches.push_back(&device);
            continue;
        }

        if (magda::isRack(element)) {
            for (auto& chain : magda::getRack(element).chains)
                collectDeviceIdMatches(chain.elements, deviceId, matches);
        }
    }
}

void collectDeviceIdMatches(std::vector<PostFxChainElement>& elements, DeviceId deviceId,
                            std::vector<DeviceInfo*>& matches) {
    for (auto& element : elements)
        if (element.device.id == deviceId)
            matches.push_back(&element.device);
}

DeviceInfo* findUniqueBareDeviceIdMatch(TrackInfo& masterTrack, std::vector<TrackInfo>& tracks,
                                        DeviceId deviceId) {
    std::vector<DeviceInfo*> matches;
    collectDeviceIdMatches(masterTrack.chain.fxChainElements, deviceId, matches);
    collectDeviceIdMatches(masterTrack.chain.postFxChainElements, deviceId, matches);
    collectDeviceIdMatches(masterTrack.chain.mixerAnalysisElements, deviceId, matches);

    for (auto& track : tracks) {
        collectDeviceIdMatches(track.chain.fxChainElements, deviceId, matches);
        collectDeviceIdMatches(track.chain.postFxChainElements, deviceId, matches);
        collectDeviceIdMatches(track.chain.mixerAnalysisElements, deviceId, matches);
    }

    return matches.size() == 1 ? matches.front() : nullptr;
}

}  // namespace

// ============================================================================
// Device Management in Chains
// ============================================================================

DeviceId TrackManager::addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                                        const DeviceInfo& device) {
    if (auto* track = getTrack(trackId)) {
        if (track->type == TrackType::Group && device.isInstrument) {
            return INVALID_DEVICE_ID;
        }
    }
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        DeviceInfo newDevice = prepareNewDevice(device);
        chain->elements.push_back(makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(trackId);
        notifyDeviceAdded(ChainNodePath::chainDevice(trackId, rackId, chainId, newDevice.id),
                          newDevice);
        return newDevice.id;
    }
    return INVALID_DEVICE_ID;
}

DeviceId TrackManager::addDeviceToChainByPath(const ChainNodePath& chainPath,
                                              const DeviceInfo& device) {
    if (auto* track = getTrack(chainPath.trackId)) {
        if (track->type == TrackType::Group && device.isInstrument) {
            return INVALID_DEVICE_ID;
        }
    }
    // The chainPath should end with a Chain step

    if (chainPath.steps.empty()) {
        return INVALID_DEVICE_ID;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
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
            return INVALID_DEVICE_ID;
        }

        // Add the device
        DeviceInfo newDevice = prepareNewDevice(device);
        chain->elements.push_back(makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(chainPath.trackId);
        notifyDeviceAdded(chainPath.withDevice(newDevice.id), newDevice);
        return newDevice.id;
    }

    return INVALID_DEVICE_ID;
}

DeviceId TrackManager::addDeviceToChainByPath(const ChainNodePath& chainPath,
                                              const DeviceInfo& device, int insertIndex) {
    if (auto* track = getTrack(chainPath.trackId)) {
        if (track->type == TrackType::Group && device.isInstrument) {
            return INVALID_DEVICE_ID;
        }
    }
    // Similar to the non-indexed version but inserts at a specific position
    if (chainPath.steps.empty()) {
        return INVALID_DEVICE_ID;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
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
            return INVALID_DEVICE_ID;
        }

        // Add the device at the specified index
        DeviceInfo newDevice = prepareNewDevice(device);

        // Clamp insert index to valid range
        int maxIndex = static_cast<int>(chain->elements.size());
        insertIndex = std::clamp(insertIndex, 0, maxIndex);

        chain->elements.insert(chain->elements.begin() + insertIndex, makeDeviceElement(newDevice));
        notifyTrackDevicesChanged(chainPath.trackId);
        notifyDeviceAdded(chainPath.withDevice(newDevice.id), newDevice);
        return newDevice.id;
    }

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
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(
                ChainNodePath::chainDevice(trackId, rackId, chainId, deviceId));
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
        return;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
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

static std::vector<ChainElement>* getElementContainerForChainPath(TrackManager& tm,
                                                                  const ChainNodePath& chainPath) {
    if (chainPath.trackId == INVALID_TRACK_ID)
        return nullptr;

    if (chainPath.steps.empty()) {
        if (auto* track = tm.getTrack(chainPath.trackId))
            return &track->chain.fxChainElements;
        return nullptr;
    }

    if (auto* chain = getChainFromPath(tm, chainPath))
        return &chain->elements;

    return nullptr;
}

static ChainNodePath getParentChainPathForElementPath(const ChainNodePath& elementPath) {
    ChainNodePath parent;
    parent.trackId = elementPath.trackId;
    if (elementPath.topLevelDeviceId != INVALID_DEVICE_ID)
        return parent;

    parent.steps = elementPath.steps;
    if (!parent.steps.empty())
        parent.steps.pop_back();
    return parent;
}

using DevicePathMap = std::map<DeviceId, ChainNodePath>;

static void retargetMovedTarget(ControlTarget& target, const DevicePathMap& movedPaths) {
    const auto deviceId = target.devicePath.getDeviceId();
    if (deviceId == INVALID_DEVICE_ID)
        return;

    auto it = movedPaths.find(deviceId);
    if (it != movedPaths.end())
        target.devicePath = it->second;
}

static void retargetMovedLinks(MacroArray& macros, ModArray& mods,
                               const DevicePathMap& movedPaths) {
    for (auto& macro : macros) {
        for (auto& link : macro.links)
            retargetMovedTarget(link.target, movedPaths);
    }

    for (auto& mod : mods) {
        for (auto& link : mod.links)
            retargetMovedTarget(link.target, movedPaths);
    }
}

static void collectMovedDevicePaths(const ChainElement& element, const ChainNodePath& elementPath,
                                    DevicePathMap& movedPaths) {
    if (magda::isDevice(element)) {
        movedPaths[magda::getDevice(element).id] = elementPath;
        return;
    }

    const auto& rack = magda::getRack(element);
    auto rackPath = elementPath;
    for (const auto& chain : rack.chains) {
        auto chainPath = rackPath.withChain(chain.id);
        for (const auto& child : chain.elements) {
            if (magda::isDevice(child)) {
                collectMovedDevicePaths(child, chainPath.withDevice(magda::getDevice(child).id),
                                        movedPaths);
            } else if (magda::isRack(child)) {
                collectMovedDevicePaths(child, chainPath.withRack(magda::getRack(child).id),
                                        movedPaths);
            }
        }
    }
}

static void retargetLinksInElements(std::vector<ChainElement>& elements,
                                    const DevicePathMap& movedPaths) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            retargetMovedLinks(device.macros, device.mods, movedPaths);
        } else if (magda::isRack(element)) {
            auto& rack = magda::getRack(element);
            retargetMovedLinks(rack.macros, rack.mods, movedPaths);
            for (auto& chain : rack.chains)
                retargetLinksInElements(chain.elements, movedPaths);
        }
    }
}

static void retargetMovedLinksInTrack(TrackInfo& track, const DevicePathMap& movedPaths) {
    retargetMovedLinks(track.macros, track.mods, movedPaths);
    retargetLinksInElements(track.chain.fxChainElements, movedPaths);
}

static bool targetPointsAtMovedDevice(const ControlTarget& target,
                                      const DevicePathMap& movedPaths) {
    const auto deviceId = target.devicePath.getDeviceId();
    return deviceId != INVALID_DEVICE_ID && movedPaths.find(deviceId) != movedPaths.end();
}

static void removeMovedTargets(MacroArray& macros, ModArray& mods,
                               const DevicePathMap& movedPaths) {
    for (auto& macro : macros) {
        macro.links.erase(std::remove_if(macro.links.begin(), macro.links.end(),
                                         [&movedPaths](const MacroLink& link) {
                                             return targetPointsAtMovedDevice(link.target,
                                                                              movedPaths);
                                         }),
                          macro.links.end());
    }

    for (auto& mod : mods) {
        mod.links.erase(std::remove_if(mod.links.begin(), mod.links.end(),
                                       [&movedPaths](const ModLink& link) {
                                           return targetPointsAtMovedDevice(link.target,
                                                                            movedPaths);
                                       }),
                        mod.links.end());
    }
}

static void removeMovedTargetsInElements(std::vector<ChainElement>& elements,
                                         const DevicePathMap& movedPaths) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            removeMovedTargets(device.macros, device.mods, movedPaths);
        } else if (magda::isRack(element)) {
            auto& rack = magda::getRack(element);
            removeMovedTargets(rack.macros, rack.mods, movedPaths);
            for (auto& chain : rack.chains)
                removeMovedTargetsInElements(chain.elements, movedPaths);
        }
    }
}

static void removeMovedTargetsInTrack(TrackInfo& track, const DevicePathMap& movedPaths) {
    removeMovedTargets(track.macros, track.mods, movedPaths);
    removeMovedTargetsInElements(track.chain.fxChainElements, movedPaths);
}

static ChainNodePath getInsertedElementPath(const ChainNodePath& destinationChainPath,
                                            const ChainElement& element) {
    if (magda::isDevice(element)) {
        const auto deviceId = magda::getDevice(element).id;
        if (destinationChainPath.steps.empty())
            return ChainNodePath::topLevelDevice(destinationChainPath.trackId, deviceId);
        return destinationChainPath.withDevice(deviceId);
    }

    return destinationChainPath.withRack(magda::getRack(element).id);
}

static void reassignCopiedElementIds(TrackManager& tm, std::vector<ChainElement>& elements,
                                     TrackId targetTrackId) {
    PresetIdRemap remap;
    remap.trackId = targetTrackId;

    std::function<void(std::vector<ChainElement>&)> reassignIds;
    reassignIds = [&](std::vector<ChainElement>& items) {
        for (auto& element : items) {
            if (magda::isDevice(element)) {
                auto& device = magda::getDevice(element);
                const auto oldId = device.id;
                device.id = tm.allocateDeviceId();
                remap.devices[oldId] = device.id;
            } else if (magda::isRack(element)) {
                auto& rack = magda::getRack(element);
                const auto oldRackId = rack.id;
                rack.id = tm.allocateRackId();
                remap.racks[oldRackId] = rack.id;
                for (auto& chain : rack.chains) {
                    const auto oldChainId = chain.id;
                    chain.id = tm.allocateChainId();
                    remap.chains[oldChainId] = chain.id;
                    reassignIds(chain.elements);
                }
            }
        }
    };

    reassignIds(elements);
    remapPresetLinksRecursive(elements, remap);
}

static bool chainPathContainsRack(const ChainNodePath& destinationChainPath,
                                  const ChainNodePath& sourceRackPath) {
    if (sourceRackPath.steps.empty() || sourceRackPath.steps.back().type != ChainStepType::Rack ||
        destinationChainPath.steps.size() <= sourceRackPath.steps.size()) {
        return false;
    }

    return std::equal(sourceRackPath.steps.begin(), sourceRackPath.steps.end(),
                      destinationChainPath.steps.begin());
}

static bool elementContainsInstrument(const ChainElement& element) {
    if (magda::isDevice(element))
        return magda::getDevice(element).isInstrument;

    const auto& rack = magda::getRack(element);
    for (const auto& chain : rack.chains) {
        for (const auto& child : chain.elements) {
            if (elementContainsInstrument(child))
                return true;
        }
    }
    return false;
}

bool TrackManager::moveChainElement(const ChainNodePath& sourceElementPath,
                                    const ChainNodePath& destinationChainPath, int insertIndex) {
    if (sourceElementPath.trackId == INVALID_TRACK_ID ||
        destinationChainPath.trackId == INVALID_TRACK_ID) {
        return false;
    }

    ChainNodePath sourceChainPath;
    sourceChainPath.trackId = sourceElementPath.trackId;
    ChainStepType sourceType = ChainStepType::Device;
    int sourceId = INVALID_DEVICE_ID;

    if (sourceElementPath.topLevelDeviceId != INVALID_DEVICE_ID) {
        sourceType = ChainStepType::Device;
        sourceId = sourceElementPath.topLevelDeviceId;
    } else if (!sourceElementPath.steps.empty() &&
               (sourceElementPath.steps.back().type == ChainStepType::Device ||
                sourceElementPath.steps.back().type == ChainStepType::Rack)) {
        sourceType = sourceElementPath.steps.back().type;
        sourceId = sourceElementPath.steps.back().id;
        sourceChainPath.steps.assign(sourceElementPath.steps.begin(),
                                     sourceElementPath.steps.end() - 1);
    } else {
        return false;
    }

    if (sourceType == ChainStepType::Rack &&
        chainPathContainsRack(destinationChainPath, sourceElementPath)) {
        return false;
    }

    auto* sourceElements = getElementContainerForChainPath(*this, sourceChainPath);
    auto* destinationElements = getElementContainerForChainPath(*this, destinationChainPath);
    if (sourceElements == nullptr || destinationElements == nullptr) {
        return false;
    }

    auto sourceIt = std::find_if(
        sourceElements->begin(), sourceElements->end(),
        [sourceType, sourceId](const ChainElement& element) {
            if (sourceType == ChainStepType::Device)
                return magda::isDevice(element) && magda::getDevice(element).id == sourceId;
            return magda::isRack(element) && magda::getRack(element).id == sourceId;
        });
    if (sourceIt == sourceElements->end()) {
        return false;
    }

    if (auto* destinationTrack = getTrack(destinationChainPath.trackId)) {
        const bool destinationCannotHostInstruments = destinationTrack->type == TrackType::Aux ||
                                                      destinationTrack->type == TrackType::Group ||
                                                      destinationTrack->type == TrackType::Master;
        if (destinationCannotHostInstruments && elementContainsInstrument(*sourceIt)) {
            return false;
        }
    } else {
        return false;
    }

    const bool sameContainer = sourceElements == destinationElements;
    const int sourceIndex = static_cast<int>(std::distance(sourceElements->begin(), sourceIt));
    const int destinationSize = static_cast<int>(destinationElements->size());
    insertIndex = std::clamp(insertIndex, 0, destinationSize);

    if (sameContainer && (insertIndex == sourceIndex || insertIndex == sourceIndex + 1)) {
        return false;
    }

    if (audioEngine_) {
        if (auto* bridge = audioEngine_->getAudioBridge()) {
            bridge->getPluginManager().prepareForChainElementMove(sourceElementPath,
                                                                  destinationChainPath);
        }
    }

    ChainElement element = std::move(*sourceIt);
    sourceElements->erase(sourceElements->begin() + sourceIndex);

    if (sameContainer && insertIndex > sourceIndex)
        --insertIndex;

    insertIndex = std::clamp(insertIndex, 0, static_cast<int>(destinationElements->size()));
    destinationElements->insert(destinationElements->begin() + insertIndex, std::move(element));

    DevicePathMap movedPaths;
    auto& insertedElement = (*destinationElements)[static_cast<size_t>(insertIndex)];
    collectMovedDevicePaths(
        insertedElement, getInsertedElementPath(destinationChainPath, insertedElement), movedPaths);
    if (sameContainer || sourceElementPath.trackId == destinationChainPath.trackId) {
        if (auto* track = getTrack(destinationChainPath.trackId))
            retargetMovedLinksInTrack(*track, movedPaths);
    } else {
        retargetLinksInElements(*destinationElements, movedPaths);
        if (auto* sourceTrack = getTrack(sourceElementPath.trackId))
            removeMovedTargetsInTrack(*sourceTrack, movedPaths);
    }

    notifyTrackDevicesChanged(sourceElementPath.trackId);
    if (destinationChainPath.trackId != sourceElementPath.trackId)
        notifyTrackDevicesChanged(destinationChainPath.trackId);

    return true;
}

std::vector<ChainElement> TrackManager::copyChainElements(
    const std::vector<ChainNodePath>& paths) const {
    std::vector<ChainElement> copied;
    std::vector<ChainNodePath> uniquePaths;
    for (const auto& path : paths) {
        if (path.isValid() &&
            std::find(uniquePaths.begin(), uniquePaths.end(), path) == uniquePaths.end())
            uniquePaths.push_back(path);
    }

    auto& mutableThis = const_cast<TrackManager&>(*this);
    std::stable_sort(
        uniquePaths.begin(), uniquePaths.end(), [&mutableThis](const auto& a, const auto& b) {
            const auto parentA = getParentChainPathForElementPath(a);
            const auto parentB = getParentChainPathForElementPath(b);
            if (parentA == parentB)
                return mutableThis.getChainElementIndex(a) < mutableThis.getChainElementIndex(b);
            if (parentA.trackId != parentB.trackId)
                return parentA.trackId < parentB.trackId;
            return parentA.toString() < parentB.toString();
        });

    for (const auto& path : uniquePaths) {
        const auto parentPath = getParentChainPathForElementPath(path);
        auto* elements = getElementContainerForChainPath(mutableThis, parentPath);
        if (elements == nullptr)
            continue;

        const auto type =
            path.topLevelDeviceId != INVALID_DEVICE_ID
                ? ChainStepType::Device
                : (!path.steps.empty() ? path.steps.back().type : ChainStepType::Device);
        const auto id = path.topLevelDeviceId != INVALID_DEVICE_ID
                            ? path.topLevelDeviceId
                            : (!path.steps.empty() ? path.steps.back().id : INVALID_DEVICE_ID);
        auto it = std::find_if(elements->begin(), elements->end(), [type, id](const auto& element) {
            if (type == ChainStepType::Device)
                return magda::isDevice(element) && magda::getDevice(element).id == id;
            return magda::isRack(element) && magda::getRack(element).id == id;
        });
        if (it != elements->end())
            copied.push_back(deepCopyElement(*it));
    }

    return copied;
}

bool TrackManager::insertChainElementsByPath(const ChainNodePath& destinationChainPath,
                                             std::vector<ChainElement> elements, int insertIndex,
                                             bool reassignIds) {
    auto* destinationElements = getElementContainerForChainPath(*this, destinationChainPath);
    if (destinationElements == nullptr || elements.empty())
        return false;

    if (reassignIds)
        reassignCopiedElementIds(*this, elements, destinationChainPath.trackId);

    insertIndex = std::clamp(insertIndex, 0, static_cast<int>(destinationElements->size()));
    destinationElements->insert(destinationElements->begin() + insertIndex,
                                std::make_move_iterator(elements.begin()),
                                std::make_move_iterator(elements.end()));
    notifyTrackDevicesChanged(destinationChainPath.trackId);
    return true;
}

RackId TrackManager::wrapChainElementsInRack(const std::vector<ChainNodePath>& paths,
                                             const juce::String& rackName) {
    if (paths.empty())
        return INVALID_RACK_ID;

    const auto sourceChainPath = getParentChainPathForElementPath(paths.front());
    auto* sourceElements = getElementContainerForChainPath(*this, sourceChainPath);
    if (sourceElements == nullptr)
        return INVALID_RACK_ID;

    std::vector<std::pair<int, ChainNodePath>> orderedPaths;
    for (const auto& path : paths) {
        if (!path.isValid() || getParentChainPathForElementPath(path) != sourceChainPath)
            return INVALID_RACK_ID;

        const int index = getChainElementIndex(path);
        if (index >= 0)
            orderedPaths.push_back({index, path});
    }

    if (orderedPaths.empty())
        return INVALID_RACK_ID;

    std::stable_sort(orderedPaths.begin(), orderedPaths.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    RackInfo rack;
    rack.id = allocateRackId();
    rack.name = rackName.isEmpty() ? "Rack" : rackName;
    ChainInfo chain;
    chain.id = allocateChainId();
    chain.name = "Chain 1";

    if (audioEngine_) {
        if (auto* bridge = audioEngine_->getAudioBridge()) {
            ChainNodePath destinationPath = sourceChainPath;
            destinationPath.steps.push_back({ChainStepType::Rack, rack.id});
            destinationPath.steps.push_back({ChainStepType::Chain, chain.id});
            for (const auto& [_, path] : orderedPaths)
                bridge->getPluginManager().prepareForChainElementMove(path, destinationPath);
        }
    }

    for (const auto& [index, _] : orderedPaths)
        chain.elements.push_back(std::move((*sourceElements)[static_cast<size_t>(index)]));

    for (auto it = orderedPaths.rbegin(); it != orderedPaths.rend(); ++it)
        sourceElements->erase(sourceElements->begin() + it->first);

    const int insertIndex =
        std::clamp(orderedPaths.front().first, 0, static_cast<int>(sourceElements->size()));
    rack.chains.push_back(std::move(chain));
    sourceElements->insert(sourceElements->begin() + insertIndex, makeRackElement(std::move(rack)));

    DevicePathMap movedPaths;
    auto& insertedRack = (*sourceElements)[static_cast<size_t>(insertIndex)];
    collectMovedDevicePaths(insertedRack, sourceChainPath.withRack(magda::getRack(insertedRack).id),
                            movedPaths);
    if (auto* track = getTrack(sourceChainPath.trackId))
        retargetMovedLinksInTrack(*track, movedPaths);

    notifyTrackDevicesChanged(sourceChainPath.trackId);
    return magda::getRack((*sourceElements)[static_cast<size_t>(insertIndex)]).id;
}

int TrackManager::getChainElementIndex(const ChainNodePath& elementPath) {
    ChainNodePath containerPath;
    containerPath.trackId = elementPath.trackId;

    ChainStepType sourceType = ChainStepType::Device;
    int sourceId = INVALID_DEVICE_ID;

    if (elementPath.topLevelDeviceId != INVALID_DEVICE_ID) {
        sourceType = ChainStepType::Device;
        sourceId = elementPath.topLevelDeviceId;
    } else if (!elementPath.steps.empty() &&
               (elementPath.steps.back().type == ChainStepType::Device ||
                elementPath.steps.back().type == ChainStepType::Rack)) {
        sourceType = elementPath.steps.back().type;
        sourceId = elementPath.steps.back().id;
        containerPath.steps.assign(elementPath.steps.begin(), elementPath.steps.end() - 1);
    } else {
        return -1;
    }

    auto* elements = getElementContainerForChainPath(*this, containerPath);
    if (elements == nullptr)
        return -1;

    for (int i = 0; i < static_cast<int>(elements->size()); ++i) {
        const auto& element = (*elements)[static_cast<size_t>(i)];
        if (sourceType == ChainStepType::Device) {
            if (isDevice(element) && magda::getDevice(element).id == sourceId)
                return i;
        } else if (isRack(element) && magda::getRack(element).id == sourceId) {
            return i;
        }
    }

    return -1;
}

void TrackManager::removeDeviceFromChainByPath(const ChainNodePath& devicePath) {
    auto removeFromFlatSection = [&](std::vector<PostFxChainElement>& elements) {
        DeviceId id = devicePath.getDeviceId();
        auto it = std::find_if(elements.begin(), elements.end(),
                               [id](const PostFxChainElement& e) { return e.device.id == id; });
        if (it != elements.end()) {
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(devicePath);
            elements.erase(it);
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    };

    // Post-fader FX list: flat, Segment(PostFx) > Device.
    if (devicePath.isPostFx()) {
        if (auto* track = getTrack(devicePath.trackId))
            removeFromFlatSection(track->chain.postFxChainElements);
        return;
    }
    // Mixer-analysis section: flat, Segment(MixerAnalysis) > Device.
    if (devicePath.isMixerAnalysis()) {
        if (auto* track = getTrack(devicePath.trackId))
            removeFromFlatSection(track->chain.mixerAnalysisElements);
        return;
    }

    // Handle top-level device (uses topLevelDeviceId field)
    if (devicePath.topLevelDeviceId != INVALID_DEVICE_ID) {
        auto* track = getTrack(devicePath.trackId);
        if (!track)
            return;
        auto& elements = track->chain.fxChainElements;
        auto it =
            std::find_if(elements.begin(), elements.end(), [&devicePath](const ChainElement& e) {
                return magda::isDevice(e) && magda::getDevice(e).id == devicePath.topLevelDeviceId;
            });
        if (it != elements.end()) {
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(devicePath);
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
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(devicePath);
            elements.erase(it);
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

DeviceInfo* TrackManager::getDeviceInChainByPath(const ChainNodePath& devicePath) {
    auto lookupInFlatSection = [&](std::vector<PostFxChainElement>& elements) -> DeviceInfo* {
        DeviceId id = devicePath.getDeviceId();
        for (auto& e : elements) {
            if (e.device.id == id)
                return &e.device;
        }
        return nullptr;
    };
    // Post-fader FX list: flat, so the path is Segment(PostFx) > Device.
    if (devicePath.isPostFx()) {
        if (auto* track = getTrack(devicePath.trackId))
            return lookupInFlatSection(track->chain.postFxChainElements);
        return nullptr;
    }
    // Mixer-analysis section: flat, Segment(MixerAnalysis) > Device.
    if (devicePath.isMixerAnalysis()) {
        if (auto* track = getTrack(devicePath.trackId))
            return lookupInFlatSection(track->chain.mixerAnalysisElements);
        return nullptr;
    }

    // Handle top-level device (legacy path format with topLevelDeviceId)
    if (devicePath.topLevelDeviceId != INVALID_DEVICE_ID) {
        auto* track = getTrack(devicePath.trackId);
        if (!track)
            return nullptr;
        for (auto& element : track->chain.fxChainElements) {
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
        for (auto& element : track->chain.fxChainElements) {
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

const DeviceInfo* TrackManager::getDeviceInChainByPath(const ChainNodePath& devicePath) const {
    // Standard const-overload idiom: the mutable version performs no mutation
    // (it's a pure lookup), so const_cast'ing `this` here is safe and avoids
    // duplicating the 50-line traversal.
    return const_cast<TrackManager*>(this)->getDeviceInChainByPath(devicePath);
}

void TrackManager::setDeviceInChainBypassedByPath(const ChainNodePath& devicePath, bool bypassed) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->bypassed = bypassed;
        notifyDevicePropertyChanged(devicePath);
    }
}

// ============================================================================
// Device Parameters
// ============================================================================

void TrackManager::setDeviceGainDb(const ChainNodePath& devicePath, float gainDb) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->gainDb = gainDb;
        // Convert dB to linear: 10^(dB/20)
        device->gainValue = std::pow(10.0f, gainDb / 20.0f);
        notifyDevicePropertyChanged(devicePath);
    }
}

void TrackManager::setDeviceLevel(const ChainNodePath& devicePath, float level) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->gainValue = level;
        // Convert linear to dB: 20 * log10(level)
        device->gainDb = (level > 0.0f) ? 20.0f * std::log10(level) : -100.0f;
        notifyDevicePropertyChanged(devicePath);
    }
}

namespace {
// Mirror the device's current kit to the user-global default in
// PluginPreferences. Called after every kit mutation so the most recent edit
// to any instance becomes the default applied to new instances of the same
// plugin. Empty identifier (no plugin loaded yet) is a no-op.
void mirrorKitToPreferences(const DeviceInfo& device) {
    const auto identifier = PluginPreferences::identifierForDevice(device);
    if (identifier.isEmpty())
        return;
    PluginPreferences::getInstance().setDefaultKitRows(identifier, device.kitRows);
}

DeviceInfo* findPrimaryInstrumentIn(std::vector<ChainElement>& elements) {
    for (auto& element : elements) {
        if (isDevice(element)) {
            auto& dev = getDevice(element);
            if (dev.isInstrument)
                return &dev;
        } else if (isRack(element)) {
            auto& rack = getRack(element);
            for (auto& chain : rack.chains) {
                if (auto* dev = findPrimaryInstrumentIn(chain.elements))
                    return dev;
            }
        }
    }
    return nullptr;
}

// Recursive id-based device lookup. TrackManager::getDevice only walks the
// top-level chainElements — rack-contained devices return nullptr from it,
// which silently broke kit-row mutations on instruments wrapped in racks
// (the common case: every instrument added via the plugin browser is
// auto-wrapped in an InstrumentRack).
DeviceInfo* findDeviceByIdIn(std::vector<ChainElement>& elements, DeviceId deviceId) {
    for (auto& element : elements) {
        if (isDevice(element)) {
            auto& dev = getDevice(element);
            if (dev.id == deviceId)
                return &dev;
        } else if (isRack(element)) {
            auto& rack = getRack(element);
            for (auto& chain : rack.chains) {
                if (auto* dev = findDeviceByIdIn(chain.elements, deviceId))
                    return dev;
            }
        }
    }
    return nullptr;
}

DeviceInfo* findDeviceOnTrack(TrackInfo* track, DeviceId deviceId) {
    return track != nullptr ? findDeviceByIdIn(track->chain.fxChainElements, deviceId) : nullptr;
}
}  // namespace

const DeviceInfo* TrackManager::getPrimaryInstrument(TrackId trackId) const {
    return const_cast<TrackManager*>(this)->getPrimaryInstrument(trackId);
}

DeviceInfo* TrackManager::getPrimaryInstrument(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (track == nullptr)
        return nullptr;
    return findPrimaryInstrumentIn(track->chain.fxChainElements);
}

namespace {
// Find/insert/update a row in `rows` by note number. Returns true if the row
// vector changed (so caller can skip the notify on no-op edits).
bool updateKitRow(std::vector<KitRow>& rows, int noteNumber, const juce::String* label,
                  const juce::String* role) {
    auto it = std::find_if(rows.begin(), rows.end(),
                           [noteNumber](const KitRow& r) { return r.noteNumber == noteNumber; });
    if (it == rows.end()) {
        const bool labelEmpty = (label == nullptr || label->isEmpty());
        const bool roleEmpty = (role == nullptr || role->isEmpty());
        if (labelEmpty && roleEmpty)
            return false;
        KitRow r;
        r.noteNumber = noteNumber;
        if (label != nullptr)
            r.label = *label;
        if (role != nullptr)
            r.role = *role;
        rows.push_back(std::move(r));
        return true;
    }
    bool changed = false;
    if (label != nullptr && it->label != *label) {
        it->label = *label;
        changed = true;
    }
    if (role != nullptr && it->role != *role) {
        it->role = *role;
        changed = true;
    }
    if (it->label.isEmpty() && it->role.isEmpty()) {
        rows.erase(it);
        return true;
    }
    return changed;
}
}  // namespace

void TrackManager::setDeviceKitRowLabel(TrackId trackId, DeviceId deviceId, int noteNumber,
                                        const juce::String& label) {
    auto* device = findDeviceOnTrack(getTrack(trackId), deviceId);
    if (device == nullptr || noteNumber < 0 || noteNumber > 127)
        return;
    if (updateKitRow(device->kitRows, noteNumber, &label, nullptr)) {
        notifyDevicePropertyChanged(ChainNodePath::topLevelDevice(trackId, deviceId));
        mirrorKitToPreferences(*device);
    }
}

void TrackManager::setDeviceKitRowRole(TrackId trackId, DeviceId deviceId, int noteNumber,
                                       const juce::String& role) {
    auto* device = findDeviceOnTrack(getTrack(trackId), deviceId);
    if (device == nullptr || noteNumber < 0 || noteNumber > 127)
        return;
    if (updateKitRow(device->kitRows, noteNumber, nullptr, &role)) {
        notifyDevicePropertyChanged(ChainNodePath::topLevelDevice(trackId, deviceId));
        mirrorKitToPreferences(*device);
    }
}

void TrackManager::clearDeviceKitRow(TrackId trackId, DeviceId deviceId, int noteNumber) {
    auto* device = findDeviceOnTrack(getTrack(trackId), deviceId);
    if (device == nullptr)
        return;
    auto& rows = device->kitRows;
    auto it = std::find_if(rows.begin(), rows.end(),
                           [noteNumber](const KitRow& r) { return r.noteNumber == noteNumber; });
    if (it == rows.end())
        return;
    rows.erase(it);
    notifyDevicePropertyChanged(ChainNodePath::topLevelDevice(trackId, deviceId));
    mirrorKitToPreferences(*device);
}

void TrackManager::setDeviceKitRows(TrackId trackId, DeviceId deviceId,
                                    const std::vector<KitRow>& rows) {
    auto* device = findDeviceOnTrack(getTrack(trackId), deviceId);
    if (device == nullptr)
        return;
    device->kitRows = rows;
    notifyDevicePropertyChanged(ChainNodePath::topLevelDevice(trackId, deviceId));
    mirrorKitToPreferences(*device);
}

ChainNodePath TrackManager::findDevicePath(DeviceId deviceId) const {
    // Search all tracks for a device by ID and return its full path
    for (const auto& track : tracks_) {
        for (const auto& element : track.chain.fxChainElements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId)
                return ChainNodePath::topLevelDevice(track.id, deviceId);
            if (magda::isRack(element)) {
                const auto& rack = magda::getRack(element);
                for (const auto& chain : rack.chains) {
                    for (const auto& chainElement : chain.elements) {
                        if (magda::isDevice(chainElement) &&
                            magda::getDevice(chainElement).id == deviceId)
                            return ChainNodePath::chainDevice(track.id, rack.id, chain.id,
                                                              deviceId);
                    }
                }
            }
        }
    }
    // Also check master track
    for (const auto& element : masterTrack_.chain.fxChainElements) {
        if (magda::isDevice(element) && magda::getDevice(element).id == deviceId)
            return ChainNodePath::topLevelDevice(MASTER_TRACK_ID, deviceId);
    }
    return {};  // Not found — returns invalid path
}

void TrackManager::updateDeviceParameters(DeviceId deviceId,
                                          const std::vector<ParameterInfo>& params) {
    // Check master track first
    for (auto& element : masterTrack_.chain.fxChainElements) {
        if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
            magda::getDevice(element).parameters = params;
            return;
        }
    }

    // Search all tracks for the device and update its parameters
    for (auto& track : tracks_) {
        for (auto& element : track.chain.fxChainElements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                magda::getDevice(element).parameters = params;
                return;
            }
            if (magda::isRack(element)) {
                for (auto& chain : magda::getRack(element).chains) {
                    for (auto& chainElement : chain.elements) {
                        if (magda::isDevice(chainElement) &&
                            magda::getDevice(chainElement).id == deviceId) {
                            magda::getDevice(chainElement).parameters = params;
                            return;
                        }
                    }
                }
            }
        }
    }
}

void TrackManager::updateDeviceParametersByPath(const ChainNodePath& devicePath,
                                                const std::vector<ParameterInfo>& params) {
    if (auto* device = getDeviceInChainByPath(devicePath))
        device->parameters = params;
}

void TrackManager::setDeviceVisibleParameters(const ChainNodePath& devicePath,
                                              const std::vector<int>& visibleParams) {
    if (auto* device = getDeviceInChainByPath(devicePath))
        device->visibleParameters = visibleParams;
}

void TrackManager::setDeviceVisibleParameters(DeviceId deviceId,
                                              const std::vector<int>& visibleParams) {
    if (auto* device = findUniqueBareDeviceIdMatch(masterTrack_, tracks_, deviceId))
        device->visibleParameters = visibleParams;
    else
        DBG("Ignoring visible-parameter update for ambiguous device id " << deviceId);
}

void TrackManager::setDeviceMiniMixerParameters(const ChainNodePath& devicePath,
                                                const std::vector<int>& miniParams) {
    if (auto* device = getDeviceInChainByPath(devicePath))
        device->miniMixerParameters = miniParams;
}

void TrackManager::setDeviceMiniMixerParameters(DeviceId deviceId,
                                                const std::vector<int>& miniParams) {
    if (auto* device = findUniqueBareDeviceIdMatch(masterTrack_, tracks_, deviceId))
        device->miniMixerParameters = miniParams;
    else
        DBG("Ignoring mini-mixer-parameter update for ambiguous device id " << deviceId);
}

void TrackManager::setDeviceParameterValue(const ChainNodePath& devicePath, int paramIndex,
                                           ParameterModelValue value) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (auto* stored = device->findParameterByIndex(paramIndex)) {
            stored->currentValue = value.value;
            // Use granular notification - only sync this one parameter, not all 543
            notifyDeviceParameterChanged(devicePath, paramIndex, value.value);
        }
    }
}

bool TrackManager::applyDevicePreset(const ChainNodePath& devicePath,
                                     const DeviceInfo& presetDevice) {
    auto* live = getDeviceInChainByPath(devicePath);
    if (!live) {
        return false;
    }

    // Don't load a preset captured from a different plugin onto this slot.
    if (live->pluginId != presetDevice.pluginId) {
        return false;
    }

    auto presetMacros = presetDevice.macros;
    auto presetMods = presetDevice.mods;
    retargetPresetLinks(presetMacros, presetMods, presetDevice.id, devicePath);

    // Copy state-y fields; preserve identity (id, name, format, fileOrIdentifier,
    // capabilities, sidechain wiring, current track placement).
    live->parameters = presetDevice.parameters;
    live->macros = std::move(presetMacros);
    live->mods = std::move(presetMods);
    live->gainDb = presetDevice.gainDb;
    live->gainValue = std::pow(10.0f, presetDevice.gainDb / 20.0f);
    live->pluginState = stripPresetRuntimePluginState(presetDevice.pluginState);

    // Push the new pluginState into the running plugin.
    if (audioEngine_) {
        if (auto* bridge = audioEngine_->getAudioBridge()) {
            if (auto plugin = bridge->getPlugin(devicePath)) {
                if (dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin.get()) != nullptr) {
                    // Only when the preset carries a native state chunk: it is the
                    // authoritative source for the entire voice. Re-assert it +
                    // refresh TE's param cache, then re-derive live->parameters from
                    // the plugin, so the preset's (possibly stale) saved parameter
                    // array can't clobber the restored voice when the
                    // devicePropertyChanged notification below drives
                    // syncFromDeviceInfo. (Same hazard + helper as loadDeviceAsPlugin.)
                    //
                    // For a parameter-only preset (no chunk -- e.g. a plugin that
                    // returns no state, or a legacy preset) we must NOT repopulate:
                    // that would overwrite the preset's saved parameter values with
                    // the plugin's current ones. Leave live->parameters as captured
                    // and let the notification below apply them via syncFromDeviceInfo.
                    if (live->pluginState.isNotEmpty()) {
                        applyExternalPluginChunk(plugin.get(), live->pluginState);
                        if (auto* proc = bridge->getDeviceProcessor(devicePath))
                            proc->populateParameters(*live);
                    }
                } else if (auto xml = juce::parseXML(live->pluginState)) {
                    auto savedState = juce::ValueTree::fromXml(*xml);
                    if (savedState.isValid())
                        plugin->restorePluginStateFromValueTree(savedState);
                }
            }
        }
    }

    // Notify listeners — devicePropertyChanged covers gain/macros/mods refresh
    // via the AudioBridge sync path, then push each parameter individually so
    // the UI's ParamGrid pickup matches what the preset captured. Address each by
    // its real `paramIndex` (the TE automatable index), NOT the vector ordinal —
    // ParameterInfo is not 1:1 with the TE parameter list (wrapper dry/wet live in
    // wrapperParameters), and both the engine write (setParameterByIndex) and the
    // UI lookup (findParameterByIndex) interpret the notified index as paramIndex.
    notifyDevicePropertyChanged(devicePath);
    for (const auto& p : live->parameters) {
        notifyDeviceParameterChanged(devicePath, p.paramIndex, p.currentValue);
    }
    return true;
}

bool TrackManager::applyRackPreset(const ChainNodePath& rackPath, const RackInfo& presetRack) {
    auto* live = getRackByPath(rackPath);
    if (!live) {
        return false;
    }

    // Replace state, but preserve the rack's runtime identity (its id and
    // its slot in the parent track / chain).
    const auto preservedId = live->id;
    *live = presetRack;
    live->id = preservedId;

    PresetIdRemap remap;
    remap.trackId = rackPath.trackId;
    remap.racks[presetRack.id] = preservedId;

    // Reassign every chain / device / nested-rack id under this rack so the
    // freshly-loaded subtree doesn't collide with other live elements'
    // runtime IDs. Macros and mods are indexed within their parent and don't
    // need reassignment. Mirrors the recursive walk in duplicateTrack.
    std::function<void(std::vector<ChainElement>&)> reassignIds;
    reassignIds = [&](std::vector<ChainElement>& elements) {
        for (auto& element : elements) {
            if (magda::isDevice(element)) {
                auto& device = magda::getDevice(element);
                const auto oldId = device.id;
                device.id = nextFxDeviceId_++;
                remap.devices[oldId] = device.id;
            } else if (magda::isRack(element)) {
                auto& nested = magda::getRack(element);
                const auto oldRackId = nested.id;
                nested.id = nextRackId_++;
                remap.racks[oldRackId] = nested.id;
                for (auto& chain : nested.chains) {
                    const auto oldChainId = chain.id;
                    chain.id = nextChainId_++;
                    remap.chains[oldChainId] = chain.id;
                    reassignIds(chain.elements);
                }
            }
        }
    };
    for (auto& chain : live->chains) {
        const auto oldChainId = chain.id;
        chain.id = nextChainId_++;
        remap.chains[oldChainId] = chain.id;
        reassignIds(chain.elements);
    }
    remapRackPresetLinks(*live, remap);

    // Trigger a full track resync — AudioBridge::trackDevicesChanged tears
    // down and rebuilds the rack via RackSyncManager from the updated model.
    notifyTrackDevicesChanged(rackPath.trackId);
    return true;
}

bool TrackManager::applyChainPreset(TrackId trackId, std::vector<ChainElement> presetElements) {
    auto* track = getTrack(trackId);
    if (!track) {
        return false;
    }

    // Reassign every chain / device / nested-rack id in the preset so they
    // don't collide with other live elements' runtime IDs. Same recursive
    // walk applyRackPreset uses.
    PresetIdRemap remap;
    remap.trackId = trackId;
    std::function<void(std::vector<ChainElement>&)> reassignIds;
    reassignIds = [&](std::vector<ChainElement>& elements) {
        for (auto& element : elements) {
            if (magda::isDevice(element)) {
                auto& device = magda::getDevice(element);
                const auto oldId = device.id;
                device.id = nextFxDeviceId_++;
                remap.devices[oldId] = device.id;
            } else if (magda::isRack(element)) {
                auto& nested = magda::getRack(element);
                const auto oldRackId = nested.id;
                nested.id = nextRackId_++;
                remap.racks[oldRackId] = nested.id;
                for (auto& chain : nested.chains) {
                    const auto oldChainId = chain.id;
                    chain.id = nextChainId_++;
                    remap.chains[oldChainId] = chain.id;
                    reassignIds(chain.elements);
                }
            }
        }
    };
    reassignIds(presetElements);
    remapPresetLinksRecursive(presetElements, remap);

    track->chain.fxChainElements = std::move(presetElements);

    notifyTrackDevicesChanged(trackId);
    return true;
}

void TrackManager::setDeviceParameterValueFromPlugin(const ChainNodePath& devicePath,
                                                     int paramIndex, float value) {
    // This method is called when the plugin's native UI changes a parameter.
    // It updates the DeviceInfo but does NOT call notifyDevicePropertyChanged()
    // to avoid triggering AudioBridge sync (which would cause a feedback loop).
    //
    // Instead, we notify UI listeners directly about the parameter change.

    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (auto* stored = device->findParameterByIndex(paramIndex)) {
            stored->currentValue = value;

            // Notify listeners about parameter change (for UI updates)
            notifyDeviceParameterChanged(devicePath, paramIndex, value);
        }
    }
}

double TrackManager::getDeviceLatencySeconds(const ChainNodePath& devicePath) {
    auto* device = getDeviceInChainByPath(devicePath);
    if (!device || !audioEngine_)
        return 0.0;

    if (auto* bridge = audioEngine_->getAudioBridge()) {
        if (auto* processor = bridge->getPluginManager().getDeviceProcessor(devicePath)) {
            if (auto plugin = processor->getPlugin())
                return plugin->getLatencySeconds();
        }
    }
    return 0.0;
}

double TrackManager::getTrackLatencySeconds(TrackId trackId) {
    if (!audioEngine_)
        return 0.0;

    auto* bridge = audioEngine_->getAudioBridge();
    if (!bridge)
        return 0.0;

    auto* track = getTrack(trackId);
    if (!track)
        return 0.0;

    auto& pm = bridge->getPluginManager();
    double total = 0.0;

    // Helper to get latency for a single device
    auto getDeviceLatency = [&](const ChainNodePath& devicePath) -> double {
        if (auto* proc = pm.getDeviceProcessor(devicePath)) {
            if (auto plugin = proc->getPlugin())
                return plugin->getLatencySeconds();
        }
        return 0.0;
    };

    // Sum latency across top-level chain elements
    for (const auto& element : track->chain.fxChainElements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            total += getDeviceLatency(ChainNodePath::topLevelDevice(trackId, device.id));
        } else if (magda::isRack(element)) {
            // For racks: each chain is parallel, so take the max chain latency
            const auto& rack = magda::getRack(element);
            double maxChainLatency = 0.0;
            for (const auto& chain : rack.chains) {
                double chainLatency = 0.0;
                for (const auto& chainElem : chain.elements) {
                    if (magda::isDevice(chainElem)) {
                        const auto& device = magda::getDevice(chainElem);
                        chainLatency += getDeviceLatency(
                            ChainNodePath::chainDevice(trackId, rack.id, chain.id, device.id));
                    }
                }
                maxChainLatency = std::max(maxChainLatency, chainLatency);
            }
            total += maxChainLatency;
        }
    }

    return total;
}

// ============================================================================
// Wrap Device in Rack
// ============================================================================

RackId TrackManager::wrapDeviceInRack(TrackId trackId, DeviceId deviceId,
                                      const juce::String& rackName) {
    auto* track = getTrack(trackId);
    if (!track)
        return INVALID_RACK_ID;

    auto& elements = track->chain.fxChainElements;

    // Find the device in the top-level chain
    auto it = std::find_if(elements.begin(), elements.end(), [deviceId](const ChainElement& e) {
        return magda::isDevice(e) && magda::getDevice(e).id == deviceId;
    });
    if (it == elements.end())
        return INVALID_RACK_ID;

    int insertIndex = static_cast<int>(std::distance(elements.begin(), it));

    // Extract the device
    DeviceInfo extractedDevice = magda::getDevice(*it);
    elements.erase(it);

    RackId newRackId =
        createRackWithDevice(elements, insertIndex, std::move(extractedDevice), rackName);

    notifyTrackDevicesChanged(trackId);
    return newRackId;
}

RackId TrackManager::wrapDeviceInRackByPath(const ChainNodePath& devicePath,
                                            const juce::String& rackName) {
    // Handle top-level device
    if (devicePath.topLevelDeviceId != INVALID_DEVICE_ID) {
        return wrapDeviceInRack(devicePath.trackId, devicePath.topLevelDeviceId, rackName);
    }

    // Handle nested device (path ends with Device step)
    if (devicePath.steps.empty() || devicePath.steps.back().type != ChainStepType::Device)
        return INVALID_RACK_ID;

    DeviceId deviceId = devicePath.steps.back().id;

    // Build chain path (everything except last Device step)
    ChainNodePath chainPath;
    chainPath.trackId = devicePath.trackId;
    for (size_t i = 0; i < devicePath.steps.size() - 1; ++i) {
        chainPath.steps.push_back(devicePath.steps[i]);
    }

    auto* chain = getChainFromPath(*this, chainPath);
    if (!chain)
        return INVALID_RACK_ID;

    auto& elements = chain->elements;

    // Find the device in the chain
    auto it = std::find_if(elements.begin(), elements.end(), [deviceId](const ChainElement& e) {
        return magda::isDevice(e) && magda::getDevice(e).id == deviceId;
    });
    if (it == elements.end())
        return INVALID_RACK_ID;

    int insertIndex = static_cast<int>(std::distance(elements.begin(), it));

    // Extract the device
    DeviceInfo extractedDevice = magda::getDevice(*it);
    elements.erase(it);

    RackId newRackId =
        createRackWithDevice(elements, insertIndex, std::move(extractedDevice), rackName);

    notifyTrackDevicesChanged(devicePath.trackId);
    return newRackId;
}

RackId TrackManager::createRackWithDevice(std::vector<ChainElement>& elements, int insertIndex,
                                          DeviceInfo device, const juce::String& rackName) {
    RackInfo rack;
    rack.id = nextRackId_++;
    rack.name = rackName.isEmpty() ? ("Rack " + juce::String(rack.id)) : rackName;

    ChainInfo defaultChain;
    defaultChain.id = nextChainId_++;
    defaultChain.name = "Chain 1";
    defaultChain.elements.push_back(makeDeviceElement(std::move(device)));
    rack.chains.push_back(std::move(defaultChain));

    RackId newRackId = rack.id;
    elements.insert(elements.begin() + insertIndex, makeRackElement(std::move(rack)));
    return newRackId;
}

// ============================================================================
// Nested Rack Management
// ============================================================================

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
        return newRackId;
    }
    return INVALID_RACK_ID;
}

RackId TrackManager::addRackToChainByPath(const ChainNodePath& chainPath,
                                          const juce::String& name) {
    // The chainPath should end with a Chain step - we add a rack to that chain
    for (size_t i = 0; i < chainPath.steps.size(); ++i) {
    }

    if (chainPath.steps.empty()) {
        return INVALID_RACK_ID;
    }

    // Extract chainId from the last step (should be Chain type)
    ChainId chainId = INVALID_CHAIN_ID;
    if (chainPath.steps.back().type == ChainStepType::Chain) {
        chainId = chainPath.steps.back().id;
    } else {
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
        return newRackId;
    }

    return INVALID_RACK_ID;
}

void TrackManager::removeRackFromChain(TrackId trackId, RackId parentRackId, ChainId chainId,
                                       RackId nestedRackId) {
    if (auto* chain = getChain(trackId, parentRackId, chainId)) {
        auto& elements = chain->elements;
        for (auto it = elements.begin(); it != elements.end(); ++it) {
            if (magda::isRack(*it)) {
                if (magda::getRack(*it).id == nestedRackId) {
                    elements.erase(it);
                    notifyTrackDevicesChanged(trackId);
                    return;
                }
            }
        }
    } else {
    }
}

void TrackManager::removeRackFromChainByPath(const ChainNodePath& rackPath) {
    // rackPath ends with a Rack step - we need to find the parent chain and remove this rack
    for (size_t i = 0; i < rackPath.steps.size(); ++i) {
    }

    if (rackPath.steps.size() == 1 && rackPath.steps.back().type == ChainStepType::Rack) {
        removeRackFromTrack(rackPath.trackId, rackPath.steps.back().id);
        return;
    }

    if (rackPath.steps.size() < 2) {
        return;
    }

    // Extract rackId from the last step (should be Rack type)
    RackId rackId = INVALID_RACK_ID;
    if (rackPath.steps.back().type == ChainStepType::Rack) {
        rackId = rackPath.steps.back().id;
    } else {
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
        auto& elements = chain->elements;
        for (auto it = elements.begin(); it != elements.end(); ++it) {
            if (magda::isRack(*it)) {
                if (magda::getRack(*it).id == rackId) {
                    elements.erase(it);
                    notifyTrackDevicesChanged(rackPath.trackId);
                    return;
                }
            }
        }
    } else {
    }
}

// ============================================================================
// Sidechain Configuration
// ============================================================================

void TrackManager::setSidechainSource(DeviceId targetDevice, TrackId sourceTrack,
                                      SidechainConfig::Type type) {
    auto updateElements = [&](auto&& self, std::vector<ChainElement>& elements) -> bool {
        for (auto& element : elements) {
            if (magda::isDevice(element)) {
                auto& device = magda::getDevice(element);
                if (device.id == targetDevice) {
                    device.sidechain.type = type;
                    device.sidechain.sourceTrackId = sourceTrack;
                    notifyDevicePropertyChanged(findDevicePath(targetDevice));
                    return true;
                }
            } else if (magda::isRack(element)) {
                auto& rack = magda::getRack(element);
                for (auto& chain : rack.chains)
                    if (self(self, chain.elements))
                        return true;
            }
        }

        return false;
    };

    // Search all tracks for the target device
    for (auto& track : tracks_) {
        if (updateElements(updateElements, track.chain.fxChainElements)) {
            // Re-sync the device's modifiers so an envelope follower picks up
            // the new source (setUsesExternalInput) and the sidechain cache is
            // rebuilt with it. Mirrors setRackSidechainSource.
            notifyDeviceModifiersChanged(track.id);
            return;
        }
    }
}

void TrackManager::clearSidechain(DeviceId targetDevice) {
    setSidechainSource(targetDevice, INVALID_TRACK_ID, SidechainConfig::Type::None);
}

void TrackManager::setRackSidechainSource(const ChainNodePath& rackPath, TrackId sourceTrack,
                                          SidechainConfig::Type type) {
    auto* rack = getRackByPath(rackPath);
    if (!rack)
        return;
    rack->sidechain.type = type;
    rack->sidechain.sourceTrackId = sourceTrack;
    notifyDeviceModifiersChanged(rackPath.trackId);
}

void TrackManager::clearRackSidechain(const ChainNodePath& rackPath) {
    setRackSidechainSource(rackPath, INVALID_TRACK_ID, SidechainConfig::Type::None);
}

}  // namespace magda
