#include "DeviceParameterDisplayTextProvider.hpp"

#include "core/DeviceInfo.hpp"
#include "core/ParameterInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda {

namespace {

DeviceParameterFormatter& deviceParameterFormatter() {
    static DeviceParameterFormatter formatter;
    return formatter;
}

juce::String formatParameterDisplayTextFromDevice(
    const ParameterInfo::DisplayTextProvider& provider, float normalizedValue) {
    const auto& formatter = deviceParameterFormatter();
    if (!formatter)
        return {};

    auto path = provider.devicePath;
    if (!path.isValid() && provider.deviceId != INVALID_DEVICE_ID)
        path = TrackManager::getInstance().findDevicePath(provider.deviceId);
    if (!path.isValid())
        return {};

    return formatter(path, provider.paramIndex, normalizedValue);
}

std::shared_ptr<ParameterInfo::DisplayTextProvider> makeDeviceParameterDisplayTextProvider(
    const ChainNodePath& devicePath, int deviceId, int paramIndex) {
    auto provider = std::make_shared<ParameterInfo::DisplayTextProvider>();
    provider->devicePath = devicePath;
    provider->deviceId = deviceId;
    provider->paramIndex = paramIndex;
    provider->formatter = formatParameterDisplayTextFromDevice;
    return provider;
}

}  // namespace

void setDeviceParameterFormatter(DeviceParameterFormatter formatter) {
    deviceParameterFormatter() = std::move(formatter);
}

void forgetDeviceParameterFormatter() {
    deviceParameterFormatter() = nullptr;
}

void attachParameterTextProviders(DeviceInfo& device, const ChainNodePath& devicePath) {
    if (!devicePath.isValid())
        return;

    for (auto& parameter : device.parameters)
        parameter.displayText =
            makeParameterDisplayTextProvider(devicePath, device.id, parameter.paramIndex);
}

void installDeviceParameterDisplayTextProviderFactory() {
    const bool registered =
        registerParameterDisplayTextProviderFactory(makeDeviceParameterDisplayTextProvider);
    jassert(registered);
    juce::ignoreUnused(registered);
}

}  // namespace magda
