#include "AutomationInfo.hpp"

#include "DeviceInfo.hpp"
#include "TrackManager.hpp"

namespace magda {

ParameterInfo AutomationTarget::getParameterInfo() const {
    switch (type) {
        case AutomationTargetType::TrackVolume:
            return ParameterPresets::faderVolume(-1, "Volume");

        case AutomationTargetType::TrackPan:
            return ParameterPresets::pan(-1, "Pan");

        case AutomationTargetType::DeviceParameter: {
            // Look up the real ParameterInfo populated by the owning
            // DeviceProcessor so labels/units/ranges come from the actual
            // plugin. Must use the path-based lookup since the target
            // device often sits inside a rack/chain — the flat
            // getDevice(trackId, deviceId) only scans top-level elements
            // and would leave us falling back to a generic percent scale.
            if (paramIndex < 0)
                break;
            auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
            if (!device)
                break;
            if (paramIndex >= static_cast<int>(device->parameters.size()))
                break;
            ParameterInfo info = device->parameters[static_cast<size_t>(paramIndex)];
            // Restore display name if the lane overrode it.
            if (paramName.isNotEmpty())
                info.name = paramName;
            return info;
        }

        case AutomationTargetType::Macro:
        case AutomationTargetType::ModParameter:
            break;
    }

    // Fallback for unresolved targets: generic percentage scale so the lane
    // remains usable even if the underlying device info is unavailable.
    return ParameterPresets::percent(-1, getDisplayName());
}

}  // namespace magda
