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

    // 'value' is normalized 0..1 (what BindingTransform produces). Map to the
    // parameter's actual value range before writing — te::AutomatableParameter::
    // setParameter expects raw, not normalized.
    const auto range = param->getValueRange();
    const float clamped = juce::jlimit(0.0f, 1.0f, value);
    const float raw = static_cast<float>(range.getStart() + clamped * range.getLength());
    param->setParameter(raw, juce::sendNotificationSync);
}

}  // namespace magda
