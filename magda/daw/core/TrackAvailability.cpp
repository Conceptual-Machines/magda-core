// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TrackAvailability.hpp"

#include "ChainWalk.hpp"
#include "StringTable.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda {

namespace {

template <typename Elements>
void collectUnregistered(const Elements& elements, const ChainNodePath& path,
                         std::vector<const DeviceInfo*>& out) {
    chain_walk::forEachDevice(elements, path, chain_walk::Pads::Enter,
                              [&out](const DeviceInfo& device, const ChainNodePath&) {
                                  if (isUnregisteredDevice(device))
                                      out.push_back(&device);
                              });
}

/// A flat section is not a tree, but a device in it can still carry pads.
void collectUnregisteredFlat(const std::vector<PostFxChainElement>& elements, TrackId trackId,
                             std::vector<const DeviceInfo*>& out) {
    for (const auto& element : elements) {
        if (isUnregisteredDevice(element.device))
            out.push_back(&element.device);

        if (element.device.pads)
            for (const auto& pad : element.device.pads->chains)
                collectUnregistered(
                    pad.elements, ChainNodePath::padChain(trackId, element.device.id, pad.id), out);
    }
}

}  // namespace

bool isUnregisteredDevice(const DeviceInfo& device) {
    // The same two catalogs engine_adapter::isRegisteredDevice asks, which core cannot link.
    return device.format == PluginFormat::Internal && device.pluginId.isNotEmpty() &&
           daw::audio::findInternalPluginSpec(device.pluginId) == nullptr &&
           daw::audio::compiled::findCompiledPluginSpec(device.pluginId) == nullptr;
}

std::vector<const DeviceInfo*> unregisteredDevicesOn(const TrackInfo& track) {
    std::vector<const DeviceInfo*> devices;
    collectUnregistered(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id), devices);
    collectUnregisteredFlat(track.chain.postFxChainElements, track.id, devices);
    collectUnregisteredFlat(track.chain.mixerAnalysisElements, track.id, devices);
    return devices;
}

juce::String unavailableDevicesText(const juce::StringArray& names) {
    if (names.isEmpty())
        return {};
    const auto key = names.size() == 1 ? "devices.unavailable" : "devices.unavailable_other";
    return tr(key).replace("{0}", names.joinIntoString(", "));
}

std::set<TrackId> tracksHoldingUnregisteredDevices(const std::vector<TrackInfo>& tracks,
                                                   const TrackInfo& master) {
    std::set<TrackId> held;
    for (const auto& track : tracks)
        if (!unregisteredDevicesOn(track).empty())
            held.insert(track.id);

    if (!unregisteredDevicesOn(master).empty())
        held.insert(master.id);
    return held;
}

}  // namespace magda
