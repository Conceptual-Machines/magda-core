#include "ControllerParamWriter.hpp"

#include "AudioBridge.hpp"

namespace magda {

void DefaultControllerParamWriter::write(const ResolvedTarget& resolved, float value) {
    if (!resolved.ok())
        return;

    DeviceId deviceId = resolved.devicePath.getDeviceId();
    if (deviceId == INVALID_DEVICE_ID)
        return;

    auto plugin = bridge_.getPlugin(deviceId);
    if (!plugin)
        return;

    auto params = plugin->getAutomatableParameters();
    if (resolved.paramIndex < 0 || resolved.paramIndex >= static_cast<int>(params.size()))
        return;

    auto* param = params[static_cast<size_t>(resolved.paramIndex)];
    if (!param)
        return;

    param->setParameter(value, juce::sendNotificationSync);
}

}  // namespace magda
