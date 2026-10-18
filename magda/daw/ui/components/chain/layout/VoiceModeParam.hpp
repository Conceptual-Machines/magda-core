#pragma once

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/// The Poly / Mono / Legato choice MAGDA's instruments share; the slot footer draws it, so the
/// device's own grid and panel leave it out.
inline bool isVoiceModeParameter(const magda::ParameterInfo& param) {
    return param.name == "Voice Mode" && param.choices.size() == 3;
}

/// The device's voice-mode parameter, or nullptr when it has none.
inline const magda::ParameterInfo* findVoiceModeParameter(const magda::DeviceInfo& device) {
    for (const auto& param : device.parameters)
        if (isVoiceModeParameter(param))
            return &param;
    return nullptr;
}

}  // namespace magda::daw::ui
