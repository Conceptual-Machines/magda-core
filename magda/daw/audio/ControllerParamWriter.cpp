#include "ControllerParamWriter.hpp"

#include "../core/TrackManager.hpp"
#include "AudioBridge.hpp"
#include "PluginManager.hpp"

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
            writeMacro(resolved, clamped);
            break;
        case StaticTarget::Owner::ModParam:
            writeModParam(resolved, clamped);
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

void DefaultControllerParamWriter::writeMacro(const ResolvedTarget& resolved, float clamped) {
    auto& tm = TrackManager::getInstance();
    switch (resolved.devicePath.getType()) {
        case ChainNodeType::Track:
            tm.setTrackMacroValue(resolved.devicePath.trackId, resolved.paramIndex, clamped);
            break;
        case ChainNodeType::Rack:
            tm.setRackMacroValue(resolved.devicePath, resolved.paramIndex, clamped);
            break;
        case ChainNodeType::TopLevelDevice:
        case ChainNodeType::Device:
            tm.setDeviceMacroValue(resolved.devicePath, resolved.paramIndex, clamped);
            break;
        default:
            break;
    }
}

void DefaultControllerParamWriter::writeModParam(const ResolvedTarget& resolved, float clamped) {
    // Resolve the modifier's TE parameter (rate / rateType depending on tempoSync).
    auto* param = bridge_.getPluginManager().findModifierParameterForAutomation(
        resolved.devicePath.trackId, resolved.devicePath, resolved.modId, resolved.modParamIndex);
    if (!param)
        return;

    const auto range = param->getValueRange();
    const float raw = static_cast<float>(range.getStart() + clamped * range.getLength());
    param->setParameterFromHost(raw, juce::sendNotificationSync);
}

}  // namespace magda
