// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SamplerDevices.hpp"

#include <map>

namespace magda::sampler_devices {

namespace {
std::map<juce::String, Layout>& registry() {
    static std::map<juce::String, Layout> devices{{kSamplerPluginId, Layout{}}};
    return devices;
}
}  // namespace

void registerDevice(const juce::String& pluginId, Layout layout) {
    registry()[pluginId] = std::move(layout);
}

const Layout* layoutOf(const juce::String& pluginId) {
    const auto& devices = registry();
    const auto it = devices.find(pluginId);
    return it != devices.end() ? &it->second : nullptr;
}

}  // namespace magda::sampler_devices
