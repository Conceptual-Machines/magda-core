#include "DeviceParameterDisplayTextProvider.hpp"

#include "core/ParameterInfo.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"

namespace magda {

namespace {

juce::String formatParameterDisplayTextFromDevice(
    const ParameterInfo::DisplayTextProvider& provider, float normalizedValue) {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr)
        return {};

    auto path = provider.devicePath;
    if (!path.isValid() && provider.deviceId != INVALID_DEVICE_ID)
        path = TrackManager::getInstance().findDevicePath(provider.deviceId);
    if (!path.isValid())
        return {};

    // The engine, not the fork's bridge: whichever one renders the device is
    // the one holding the plugin that can name the value, and under the native
    // engine there is no bridge at all (#2600).
    return engine->formatDeviceParameter(path, provider.paramIndex, normalizedValue);
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

void installDeviceParameterDisplayTextProviderFactory() {
    const bool registered =
        registerParameterDisplayTextProviderFactory(makeDeviceParameterDisplayTextProvider);
    jassert(registered);
    juce::ignoreUnused(registered);
}

}  // namespace magda
