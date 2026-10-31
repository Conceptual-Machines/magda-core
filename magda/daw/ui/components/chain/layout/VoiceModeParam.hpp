// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/// The Poly / Mono / Legato choice MAGDA's instruments share.
inline bool isVoiceModeParameter(const magda::ParameterInfo& param) {
    return param.name == "Voice Mode" && param.choices.size() == 3;
}

/// Instruments that carry the switch in their own Voice tab, so the header leaves it out. The
/// Faust instrument's sits on its Voice page beside Glide and Bend Range.
inline bool faceplateShowsVoiceMode(const juce::String& pluginId) {
    return pluginId.equalsIgnoreCase("magda_polysynth") || pluginId.equalsIgnoreCase("magda_fm") ||
           pluginId.equalsIgnoreCase("magdasampler") ||
           pluginId.equalsIgnoreCase("faust-instrument");
}

/// The device's voice-mode parameter, or nullptr when it has none.
inline const magda::ParameterInfo* findVoiceModeParameter(const magda::DeviceInfo& device) {
    for (const auto& param : device.parameters)
        if (isVoiceModeParameter(param))
            return &param;
    return nullptr;
}

}  // namespace magda::daw::ui
