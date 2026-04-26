#include "ControllerParamWriter.hpp"

#include "../core/TrackManager.hpp"
#include "AudioBridge.hpp"

namespace magda {

void DefaultControllerParamWriter::write(const ResolvedTarget& resolved, float value) {
    if (!resolved.ok())
        return;

    const float clamped = juce::jlimit(0.0f, 1.0f, value);

    switch (resolved.owner) {
        case StaticTarget::Owner::PluginParam:
            writePluginParam(resolved, clamped);
            break;
        case StaticTarget::Owner::DeviceMacro:
            writeDeviceMacro(resolved, clamped);
            break;
    }
}

void DefaultControllerParamWriter::writePluginParam(const ResolvedTarget& resolved, float clamped) {
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

    // 'clamped' is normalized 0..1 (what BindingTransform produces). Map to the
    // parameter's actual value range before writing — te::AutomatableParameter::
    // setParameter expects raw, not normalized.
    const auto range = param->getValueRange();
    const float raw = static_cast<float>(range.getStart() + clamped * range.getLength());
    param->setParameterFromHost(raw, juce::sendNotificationSync);

    // Mirror the write into DeviceInfo and notify MAGDA listeners so param
    // sliders / inspector UIs update. Same path the plugin's native UI uses
    // when a knob is dragged on the plugin window.
    TrackManager::getInstance().setDeviceParameterValueFromPlugin(resolved.devicePath,
                                                                  resolved.paramIndex, raw);
}

void DefaultControllerParamWriter::writeDeviceMacro(const ResolvedTarget& resolved, float clamped) {
    TrackManager::getInstance().setDeviceMacroValue(resolved.devicePath, resolved.paramIndex,
                                                    clamped);
}

}  // namespace magda
