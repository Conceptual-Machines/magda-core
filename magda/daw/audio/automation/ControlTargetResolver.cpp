#include "automation/ControlTargetResolver.hpp"

#include "../../core/AutomationInfo.hpp"
#include "../../core/ParameterInfo.hpp"
#include "../../core/ParameterUtils.hpp"
#include "TrackController.hpp"
#include "plugin_manager/PluginManager.hpp"
#include "plugins/compiled/tracktion/CompiledFaustTracktionAdapter.hpp"
#include "plugins/tracktion/TracktionDeviceAdapters.hpp"

namespace magda {

ControlTargetResolver::ControlTargetResolver(TrackController& trackController,
                                             PluginManager& pluginManager)
    : trackController_(trackController), pluginManager_(pluginManager) {}

te::AutomatableParameter* ControlTargetResolver::resolve(const ControlTarget& target) const {
    switch (target.kind) {
        case ControlTarget::Kind::TrackVolume: {
            // The master channel is not a te::AudioTrack; its level lives on the
            // edit's master volume plugin.
            if (target.devicePath.trackId == MASTER_TRACK_ID) {
                if (auto* mvp = trackController_.getMasterVolumePlugin())
                    return mvp->volParam.get();
                return nullptr;
            }
            auto* track = trackController_.getAudioTrack(target.devicePath.trackId);
            if (!track)
                return nullptr;
            if (auto* vp = track->getVolumePlugin())
                return vp->volParam.get();
            return nullptr;
        }

        case ControlTarget::Kind::TrackPan: {
            auto* track = trackController_.getAudioTrack(target.devicePath.trackId);
            if (!track)
                return nullptr;
            if (auto* vp = track->getVolumePlugin())
                return vp->panParam.get();
            return nullptr;
        }

        case ControlTarget::Kind::SendLevel: {
            auto* track = trackController_.getAudioTrack(target.devicePath.trackId);
            if (!track)
                return nullptr;
            if (auto* auxSend = track->getAuxSendPlugin(target.sendBusIndex))
                return auxSend->gain.get();
            return nullptr;
        }

        case ControlTarget::Kind::PluginParam: {
            if (target.devicePath.getDeviceId() == INVALID_DEVICE_ID)
                return nullptr;
            auto plugin = pluginManager_.getPlugin(target.devicePath);
            if (!plugin)
                return nullptr;
            if (auto* compiledParameter = daw::audio::compiled::tracktionParameterForSlot(
                    plugin.get(), target.paramIndex))
                return compiledParameter;
            auto params = plugin->getAutomatableParameters();
            if (target.paramIndex >= 0 && target.paramIndex < static_cast<int>(params.size()))
                return params[static_cast<size_t>(target.paramIndex)];
            return nullptr;
        }

        case ControlTarget::Kind::DeviceMacro:
            return pluginManager_.findMacroParameterForAutomation(
                target.devicePath.trackId, target.devicePath, target.paramIndex);

        case ControlTarget::Kind::ModParam:
            return pluginManager_.findModifierParameterForAutomation(
                target.devicePath.trackId, target.devicePath, target.modId, target.modParamIndex);

        case ControlTarget::Kind::Tempo:
            // Edit-scoped: tempo has no te::AutomatableParameter. The BPM bridge
            // drives te::TempoSequence directly rather than through a parameter.
            return nullptr;
    }
    return nullptr;
}

double laneNormalizedFromTEValue(const ControlTarget& target, te::AutomatableParameter* param,
                                 float teValue) {
    switch (target.kind) {
        case ControlTarget::Kind::DeviceMacro:
            // Mirror of makeParameterValueConverter: macros are 0..1 on both sides.
            return juce::jlimit(0.0, 1.0, static_cast<double>(teValue));

        case ControlTarget::Kind::TrackVolume:
        case ControlTarget::Kind::SendLevel: {
            // TE fader position → dB → MAGDA 0-1 (FaderDB scale). Mirror of
            // the forward path; kept identical for TrackVolume and SendLevel.
            auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
            float dB = te::volumeFaderPositionToDB(teValue);
            return ParameterUtils::realToNormalized(dB, paramInfo);
        }
        case ControlTarget::Kind::TrackPan: {
            auto paramInfo = ParameterPresets::pan(-1, "Pan");
            return ParameterUtils::realToNormalized(teValue, paramInfo);
        }
        default: {
            // Inverse of makeParameterValueConverter — keep the two symmetric or the
            // round-trip (MAGDA normalized -> TE raw -> MAGDA normalized)
            // will drift and the UI will fight the curve.
            ParameterInfo info = getParameterInfoForTarget(target);
            // Mirror of makeParameterValueConverter: display-mapped internal params keep
            // the lane normalized == TE native, so pass through directly.
            if (ParameterUtils::isDisplayMappedInternalValue(info))
                return juce::jlimit(0.0, 1.0, static_cast<double>(teValue));
            const float teSpan = info.teMaxValue - info.teMinValue;
            if (teSpan <= 0.0f) {
                if (!param)
                    return teValue;
                auto range = param->getValueRange();
                float span = range.getEnd() - range.getStart();
                if (span <= 0.0f)
                    return 0.0;
                return juce::jlimit(0.0, 1.0,
                                    static_cast<double>((teValue - range.getStart()) / span));
            }
            return ParameterUtils::modelToNormalizedValue(ParameterModelValue{teValue}, info).value;
        }
    }
}

}  // namespace magda
