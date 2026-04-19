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

        case AutomationTargetType::SendLevel: {
            juce::String name = paramName.isNotEmpty()
                                    ? paramName
                                    : juce::String("Send ") + juce::String(sendBusIndex + 1);
            return ParameterPresets::faderVolume(-1, name);
        }

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
            // Backstop: ensure every DeviceParameter info carries a working
            // DisplayTextProvider so the lane scale labels, hover tooltips,
            // and the device slot all route display through the plugin's
            // own valueToString — same source of truth. The field isn't
            // serialized and paths like the project loader / ParameterConfig
            // apply can leave it null on the TrackManager-side copy even
            // when DeviceSlotComponent's local copy has been repopulated.
            //
            // Write the provider back into the device so subsequent calls
            // (and there are MANY on a lane resize drag — paint × scale
            // labels × frames) see it already set and don't allocate a
            // fresh shared_ptr on every call.
            auto& stored = device->parameters[static_cast<size_t>(paramIndex)];
            if (!stored.displayText && devicePath.getDeviceId() != INVALID_DEVICE_ID) {
                auto provider = std::make_shared<ParameterInfo::DisplayTextProvider>();
                provider->deviceId = devicePath.getDeviceId();
                provider->paramIndex = paramIndex;
                stored.displayText = std::move(provider);
            }
            ParameterInfo info = stored;

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
