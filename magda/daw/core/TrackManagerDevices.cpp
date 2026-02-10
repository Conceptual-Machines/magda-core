#include "../audio/AudioBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "RackInfo.hpp"
#include "TrackManager.hpp"

namespace magda {

// ============================================================================
// Device Management in Chains
// ============================================================================

DeviceId TrackManager::addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                                        const DeviceInfo& device) {
    if (auto* track = getTrack(trackId)) {
        if (track->type == TrackType::Group && device.isInstrument) {
            DBG("Cannot add instrument plugin to group track");
            return INVALID_DEVICE_ID;
        }
    }
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
    if (auto* track = getTrack(chainPath.trackId)) {
        if (track->type == TrackType::Group && device.isInstrument) {
            DBG("Cannot add instrument plugin to group track");
            return INVALID_DEVICE_ID;
        }
    }
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
    if (auto* track = getTrack(chainPath.trackId)) {
        if (track->type == TrackType::Group && device.isInstrument) {
            DBG("Cannot add instrument plugin to group track");
            return INVALID_DEVICE_ID;
        }
    }
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

// ============================================================================
// Device Parameters
// ============================================================================

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
        }
    }
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
// Sidechain Configuration
// ============================================================================

void TrackManager::setSidechainSource(DeviceId targetDevice, TrackId sourceTrack,
                                      SidechainConfig::Type type) {
    // Search all tracks for the target device
    for (auto& track : tracks_) {
        // Search top-level chain elements
        for (auto& element : track.chainElements) {
            if (magda::isDevice(element)) {
                auto& device = magda::getDevice(element);
                if (device.id == targetDevice) {
                    device.sidechain.type = type;
                    device.sidechain.sourceTrackId = sourceTrack;
                    notifyDevicePropertyChanged(targetDevice);
                    return;
                }
            } else if (magda::isRack(element)) {
                // Search inside racks
                auto& rack = magda::getRack(element);
                for (auto& chain : rack.chains) {
                    for (auto& chainElement : chain.elements) {
                        if (magda::isDevice(chainElement)) {
                            auto& device = magda::getDevice(chainElement);
                            if (device.id == targetDevice) {
                                device.sidechain.type = type;
                                device.sidechain.sourceTrackId = sourceTrack;
                                notifyDevicePropertyChanged(targetDevice);
                                return;
                            }
                        }
                    }
                }
            }
        }
    }
}

void TrackManager::clearSidechain(DeviceId targetDevice) {
    setSidechainSource(targetDevice, INVALID_TRACK_ID, SidechainConfig::Type::None);
}

}  // namespace magda
