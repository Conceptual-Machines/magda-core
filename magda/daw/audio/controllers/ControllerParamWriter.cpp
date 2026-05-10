#include "controllers/ControllerParamWriter.hpp"

#include <algorithm>
#include <cmath>

#include "../../core/AutomationInfo.hpp"
#include "../../core/ModInfo.hpp"
#include "../../core/ParameterUtils.hpp"
#include "../../core/TrackManager.hpp"
#include "AudioBridge.hpp"
#include "plugin_manager/PluginManager.hpp"

namespace magda {

namespace {

const ParameterInfo* findDeviceParameterInfo(const DeviceInfo& device, int paramIndex) {
    auto it = std::find_if(
        device.parameters.begin(), device.parameters.end(),
        [paramIndex](const ParameterInfo& param) { return param.paramIndex == paramIndex; });
    if (it != device.parameters.end())
        return &*it;

    if (paramIndex >= 0 && paramIndex < static_cast<int>(device.parameters.size()))
        return &device.parameters[static_cast<size_t>(paramIndex)];

    return nullptr;
}

}  // namespace

void DefaultControllerParamWriter::write(const ResolveResult& resolved, float value) {
    if (!resolved.ok())
        return;

    const float clamped = juce::jlimit(0.0f, 1.0f, value);

    switch (resolved.target.kind) {
        case ControlTarget::Kind::PluginParam:
            writePluginParam(resolved.target, clamped);
            break;
        case ControlTarget::Kind::DeviceMacro:
            writeMacro(resolved.target, clamped);
            break;
        case ControlTarget::Kind::ModParam:
            writeModParam(resolved.target, clamped);
            break;
        case ControlTarget::Kind::TrackVolume:
        case ControlTarget::Kind::TrackPan:
        case ControlTarget::Kind::SendLevel:
            break;
    }
}

void DefaultControllerParamWriter::writePluginParam(const ControlTarget& target, float clamped) {
    auto& trackMgr = TrackManager::getInstance();
    if (auto* device = trackMgr.getDeviceInChainByPath(target.devicePath)) {
        const auto* info = findDeviceParameterInfo(*device, target.paramIndex);
        const bool displayMapped = info != nullptr && device->format == PluginFormat::Internal &&
                                   ParameterUtils::isDisplayMappedInternalValue(*info);
        if (displayMapped) {
            const auto displayValue = ParameterUtils::normalizedToModelValue(
                ParameterNormalizedValue::clamped(clamped), *info);
            trackMgr.setDeviceParameterValue(target.devicePath, target.paramIndex, displayValue);
            return;
        }
    }

    auto* param = bridge_.resolveControlTarget(target);
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
    TrackManager::getInstance().setDeviceParameterValueFromPlugin(target.devicePath,
                                                                  target.paramIndex, raw);
}

void DefaultControllerParamWriter::writeMacro(const ControlTarget& target, float clamped) {
    if (!target.devicePath.isValid())
        return;
    // setMacroValue takes the macro's owning ChainNodePath unchanged for every
    // valid scope (Track/Rack/Device). The previous per-getType() switch
    // dispatched to the same call in each arm.
    TrackManager::getInstance().setMacroValue(target.devicePath, target.paramIndex, clamped);
}

void DefaultControllerParamWriter::writeModParam(const ControlTarget& target, float clamped) {
    // Mirror the path used by AutomationPlaybackEngine::writeModRateFromCurve:
    // build an AutomationTarget for the rate, take its perceptual ParameterInfo
    // (logarithmic Hz or discrete sync division depending on the modifier's
    // tempoSync flag), map normalized → real through ParameterUtils, and write
    // via TrackManager so MAGDA state, the slider UI, and the live TE param
    // all stay in sync. A linear interp on TE's raw Hz range crammed every
    // audible rate change into the bottom 5% of the slider.
    ParameterInfo info = getParameterInfoForTarget(target);
    auto& trackMgr = TrackManager::getInstance();

    auto* track = trackMgr.getTrack(target.devicePath.trackId);
    const ModInfo* mod = nullptr;
    if (track) {
        if (target.devicePath.isValid()) {
            auto resolvedPath = trackMgr.resolvePath(target.devicePath);
            if (resolvedPath.valid && resolvedPath.rack) {
                for (const auto& m : resolvedPath.rack->mods)
                    if (m.id == target.modId) {
                        mod = &m;
                        break;
                    }
            } else if (resolvedPath.valid && resolvedPath.device) {
                for (const auto& m : resolvedPath.device->mods)
                    if (m.id == target.modId) {
                        mod = &m;
                        break;
                    }
            }
        }
        if (!mod) {
            for (const auto& m : track->mods)
                if (m.id == target.modId) {
                    mod = &m;
                    break;
                }
        }
    }
    const bool sync = mod && mod->tempoSync;

    // setModRate / setModSyncDivision accept the modifier's owning path
    // directly for Rack/TopLevelDevice/Device scopes; track-level modifiers
    // need the trackLevel path. Pick once instead of dispatching on getType().
    auto type = target.devicePath.getType();
    auto pathForWrite = (type == ChainNodeType::Rack || type == ChainNodeType::TopLevelDevice ||
                         type == ChainNodeType::Device)
                            ? target.devicePath
                            : ChainNodePath::trackLevel(target.devicePath.trackId);

    float real = ParameterUtils::normalizedToReal(clamped, info);
    if (sync) {
        int ordinal = juce::jlimit(1, 23, static_cast<int>(std::round(real)) + 1);
        SyncDivision division = teRateOrdinalToSyncDivision(ordinal);
        trackMgr.setModSyncDivision(pathForWrite, target.modId, division);
        return;
    }
    trackMgr.setModRate(pathForWrite, target.modId, real);
}

}  // namespace magda
