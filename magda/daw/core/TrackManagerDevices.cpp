#include <algorithm>
#include <map>
#include <set>
#include <span>

#include "../audio/AudioBridge.hpp"
#include "../audio/TracktionHelpers.hpp"
#include "../audio/plugin_manager/ExternalPluginStateUtil.hpp"
#include "../audio/plugins/InternalPluginRegistry.hpp"
#include "../audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "ChainWalk.hpp"
#include "DeviceState.hpp"
#include "DrumGridPads.hpp"
#include "PluginCapabilities.hpp"
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

template <typename Id> bool remapId(std::map<Id, Id> const& ids, int& value) {
    auto it = ids.find(value);
    if (it == ids.end())
        return false;
    value = it->second;
    return true;
}

/// Whether anything in @p element has a Drum Grid pad routed to a bus.
///
/// The whole subtree, because a rack being moved carries its devices with it.
/// The single-device callers this replaces looked only at the element itself.
bool anyPadOnABusInSubtree(const ChainElement& element) {
    if (magda::isDevice(element)) {
        const auto& device = magda::getDevice(element);
        if (anyPadOnABus(device))
            return true;

        if (device.pads)
            for (const auto& pad : device.pads->chains)
                for (const auto& padElement : pad.elements)
                    if (anyPadOnABusInSubtree(padElement))
                        return true;
        return false;
    }

    if (!magda::isRack(element))
        return false;

    for (const auto& chain : magda::getRack(element).chains)
        for (const auto& nested : chain.elements)
            if (anyPadOnABusInSubtree(nested))
                return true;
    return false;
}

/// Whether anything strictly BELOW @p element has a Drum Grid pad routed to a bus.
///
/// The distinction the destination rule needs. Only the root of a subtree lands
/// where the caller says it lands; anything below it stays nested inside the
/// root wherever that is, so it can never be the top-level instrument whose
/// output instance carries a bus (#2221).
bool anyPadOnABusBelowRoot(const ChainElement& element) {
    if (magda::isDevice(element)) {
        const auto& device = magda::getDevice(element);
        if (device.pads)
            for (const auto& pad : device.pads->chains)
                for (const auto& padElement : pad.elements)
                    if (anyPadOnABusInSubtree(padElement))
                        return true;
        return false;
    }

    if (!magda::isRack(element))
        return false;

    for (const auto& chain : magda::getRack(element).chains)
        for (const auto& nested : chain.elements)
            if (anyPadOnABusInSubtree(nested))
                return true;
    return false;
}

/// Whether anything in @p element drives a multi-out child track of @p sourceTrackId.
///
/// The whole subtree, because a rack being moved carries its devices with it.
/// Ownership of a generated child track belongs to the device where it stands,
/// and the child track's link names the track it stands on, so carrying the
/// device somewhere else would leave the link naming a track that no longer
/// hosts it (#2220).
bool ownsMultiOutChildTracks(const TrackManager& tm, const ChainElement& element,
                             TrackId sourceTrackId) {
    if (magda::isDevice(element)) {
        const auto& device = magda::getDevice(element);
        if (device.multiOut.isMultiOut) {
            for (std::size_t pair = 0; pair < device.multiOut.outputPairs.size(); ++pair)
                if (tm.multiOutPairIsActive(sourceTrackId, device.id, static_cast<int>(pair)))
                    return true;
        }

        if (device.pads)
            for (const auto& pad : device.pads->chains)
                for (const auto& padElement : pad.elements)
                    if (ownsMultiOutChildTracks(tm, padElement, sourceTrackId))
                        return true;
        return false;
    }

    if (!magda::isRack(element))
        return false;

    for (const auto& chain : magda::getRack(element).chains)
        for (const auto& nested : chain.elements)
            if (ownsMultiOutChildTracks(tm, nested, sourceTrackId))
                return true;
    return false;
}

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
            case ChainStepType::PadRack:
                // A PadRack step carries the owning grid's DeviceId, so it moves
                // with the devices map like any other device id. Saying so in
                // the type is what removed the shape-matching pad remapper this
                // used to need (#2219).
                touched = remapId(remap.devices, step.id) || touched;
                break;
            case ChainStepType::PadChain:
                break;  // Pad chain ids are rack-local and survive the copy
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

// v2 device state is captured already stripped of engine ids and modifier
// assignments (see TracktionDeviceStateBridge.hpp), so only legacy engine XML
// needs cleaning here.
juce::String stripPresetRuntimePluginState(const juce::String& pluginState) {
    if (pluginState.isEmpty() || !device_state::looksLikeLegacyEngineState(pluginState))
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

/// Whether a re-keyed subtree's saved plugin state is a preset's or a project's.
///
/// A preset carries state captured against another project's engine, so the
/// runtime ids in it name plugin instances that are not this project's. A
/// subtree being re-keyed in place -- a Drum Grid getting its own DeviceId as it
/// is placed -- carries no such thing, and rewriting its state would be a change
/// nobody asked for.
enum class PresetState { Strip, Keep };

void remapPresetLinksRecursive(std::vector<ChainElement>& elements, const PresetIdRemap& remap,
                               PresetState state = PresetState::Strip);

void remapRackPresetLinks(RackInfo& rack, const PresetIdRemap& remap,
                          PresetState state = PresetState::Strip) {
    remapPresetLinks(rack.macros, rack.mods, remap);
    for (auto& chain : rack.chains)
        remapPresetLinksRecursive(chain.elements, remap, state);
}

void remapPresetLinksRecursive(std::vector<ChainElement>& elements, const PresetIdRemap& remap,
                               PresetState state) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            remapPresetLinks(device.macros, device.mods, remap);
            if (state == PresetState::Strip)
                device.pluginState = stripPresetRuntimePluginState(device.pluginState);

            // The pad rack, as a rack. A pad device is an ordinary DeviceInfo
            // and owns macros and mods like any other, so a copy's have to be
            // retargeted too (#2211) -- and so does the pad rack itself, which
            // is a RackInfo with macros and mods of its own that a pad path
            // resolves to and the modulation surfaces compile from. Descending
            // straight into its chains walked past those (#2261).
            if (device.pads)
                remapRackPresetLinks(*device.pads.get(), remap, state);
        } else if (magda::isRack(element)) {
            remapRackPresetLinks(magda::getRack(element), remap, state);
        }
    }
}

/// Follow a re-keyed Drum Grid's pads with everything that addressed them.
///
/// @p ids is what `rekeyPads()` moved, the grid's own id included. Every end of
/// a pad link needs it: the grid's macros and mods point down into the pads,
/// the pad rack's own point at what it holds, and a pad device's point at its
/// siblings. A link out of the subtree names an id this does not hold and is
/// left alone, which is what `remapPresetPath()` does with an unmapped id.
void retargetPadLinks(DeviceInfo& device, TrackId trackId, const ChainIdRemap& ids) {
    if (!device.pads)
        return;

    PresetIdRemap remap;
    remap.trackId = trackId;
    remap.devices = ids.devices;
    remap.racks = ids.racks;
    remap.chains = ids.chains;

    remapPresetLinks(device.macros, device.mods, remap);
    remapRackPresetLinks(*device.pads.get(), remap, PresetState::Keep);
}

void collectDeviceIdMatches(std::vector<ChainElement>& elements, TrackId trackId, DeviceId deviceId,
                            std::vector<DeviceInfo*>& matches) {
    // Pads entered: this asks whether a bare device id names exactly one
    // device, and a pad device is one. Skipping them would answer "unique" for
    // an id that two devices hold.
    chain_walk::forEachDevice(elements, ChainNodePath::trackLevel(trackId), chain_walk::Pads::Enter,
                              [deviceId, &matches](DeviceInfo& device, const ChainNodePath&) {
                                  if (device.id == deviceId)
                                      matches.push_back(&device);
                              });
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
    collectDeviceIdMatches(masterTrack.chain.fxChainElements, masterTrack.id, deviceId, matches);
    collectDeviceIdMatches(masterTrack.chain.postFxChainElements, deviceId, matches);
    collectDeviceIdMatches(masterTrack.chain.mixerAnalysisElements, deviceId, matches);

    for (auto& track : tracks) {
        collectDeviceIdMatches(track.chain.fxChainElements, track.id, deviceId, matches);
        collectDeviceIdMatches(track.chain.postFxChainElements, deviceId, matches);
        collectDeviceIdMatches(track.chain.mixerAnalysisElements, deviceId, matches);
    }

    return matches.size() == 1 ? matches.front() : nullptr;
}

/// Follow @p path from @p index inside @p elements, ending on its Device step.
///
/// The ordinary `Rack > Chain > ... > Device` alternation, walked from wherever
/// the caller has already got to. A pad's chain holds elements like any other,
/// racks included, so the tail of a pad device's address is an ordinary route.
DeviceInfo* followChainSteps(std::vector<ChainElement>& elements, const ChainNodePath& path,
                             std::size_t index) {
    if (index >= path.steps.size())
        return nullptr;

    const auto& step = path.steps[index];

    if (step.type == ChainStepType::Device) {
        // A Device step is a leaf: anything after it describes no route.
        if (index + 1 != path.steps.size())
            return nullptr;
        for (auto& element : elements)
            if (magda::isDevice(element) && magda::getDevice(element).id == step.id)
                return &magda::getDevice(element);
        return nullptr;
    }

    if (step.type != ChainStepType::Rack || index + 1 >= path.steps.size() ||
        path.steps[index + 1].type != ChainStepType::Chain)
        return nullptr;

    for (auto& element : elements) {
        if (!magda::isRack(element) || magda::getRack(element).id != step.id)
            continue;
        for (auto& chain : magda::getRack(element).chains)
            if (chain.id == path.steps[index + 1].id)
                return followChainSteps(chain.elements, path, index + 2);
        return nullptr;
    }
    return nullptr;
}

/// The device @p deviceId names, searched for its pads rather than itself.
///
/// A grid can sit anywhere a device can, including inside a rack chain, so the
/// search descends the same way `findDeviceUnder` does.
DeviceInfo* findPadOwner(std::vector<ChainElement>& elements, DeviceId deviceId) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            if (device.id == deviceId)
                return device.pads ? &device : nullptr;
            continue;
        }

        if (magda::isRack(element))
            for (auto& chain : magda::getRack(element).chains)
                if (auto* found = findPadOwner(chain.elements, deviceId))
                    return found;
    }
    return nullptr;
}

}  // namespace

// ============================================================================
// Device Management in Chains
// ============================================================================

DeviceId TrackManager::addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                                        const DeviceInfo& device) {
    if (auto* track = getTrack(trackId)) {
        if (!track->canHostInstrument() && device.isInstrument) {
            return INVALID_DEVICE_ID;
        }
    }
    if (auto* chain = getChain(trackId, rackId, chainId)) {
        DeviceInfo newDevice = prepareNewDevice(trackId, device);
        seedSidechainModIfMissing(
            newDevice, ChainNodePath::chainDevice(trackId, rackId, chainId, newDevice.id));
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
        if (!track->canHostInstrument() && device.isInstrument) {
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
        DeviceInfo newDevice = prepareNewDevice(chainPath.trackId, device);
        seedSidechainModIfMissing(newDevice, chainPath.withDevice(newDevice.id));
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
        if (!track->canHostInstrument() && device.isInstrument) {
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
        DeviceInfo newDevice = prepareNewDevice(chainPath.trackId, device);
        seedSidechainModIfMissing(newDevice, chainPath.withDevice(newDevice.id));

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
            clearSelectionsUnderDevice(
                magda::getDevice(*it),
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
/**
 * @brief The chain a path names, or nothing.
 *
 * Delegates rather than resolving again. This was a second copy of
 * `TrackManager::getChainByPath` — same "drop the last step, resolve the rack,
 * search its chains" body — and the two drifted the moment one of them learned
 * something the other did not: the structural guards added for #1993 went into
 * `getChainByPath`, so `rack > chain1 > chain2` was refused there and still
 * resolved to the sibling `chain2` here.
 *
 * That mattered because this copy is the one `getDeviceInChainByPath` uses, and
 * device paths are the surface a remote client supplies verbatim
 * (`toChainNodePath` builds them from whatever steps arrive). One body means
 * one set of rules for both.
 */
static ChainInfo* getChainFromPath(TrackManager& tm, const ChainNodePath& chainPath) {
    return tm.getChainByPath(chainPath);
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
    return elementPath.parentChain();
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

/// Where every device under @p element now lives, keyed by id, for the links
/// naming them to be retargeted onto.
///
/// Pads entered: a Drum Grid carries its pad devices with it. This stopped at
/// the grid, so moving one to another track left every link into its pads
/// holding the path it had on the track it came from -- a device id that
/// `retargetMovedTarget` never found, so the whole address survived unchanged
/// (#2204).
static void collectMovedDevicePaths(const ChainElement& element, const ChainNodePath& elementPath,
                                    DevicePathMap& movedPaths) {
    const std::span<const ChainElement> subtree{&element, 1};
    const auto parentPath =
        magda::isDevice(element) ? elementPath.parentChain() : elementPath.parent();

    chain_walk::forEachDevice(subtree, parentPath, chain_walk::Pads::Enter,
                              [&movedPaths](const DeviceInfo& device, const ChainNodePath& path) {
                                  movedPaths[device.id] = path;
                              });
}

/// Point every link @p elements OWNS at where the moved devices now are.
///
/// Pads entered: a pad device owns macros and mods like any other, and a
/// modifier on one pointing at its own parameter is the ordinary case. This
/// descended only through racks, so such a link went on naming the track its
/// grid came from (#2204).
static void retargetLinksInElements(std::vector<ChainElement>& elements,
                                    const ChainNodePath& parentPath,
                                    const DevicePathMap& movedPaths) {
    // The real parent, not the track: these run over a nested chain's elements
    // as well as a track's own list, and a walk told the wrong parent spells
    // every device in it as top-level. Nothing here reads the address it
    // builds, which is exactly why passing the wrong one would sit unnoticed.
    chain_walk::forEachNode(
        elements, parentPath, chain_walk::Pads::Enter,
        [&movedPaths](DeviceInfo& device, const ChainNodePath&) {
            retargetMovedLinks(device.macros, device.mods, movedPaths);
        },
        [&movedPaths](RackInfo& rack, const ChainNodePath&) {
            retargetMovedLinks(rack.macros, rack.mods, movedPaths);
            return chain_walk::Descend::Into;
        });
}

static void retargetMovedLinksInTrack(TrackInfo& track, const DevicePathMap& movedPaths) {
    retargetMovedLinks(track.macros, track.mods, movedPaths);
    retargetLinksInElements(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                            movedPaths);
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

/// Drop every link @p elements owns to a device that has left the track.
///
/// The mirror of the above, and it skipped pads the same way: a pad device
/// staying put kept a link to something no longer on its track (#2204).
static void removeMovedTargetsInElements(std::vector<ChainElement>& elements,
                                         const ChainNodePath& parentPath,
                                         const DevicePathMap& movedPaths) {
    chain_walk::forEachNode(
        elements, parentPath, chain_walk::Pads::Enter,
        [&movedPaths](DeviceInfo& device, const ChainNodePath&) {
            removeMovedTargets(device.macros, device.mods, movedPaths);
        },
        [&movedPaths](RackInfo& rack, const ChainNodePath&) {
            removeMovedTargets(rack.macros, rack.mods, movedPaths);
            return chain_walk::Descend::Into;
        });
}

static void removeMovedTargetsInTrack(TrackInfo& track, const DevicePathMap& movedPaths) {
    removeMovedTargets(track.macros, track.mods, movedPaths);
    removeMovedTargetsInElements(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                                 movedPaths);
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

    // The shared walk: fresh ids for every device, rack and chain, pads
    // included, with the synthetic pad rack id recorded so a link naming one can
    // be followed (#2221).
    ChainIdRemap ids;
    tm.reassignChainElementIds(elements, ids);
    remap.devices = std::move(ids.devices);
    remap.racks = std::move(ids.racks);
    remap.chains = std::move(ids.chains);

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

/// The chain element @p path addresses, device or rack, or null.
const ChainElement* findChainElement(TrackManager& tm, const ChainNodePath& path) {
    const auto parentPath = getParentChainPathForElementPath(path);
    const auto* elements = getElementContainerForChainPath(tm, parentPath);
    if (elements == nullptr)
        return nullptr;

    const auto type = path.topLevelDeviceId != INVALID_DEVICE_ID
                          ? ChainStepType::Device
                          : (!path.steps.empty() ? path.steps.back().type : ChainStepType::Device);
    const auto id = path.topLevelDeviceId != INVALID_DEVICE_ID
                        ? path.topLevelDeviceId
                        : (!path.steps.empty() ? path.steps.back().id : INVALID_DEVICE_ID);

    for (const auto& element : *elements) {
        if (type == ChainStepType::Device) {
            if (magda::isDevice(element) && magda::getDevice(element).id == id)
                return &element;
            continue;
        }
        if (magda::isRack(element) && magda::getRack(element).id == id)
            return &element;
    }
    return nullptr;
}

PlacementRefusal TrackManager::checkPlacement(const PlacementRequest& request) const {
    if (request.subtree == nullptr)
        return PlacementRefusal::Allowed;

    // Reordering inside the container a subtree already lives in changes neither
    // the track nor the ids anything is keyed on, so the rules below, which all
    // exist to protect one of those, do not apply to it.
    if (!request.leavesItsContainer)
        return PlacementRefusal::Allowed;

    // A rack cannot be put inside one of its own chains.
    if (chainPathContainsRack(request.destination, request.sourcePath))
        return PlacementRefusal::DestinationInsideSource;

    // The destination track has to be able to hold what is coming. Asked of the
    // whole subtree, because a rack carries its devices with it.
    if (const auto* destinationTrack = getTrack(request.destination.trackId)) {
        if (!destinationTrack->canHostInstrument() && elementContainsInstrument(*request.subtree))
            return PlacementRefusal::DestinationCannotHostInstrument;
    } else {
        return PlacementRefusal::DestinationCannotHostInstrument;
    }

    // A pad on a bus is carried by the output instance made for a top-level
    // instrument. Nowhere else carries one, so a destination that is not a
    // track's own list would silently drop the routing, and leaving the track
    // the buses were realised on would leave its child tracks behind. Neither
    // clean-up is part of the undoable step (#2211).
    //
    // A source-less reconstruction is neither: a paste, or an undo putting a
    // deleted device back, owns no child tracks and leaves no track. Refusing it
    // would have made undoing the deletion of a routed grid restore nothing at
    // all, silently, because the callers discard the result (#2221).
    //
    // Only the root lands where the caller says it lands. A routed grid deeper
    // in the subtree stays nested inside that root wherever it goes, so it can
    // never be the top-level instrument its bus needs, whatever the destination.
    if (anyPadOnABusBelowRoot(*request.subtree))
        return PlacementRefusal::PadOnABus;

    if (magda::isDevice(*request.subtree) && anyPadOnABus(magda::getDevice(*request.subtree))) {
        const bool hasSource = request.sourcePath.trackId != INVALID_TRACK_ID;
        const bool leavesItsTrack =
            hasSource && request.destination.trackId != request.sourcePath.trackId;
        if (!request.destinationIsTrackTopLevel || leavesItsTrack)
            return PlacementRefusal::PadOnABus;
    }

    // And the same for a multi-out pair: the child track's link names the track
    // the device stands on, so moving it strands the link, and a device off the
    // top level has no output instance to carry a bus at all (#2220).
    if (ownsMultiOutChildTracks(*this, *request.subtree, request.sourcePath.trackId))
        return PlacementRefusal::OwnsMultiOutChildTracks;

    return PlacementRefusal::Allowed;
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

    // Refused for the same reason a wrap is: dropping a grid into a rack takes
    // it somewhere no bus is carried, so the move would have to put every pad
    // back on the main mix and take its child tracks down, and that clean-up is
    // not part of the undoable step the move is. Undo would return the grid to
    // the top level with its routing gone. Reordering within the track's own
    // list is not a placement change and stays allowed (#2211).
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

    const bool sameContainer = sourceElements == destinationElements;

    // One question, asked before anything is mutated (#2221).
    if (checkPlacement({&*sourceIt, sourceElementPath, destinationChainPath, !sameContainer,
                        destinationChainPath.steps.empty()}) != PlacementRefusal::Allowed)
        return false;

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
        retargetLinksInElements(*destinationElements, destinationChainPath, movedPaths);
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

bool TrackManager::insertFlatSectionDeviceByPath(const ChainNodePath& devicePath, DeviceInfo device,
                                                 int index) {
    auto* track = getTrack(devicePath.trackId);
    if (track == nullptr || device.id == INVALID_DEVICE_ID)
        return false;

    const bool postFx = devicePath.isPostFx();
    if (!postFx && !devicePath.isMixerAnalysis())
        return false;

    auto& section = postFx ? track->chain.postFxChainElements : track->chain.mixerAnalysisElements;
    index = std::clamp(index, 0, static_cast<int>(section.size()));
    section.insert(section.begin() + index, PostFxChainElement{std::move(device)});
    notifyTrackDevicesChanged(devicePath.trackId);
    return true;
}

bool TrackManager::insertChainElementsByPath(const ChainNodePath& destinationChainPath,
                                             std::vector<ChainElement> elements, int insertIndex,
                                             bool reassignIds) {
    auto* destinationElements = getElementContainerForChainPath(*this, destinationChainPath);
    if (destinationElements == nullptr || elements.empty())
        return false;

    // This asked nothing before, so a paste could put an instrument on a track
    // that cannot host one. There is no source: the elements are arriving, so
    // the rules keyed on where they came from pass and the ones about what is
    // being placed still apply (#2221).
    for (const auto& element : elements) {
        if (checkPlacement(
                {&element, {}, destinationChainPath, true, destinationChainPath.steps.empty()}) !=
            PlacementRefusal::Allowed)
            return false;
    }

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
                                             const juce::String& rackName, RackId presetRackId,
                                             ChainId presetChainId) {
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

        // Wrapping takes an element off the top level, which is a placement
        // change like any other. Asked the same way: this used to check only
        // whether a pad was on a bus, so wrapping a multi-out instrument
        // stranded the child tracks a move of it refuses to strand (#2221).
        if (const auto* element = findChainElement(*this, path);
            element != nullptr && checkPlacement({element, path, sourceChainPath, true, false}) !=
                                      PlacementRefusal::Allowed)
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
    // A redo passes the ids its first run allocated, so the rack keeps the
    // identity every link naming it was made against (#2221).
    rack.id = presetRackId != INVALID_RACK_ID ? presetRackId : allocateRackId();
    rack.name = rackName.isEmpty() ? "Rack" : rackName;
    ChainInfo chain;
    chain.id = presetChainId != INVALID_CHAIN_ID ? presetChainId : allocateChainId();
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
    // The two flat sections hold bare devices rather than chain elements, so
    // the container lookup below has no answer for them. Without this a removal
    // command could not record where a post-fader device stood and stopped
    // before removing it, which is why the structural matrix recorded every
    // flat-section removal as refused (#2232).
    if (elementPath.isPostFx() || elementPath.isMixerAnalysis()) {
        auto* track = getTrack(elementPath.trackId);
        if (track == nullptr)
            return -1;

        const auto& section = elementPath.isPostFx() ? track->chain.postFxChainElements
                                                     : track->chain.mixerAnalysisElements;
        const auto deviceId = elementPath.getDeviceId();
        for (int i = 0; i < static_cast<int>(section.size()); ++i) {
            if (section[static_cast<size_t>(i)].device.id == deviceId)
                return i;
        }
        return -1;
    }

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
            clearSelectionsUnderDevice(it->device, devicePath);
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
            clearSelectionsUnderDevice(magda::getDevice(*it), devicePath);
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
            clearSelectionsUnderDevice(magda::getDevice(*it), devicePath);
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

    // A typed pad address says outright that its owner step is a DeviceId, so
    // it goes straight to the pad route rather than walking a rack tree that
    // cannot contain it (#2219).
    if (devicePath.isPadOwned())
        return getDeviceInPadByPath(devicePath);

    // Otherwise, device is inside a chain
    if (auto* chain = getChainFromPath(*this, chainPath)) {
        for (auto& element : chain->elements) {
            if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                return &magda::getDevice(element);
            }
        }
    }

    // Last: an untyped pad address from a project saved before the pad step
    // types. Ordering is the tie-break, as it always was.
    return getDeviceInPadByPath(devicePath);
}

DeviceInfo* TrackManager::getDeviceInPadByPath(const ChainNodePath& devicePath) {
    // A pad device's chain is not in the rack tree. Its address names the
    // owning device by that device's own id, and a pad rack is
    // `DeviceInfo::pads` rather than a chain element, so `getRackByPath()`
    // cannot follow it and was never meant to (#2207).
    //
    // `PadRack(gridDeviceId) > PadChain(pad)` is the prefix; everything past it
    // is an ordinary route, because a pad's chain holds racks like any other
    // chain does and every walk that reaches a pad recurses through them.
    //
    // Two spellings are accepted. The typed one is what `padChainPath()` builds
    // and what `getDeviceInChainByPath()` dispatches on directly, because the
    // types say the leading id is a DeviceId and no allocated rack can be
    // confused with it. The untyped `Rack > Chain` one is what projects saved
    // before the pad step types carry; `migrateStagedPadPaths()` retypes those
    // on load, and until a project is re-saved this still resolves them, tried
    // only after the ordinary rack route has failed so an allocated rack
    // sharing the number wins exactly as it used to (#2219).
    if (devicePath.steps.size() < 3)
        return nullptr;
    const bool typed = devicePath.steps[0].type == ChainStepType::PadRack &&
                       devicePath.steps[1].type == ChainStepType::PadChain;
    const bool legacy = devicePath.steps[0].type == ChainStepType::Rack &&
                        devicePath.steps[1].type == ChainStepType::Chain;
    if (!typed && !legacy)
        return nullptr;

    auto* track = getTrack(devicePath.trackId);
    if (track == nullptr)
        return nullptr;

    auto* owner = findPadOwner(track->chain.fxChainElements, devicePath.steps[0].id);
    if (owner == nullptr)
        return nullptr;

    for (auto& pad : owner->pads->chains)
        if (pad.id == devicePath.steps[1].id)
            return followChainSteps(pad.elements, devicePath, 2);

    return nullptr;
}

ChainInfo* TrackManager::getChainInPadByPath(const ChainNodePath& chainPath) {
    // `PadRack(gridDeviceId) > PadChain(pad)` and then ordinary `Rack > Chain`
    // pairs: a pad's chain holds racks like any other chain does, and their
    // chains are addressable. Stopping at the pad chain would leave
    // `getElementContainerForChainPath()` unable to name them, so copy, move,
    // remove, wrap, insert and paste could not act on anything inside a rack
    // nested under a pad even though device lookup resolves the same route
    // (#2219).
    if (!chainPath.isPadOwned() || chainPath.steps.size() < 2 ||
        chainPath.getPadChainId() == INVALID_CHAIN_ID)
        return nullptr;

    // The tail is whole pairs, and the address ends on the chain it names.
    if ((chainPath.steps.size() - 2) % 2 != 0)
        return nullptr;

    auto* track = getTrack(chainPath.trackId);
    if (track == nullptr)
        return nullptr;

    auto* owner = findPadOwner(track->chain.fxChainElements, chainPath.getPadOwnerDeviceId());
    if (owner == nullptr)
        return nullptr;

    ChainInfo* current = nullptr;
    for (auto& pad : owner->pads->chains) {
        if (pad.id == chainPath.getPadChainId()) {
            current = &pad;
            break;
        }
    }
    if (current == nullptr)
        return nullptr;

    for (std::size_t index = 2; index < chainPath.steps.size(); index += 2) {
        const auto& rackStep = chainPath.steps[index];
        const auto& chainStep = chainPath.steps[index + 1];
        if (rackStep.type != ChainStepType::Rack || chainStep.type != ChainStepType::Chain)
            return nullptr;

        ChainInfo* next = nullptr;
        for (auto& element : current->elements) {
            if (!magda::isRack(element) || magda::getRack(element).id != rackStep.id)
                continue;
            for (auto& chain : magda::getRack(element).chains) {
                if (chain.id == chainStep.id) {
                    next = &chain;
                    break;
                }
            }
            break;
        }
        if (next == nullptr)
            return nullptr;
        current = next;
    }

    return current;
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
        if (bypassed)
            device->deltaSolo = false;
        notifyDevicePropertyChanged(devicePath);
    }
}

void TrackManager::setDeviceDeltaSoloByPath(const ChainNodePath& devicePath, bool deltaSolo) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->deltaSolo = deltaSolo;
        if (deltaSolo)
            device->bypassed = false;
        notifyDevicePropertyChanged(devicePath);
    }
}

void TrackManager::setDeviceInChainMidiInThruByPath(const ChainNodePath& devicePath, bool thru) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        device->midiInThru = thru;
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

namespace {

/// The path @p parentChain gives a device it directly holds.
///
/// A track's own FX list is not a chain and its devices are addressed as
/// top-level ones, which is the same split `AddDeviceByPathCommand` makes.
ChainNodePath devicePathUnder(const ChainNodePath& parentChain, DeviceId deviceId) {
    return parentChain.getType() == ChainNodeType::Track
               ? ChainNodePath::topLevelDevice(parentChain.trackId, deviceId)
               : parentChain.withDevice(deviceId);
}

/// The path addressing @p deviceId somewhere under @p parentChain, or an
/// invalid path when it is not there.
///
/// Descends into a rack's chains and into a pad-per-chain device's pads. A pad
/// device is an ordinary DeviceInfo since #2207, so anything that starts from a
/// DeviceId and needs a path -- alias generation, automation targets, link
/// repair -- has to be able to reach one (#2211).
ChainNodePath findDeviceUnder(const std::vector<ChainElement>& elements,
                              const ChainNodePath& parentChain, DeviceId deviceId) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            if (device.id == deviceId)
                return devicePathUnder(parentChain, deviceId);

            if (!device.pads)
                continue;

            // A pad's address spells its rack step with the grid's own
            // DeviceId: what padChainPath() builds, and what every link a
            // project has saved to a pad device carries.
            const auto gridPath = devicePathUnder(parentChain, device.id);
            for (const auto& pad : device.pads->chains)
                if (auto found = findDeviceUnder(
                        pad.elements, TrackManager::padChainPath(gridPath, pad.id), deviceId);
                    found.isValid())
                    return found;
            continue;
        }

        if (magda::isRack(element)) {
            const auto& rack = magda::getRack(element);
            const auto rackPath = parentChain.getType() == ChainNodeType::Track
                                      ? ChainNodePath::rack(parentChain.trackId, rack.id)
                                      : parentChain.withRack(rack.id);
            for (const auto& chain : rack.chains)
                if (auto found =
                        findDeviceUnder(chain.elements, rackPath.withChain(chain.id), deviceId);
                    found.isValid())
                    return found;
        }
    }
    return {};
}

}  // namespace

ChainNodePath TrackManager::findDevicePath(DeviceId deviceId) const {
    for (const auto& track : tracks_)
        if (auto found = findDeviceUnder(track.chain.fxChainElements,
                                         ChainNodePath::trackLevel(track.id), deviceId);
            found.isValid())
            return found;

    if (auto found = findDeviceUnder(masterTrack_.chain.fxChainElements,
                                     ChainNodePath::trackLevel(MASTER_TRACK_ID), deviceId);
        found.isValid())
        return found;

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

void TrackManager::setDeviceAiSoundDesignerParameters(const ChainNodePath& devicePath,
                                                      const std::vector<int>& aiParams) {
    if (auto* device = getDeviceInChainByPath(devicePath))
        device->aiSoundDesignerParameters = aiParams;
}

void TrackManager::setDeviceAiSoundDesignerPrompt(const ChainNodePath& devicePath,
                                                  const juce::String& prompt) {
    if (auto* device = getDeviceInChainByPath(devicePath))
        device->aiSoundDesignerPrompt = prompt;
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

namespace {

/// Push an internal device's state document into its running plugin, if it has
/// one. The projection direction: the model already holds the document, the
/// engine adapter is told to match it.
void projectAuthoredStateToEngine(AudioEngine* audioEngine, const ChainNodePath& devicePath,
                                  const juce::String& docText, const juce::String& deviceType) {
    if (audioEngine == nullptr)
        return;
    auto* bridge = audioEngine->getAudioBridge();
    if (bridge == nullptr)
        return;
    auto plugin = bridge->getPlugin(devicePath);
    if (plugin == nullptr)
        return;

    namespace ta = daw::audio::tracktion_adapter;
    auto tree = ta::devicePluginTreeFromState(docText);
    if (!tree.isValid()) {
        // An empty snapshot is still a state: "nothing authored". Project a
        // bare typed tree so a device whose contract reads absence as none (a
        // convolution's impulse response) actually unloads, rather than the
        // model saying the edit was undone while the engine keeps playing it.
        tree = juce::ValueTree(tracktion::engine::IDs::PLUGIN);
        tree.setProperty(tracktion::engine::IDs::type, deviceType, nullptr);
    }
    plugin->restorePluginStateFromValueTree(tree);
}

}  // namespace

bool TrackManager::updateDeviceAuthoredState(const ChainNodePath& devicePath,
                                             const std::function<void(device_state::Doc&)>& patch) {
    auto* device = getDeviceInChainByPath(devicePath);
    if (device == nullptr || device->format != PluginFormat::Internal)
        return false;
    if (device_state::isFutureDeviceState(device->pluginState))
        return false;

    device_state::Doc doc;
    if (auto decoded = device_state::decode(device->pluginState))
        doc = std::move(*decoded);
    doc.deviceType = device->pluginId;

    // A pre-#2317 document still carries the retired duplicate parameter
    // record, and encode() writes whatever is in `params`. The record was
    // consumed by the load-time hydration; writing it back here would leave a
    // second persisted authority alive in every path that never passes through
    // a Tracktion capture (preset saves, native-only sessions), free to
    // hydrate stale values on the next load. This edit is the moment the
    // document goes canonical.
    doc.params.clear();
    doc.paramsAreDisplayDomain = false;

    patch(doc);

    device->pluginState = device_state::encode(doc);
    projectAuthoredStateToEngine(audioEngine_, devicePath, device->pluginState, device->pluginId);
    notifyDevicePropertyChanged(devicePath);
    return true;
}

bool TrackManager::setDeviceAuthoredState(const ChainNodePath& devicePath,
                                          const juce::String& docText) {
    auto* device = getDeviceInChainByPath(devicePath);
    if (device == nullptr || device->format != PluginFormat::Internal)
        return false;
    // Same contract as updateDeviceAuthoredState: state this build cannot read
    // must not be replaced blind - and must not be ACCEPTED blind either. An
    // incoming future-schema snapshot would sit in the model unreadable while
    // the projection restored a bare tree in its place.
    if (device_state::isFutureDeviceState(device->pluginState) ||
        device_state::isFutureDeviceState(docText))
        return false;

    // Canonicalize at the WRITE BOUNDARY, not per mutation path: a snapshot
    // taken from a pre-#2317 document still carries the retired parameter
    // record, and an undo that put it back verbatim would recreate the second
    // authority the edit itself just eliminated. Anything else decode refuses -
    // legacy engine XML, an empty string - passes through unchanged; only a
    // readable v2 document is stripped.
    auto canonical = docText;
    if (auto decoded = device_state::decode(docText)) {
        // A readable document also has to be THIS device's: seating another
        // device's authored state is corruption, not restoration. The saved
        // type may be an older load alias, so the registry has the final say.
        auto savedType = decoded->deviceType;
        if (const auto* spec = daw::audio::findInternalPluginSpecForLoadType(savedType);
            spec != nullptr && spec->pluginId != nullptr)
            savedType = spec->pluginId;
        if (savedType != device->pluginId)
            return false;

        decoded->params.clear();
        decoded->paramsAreDisplayDomain = false;
        canonical = device_state::encode(*decoded);
    }

    device->pluginState = std::move(canonical);
    projectAuthoredStateToEngine(audioEngine_, devicePath, device->pluginState, device->pluginId);
    notifyDevicePropertyChanged(devicePath);
    return true;
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
                            proc->populateParameters(*live, DeviceProcessor::ValueSource::Engine);
                    }
                } else {
                    namespace ta = daw::audio::tracktion_adapter;
                    auto savedState = ta::devicePluginTreeFromState(live->pluginState);
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

DeviceInfo TrackManager::prepareNewDevice(TrackId trackId, const DeviceInfo& device) {
    DeviceInfo newDevice = device;
    newDevice.id = nextFxDeviceId_++;

    // The grid's own id is in the map because a pad path names the grid rather
    // than a route to it: every link into these pads carries the old DeviceId
    // in its PadRack step, and the ones the grid's own macros and mods hold
    // carry it in `topLevelDeviceId` as well.
    ChainIdRemap ids;
    ids.devices[device.id] = newDevice.id;
    rekeyPads(newDevice, ids);
    retargetPadLinks(newDevice, trackId, ids);

    applyCachedCapabilitiesToDevice(newDevice);
    stampDefaultKitIfMissing(newDevice);
    if (daw::audio::isInternalAnalysisPlugin(newDevice.pluginId))
        newDevice.deviceType = DeviceType::Analysis;
    return newDevice;
}

void TrackManager::rekeyPads(DeviceInfo& device, ChainIdRemap& remap) {
    if (!device.pads)
        return;

    // Both the pad rack's id and its devices' are DeviceIds in disguise, so a
    // copied Drum Grid that kept them would key the ops of the one it was
    // copied from: the plan would emit two devices onto one op and the executor
    // would run whichever it saw last (#2207).
    stampPadRackId(device);

    // A pad holds chain elements like any other chain, nested racks included,
    // so the same recursive walk re-keys them. A shallow pass over the direct
    // pad devices left everything inside a pad's rack carrying the source's
    // DeviceIds, so a copied grid shared ops with the one it came from.
    //
    // Reported rather than discarded: the ids this moves are the ones a macro
    // or a mod addressing anything in the pad subtree was pointing at, and a
    // link left on the old address resolves to nothing.
    for (auto& pad : device.pads->chains)
        reassignChainElementIds(pad.elements, remap);
}

void TrackManager::reassignChainElementIds(std::vector<ChainElement>& elements,
                                           ChainIdRemap& remap) {
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            auto& device = magda::getDevice(element);
            const auto oldDeviceId = device.id;
            device.id = allocateDeviceId();
            remap.devices[oldDeviceId] = device.id;

            if (!device.pads)
                continue;

            // A device's pads are a rack it owns, and their contents are chain
            // elements like any other, so they are re-keyed by the same walk.
            // Two of the four walkers this replaces stopped at the device and
            // left a preset's pad DeviceIds in a live project.
            //
            // The pad rack's own id is derived from the device's, so it moves
            // with it. Recorded, because a stored link can name it and there is
            // otherwise nothing to follow it by.
            remap.racks[padRackIdFor(oldDeviceId)] = padRackIdFor(device.id);
            stampPadRackId(device);

            // Pad chain ids are rack-local and stay as they are, which is what
            // keeps a link naming a pad still naming it.
            for (auto& pad : device.pads->chains)
                reassignChainElementIds(pad.elements, remap);
            continue;
        }

        if (!magda::isRack(element))
            continue;

        auto& rack = magda::getRack(element);
        const auto oldRackId = rack.id;
        rack.id = allocateRackId();
        remap.racks[oldRackId] = rack.id;

        for (auto& chain : rack.chains) {
            const auto oldChainId = chain.id;
            chain.id = allocateChainId();
            remap.chains[oldChainId] = chain.id;
            reassignChainElementIds(chain.elements, remap);
        }
    }
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
    // freshly-loaded subtree doesn't collide with other live elements' runtime
    // IDs. Macros and mods are indexed within their parent and don't need
    // reassignment. Through the shared walk, which descends into a device's
    // pads: this one used not to, so a rack preset holding a Drum Grid brought
    // the preset's pad DeviceIds in with it (#2221).
    ChainIdRemap ids;
    for (auto& chain : live->chains) {
        const auto oldChainId = chain.id;
        chain.id = allocateChainId();
        ids.chains[oldChainId] = chain.id;
        reassignChainElementIds(chain.elements, ids);
    }
    remap.devices.merge(ids.devices);
    remap.racks.merge(ids.racks);
    remap.chains.merge(ids.chains);
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
    // don't collide with other live elements' runtime IDs, through the shared
    // walk. This one used not to descend into a device's pads either (#2221).
    PresetIdRemap remap;
    remap.trackId = trackId;
    ChainIdRemap ids;
    reassignChainElementIds(presetElements, ids);
    remap.devices = std::move(ids.devices);
    remap.racks = std::move(ids.racks);
    remap.chains = std::move(ids.chains);
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

void TrackManager::setDeviceParameterValueFromPlugin(DeviceId deviceId, int paramIndex,
                                                     float value) {
    const auto path = findDevicePath(deviceId);
    if (path.isValid())
        setDeviceParameterValueFromPlugin(path, paramIndex, value);
}

bool TrackManager::isChordTrackMuted() const {
    const auto chordTrackId = getChordTrackId();
    const auto* chordTrack = chordTrackId != INVALID_TRACK_ID ? getTrack(chordTrackId) : nullptr;
    return chordTrack != nullptr && chordTrack->muted;
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

    // Wrapping takes the device off the top level, where the output instance
    // that carries a bus is made, so it is a placement change and asks the same
    // question every other one does. This used to check only whether a pad was
    // on a bus, so wrapping a multi-out instrument stranded the child tracks a
    // move of it refuses to strand (#2211, #2221).
    if (checkPlacement({&*it, ChainNodePath::topLevelDevice(trackId, deviceId),
                        ChainNodePath::trackLevel(trackId), true, false}) !=
        PlacementRefusal::Allowed)
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

    // The same question the top-level branch asks through `wrapDeviceInRack()`.
    // A nested device can drive multi-out child tracks too, and wrapping moves
    // it into a container it was not in, so this is a placement change like any
    // other and was going round the boundary (#2221).
    if (checkPlacement({&*it, devicePath, chainPath, true, false}) != PlacementRefusal::Allowed)
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

void TrackManager::removeRackFromChainByPath(const ChainNodePath& rackPath) {
    // rackPath ends with a Rack step - we need to find the parent chain and remove this rack
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
            if (magda::isRack(*it) && magda::getRack(*it).id == rackId) {
                clearSelectionsUnderRack(magda::getRack(*it), rackPath);
                elements.erase(it);
                notifyTrackDevicesChanged(rackPath.trackId);
                return;
            }
        }
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

    if (updateElements(updateElements, masterTrack_.chain.fxChainElements)) {
        notifyDeviceModifiersChanged(MASTER_TRACK_ID);
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
