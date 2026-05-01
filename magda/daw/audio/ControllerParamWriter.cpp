#include "ControllerParamWriter.hpp"

#include "../core/AutomationInfo.hpp"
#include "../core/ModInfo.hpp"
#include "../core/ParameterUtils.hpp"
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
            tm.setMacroValue(ChainNodePath::trackLevel(resolved.devicePath.trackId), resolved.paramIndex, clamped);
            break;
        case ChainNodeType::Rack:
            tm.setMacroValue(resolved.devicePath, resolved.paramIndex, clamped);
            break;
        case ChainNodeType::TopLevelDevice:
        case ChainNodeType::Device:
            tm.setMacroValue(resolved.devicePath, resolved.paramIndex, clamped);
            break;
        default:
            break;
    }
}

void DefaultControllerParamWriter::writeModParam(const ResolvedTarget& resolved, float clamped) {
    // Mirror the path used by AutomationPlaybackEngine::writeModRateFromCurve:
    // build an AutomationTarget for the rate, take its perceptual ParameterInfo
    // (logarithmic Hz or discrete sync division depending on the modifier's
    // tempoSync flag), map normalized → real through ParameterUtils, and write
    // via TrackManager so MAGDA state, the slider UI, and the live TE param
    // all stay in sync. A linear interp on TE's raw Hz range crammed every
    // audible rate change into the bottom 5% of the slider.
    AutomationTarget t;
    t.type = AutomationTargetType::ModParameter;
    t.trackId = resolved.devicePath.trackId;
    t.devicePath = resolved.devicePath;
    t.modId = resolved.modId;
    t.modParamIndex = resolved.modParamIndex;

    ParameterInfo info = t.getParameterInfo();
    auto& trackMgr = TrackManager::getInstance();

    auto* track = trackMgr.getTrack(t.trackId);
    const ModInfo* mod = nullptr;
    if (track) {
        if (t.devicePath.isValid()) {
            auto resolvedPath = trackMgr.resolvePath(t.devicePath);
            if (resolvedPath.valid && resolvedPath.rack) {
                for (const auto& m : resolvedPath.rack->mods)
                    if (m.id == t.modId) {
                        mod = &m;
                        break;
                    }
            } else if (resolvedPath.valid && resolvedPath.device) {
                for (const auto& m : resolvedPath.device->mods)
                    if (m.id == t.modId) {
                        mod = &m;
                        break;
                    }
            }
        }
        if (!mod) {
            for (const auto& m : track->mods)
                if (m.id == t.modId) {
                    mod = &m;
                    break;
                }
        }
    }
    const bool sync = mod && mod->tempoSync;

    if (sync) {
        float real = ParameterUtils::normalizedToReal(clamped, info);
        int ordinal = juce::jlimit(1, 23, static_cast<int>(std::round(real)) + 1);
        SyncDivision division = teRateOrdinalToSyncDivision(ordinal);
        if (t.devicePath.isValid()) {
            switch (t.devicePath.getType()) {
                case ChainNodeType::Rack:
                    trackMgr.setModSyncDivision(t.devicePath, t.modId, division);
                    return;
                case ChainNodeType::TopLevelDevice:
                case ChainNodeType::Device:
                    trackMgr.setModSyncDivision(t.devicePath, t.modId, division);
                    return;
                default:
                    break;
            }
        }
        trackMgr.setModSyncDivision(ChainNodePath::trackLevel(t.trackId), t.modId, division);
        return;
    }

    float real = ParameterUtils::normalizedToReal(clamped, info);
    if (t.devicePath.isValid()) {
        switch (t.devicePath.getType()) {
            case ChainNodeType::Rack:
                trackMgr.setModRate(t.devicePath, t.modId, real);
                return;
            case ChainNodeType::TopLevelDevice:
            case ChainNodeType::Device:
                trackMgr.setModRate(t.devicePath, t.modId, real);
                return;
            default:
                break;
        }
    }
    trackMgr.setModRate(ChainNodePath::trackLevel(t.trackId), t.modId, real);
}

}  // namespace magda
