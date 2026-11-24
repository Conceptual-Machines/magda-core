// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RackDevices.hpp"

#include "Crossover.hpp"
#include "PresetManager.hpp"

namespace magda {

namespace {

RackInfo newRack(const juce::String& name, bool multiband) {
    RackInfo rack;
    rack.id = 1;
    rack.name = name;
    rack.multiband = multiband;
    if (multiband)
        rack.crossovers = defaultCrossovers();
    for (std::size_t band = 0; band <= rack.crossovers.size(); ++band) {
        ChainInfo chain;
        chain.id = static_cast<ChainId>(band + 1);
        rack.chains.push_back(std::move(chain));
    }
    return rack;
}

}  // namespace

juce::String rackPresetDeviceId(const juce::String& presetName) {
    return kRackPresetDevicePrefix + presetName;
}

bool isRackDevice(const DeviceInfo& device) {
    return device.pluginId == kRackDeviceId || device.pluginId == kMultibandRackDeviceId ||
           device.pluginId.startsWith(kRackPresetDevicePrefix);
}

std::optional<RackInfo> rackForDevice(const DeviceInfo& device) {
    if (device.pluginId == kRackDeviceId)
        return newRack("Rack", false);
    if (device.pluginId == kMultibandRackDeviceId)
        return newRack("Multiband Rack", true);
    if (!device.pluginId.startsWith(kRackPresetDevicePrefix))
        return std::nullopt;

    RackInfo rack;
    const auto presetName =
        device.pluginId.fromFirstOccurrenceOf(kRackPresetDevicePrefix, false, false);
    if (!PresetManager::getInstance().loadRackPreset(presetName, rack))
        return std::nullopt;
    return rack;
}

}  // namespace magda
