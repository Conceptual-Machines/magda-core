#include "DeviceParameterList.hpp"

#include "DeviceParameterDisplayTextProvider.hpp"
#include "core/DeviceInfo.hpp"
#include "core/PluginParameterConfigStore.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "plugin_manager/ExternalPluginState.hpp"

namespace magda {

namespace {

/** @brief What the engine's instance at @p devicePath reports. */
HostParameters reportedAt(const ChainNodePath& devicePath) {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    return engine != nullptr ? engine->describeDeviceParameters(devicePath) : HostParameters{};
}

/**
 * @brief @p reported in a device, shaped for the config store and the providers.
 *
 * Both work on a DeviceInfo, and the fields they read are the plugin's identity
 * and its lists.
 */
DeviceInfo asDevice(const DeviceInfo& device, HostParameters&& reported) {
    DeviceInfo described;
    described.id = device.id;
    described.uniqueId = device.uniqueId;
    described.pluginId = device.pluginId;
    described.format = device.format;
    described.parameters = std::move(reported.parameters);
    described.wrapperParameters = std::move(reported.wrapperParameters);
    return described;
}

}  // namespace

std::vector<ParameterInfo> deviceParameterList(const DeviceInfo& device,
                                               const ChainNodePath& devicePath) {
    if (device.format == PluginFormat::Internal || !devicePath.isValid())
        return device.parameters;

    auto reported = reportedAt(devicePath);
    if (reported.parameters.empty())
        return device.parameters;

    auto described = asDevice(device, std::move(reported));
    PluginParameterConfigStore::applyToDevice(described);
    attachParameterTextProviders(described, devicePath);
    return std::move(described.parameters);
}

}  // namespace magda
