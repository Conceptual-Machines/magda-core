// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace magda::sampler_devices {

/**
 * @brief The devices built on the Sampler, and where each keeps what the Sampler's edits write
 *        (#3100).
 *
 * Zone edits, imports, media collect and presets act on any device registered here, not only
 * the Sampler. Each names its parameter slot for every Sampler parameter it has, in
 * MagdaSamplerPlugin::ParamIndex order; the Sampler itself is registered, slot for slot.
 */
struct Layout {
    /// The device's slot for Sampler parameter `i` at `slots[i]`, or -1 when it has none. Empty
    /// is the Sampler's own order.
    std::vector<int> slots;

    int slot(int samplerParameter) const {
        if (slots.empty())
            return samplerParameter;
        return samplerParameter >= 0 && samplerParameter < static_cast<int>(slots.size())
                   ? slots[static_cast<size_t>(samplerParameter)]
                   : -1;
    }
};

/// The Sampler's plugin id.
inline constexpr const char* kSamplerPluginId = "magdasampler";

/// Register @p pluginId as built on the Sampler. Message thread, before its devices load.
void registerDevice(const juce::String& pluginId, Layout layout);

/// @p pluginId's layout, or null when it is not built on the Sampler.
const Layout* layoutOf(const juce::String& pluginId);

inline bool isSampler(const juce::String& pluginId) {
    return layoutOf(pluginId) != nullptr;
}

}  // namespace magda::sampler_devices
