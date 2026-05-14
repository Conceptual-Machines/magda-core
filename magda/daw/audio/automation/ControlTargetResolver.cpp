#include "automation/ControlTargetResolver.hpp"

#include "TrackController.hpp"
#include "plugin_manager/PluginManager.hpp"
#include "plugins/compiled/CompiledFaustInterface.hpp"

namespace magda {

ControlTargetResolver::ControlTargetResolver(TrackController& trackController,
                                             PluginManager& pluginManager)
    : trackController_(trackController), pluginManager_(pluginManager) {}

te::AutomatableParameter* ControlTargetResolver::resolve(const ControlTarget& target) const {
    switch (target.kind) {
        case ControlTarget::Kind::TrackVolume: {
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
            DeviceId deviceId = target.devicePath.getDeviceId();
            if (deviceId == INVALID_DEVICE_ID)
                return nullptr;
            auto plugin = pluginManager_.getPlugin(deviceId);
            if (!plugin)
                return nullptr;
            if (auto* compiled =
                    dynamic_cast<daw::audio::compiled::ICompiledFaustPlugin*>(plugin.get()))
                return compiled->hostSlotParameter(target.paramIndex);
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
    }
    return nullptr;
}

}  // namespace magda
