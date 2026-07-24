#include "processors/ParameterDisplayTextProvider.hpp"

#include "audio/AudioBridge.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "processors/base/DeviceProcessor.hpp"

namespace magda {

namespace {

juce::String formatParameterDisplayTextFromDevice(
    const ParameterInfo::DisplayTextProvider& provider, float normalizedValue) {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr)
        return {};
    auto* bridge = engine->getAudioBridge();
    if (bridge == nullptr)
        return {};

    auto path = provider.devicePath;
    if (!path.isValid() && provider.deviceId != INVALID_DEVICE_ID)
        path = TrackManager::getInstance().findDevicePath(provider.deviceId);
    if (!path.isValid())
        return {};

    auto* processor = bridge->getDeviceProcessor(path);
    if (processor == nullptr)
        return {};
    return processor->formatParameterValue(provider.paramIndex, normalizedValue);
}

}  // namespace

std::shared_ptr<ParameterInfo::DisplayTextProvider> makeDeviceParameterDisplayTextProvider(
    const ChainNodePath& devicePath, int deviceId, int paramIndex) {
    auto provider = std::make_shared<ParameterInfo::DisplayTextProvider>();
    provider->devicePath = devicePath;
    provider->deviceId = deviceId;
    provider->paramIndex = paramIndex;
    provider->formatter = formatParameterDisplayTextFromDevice;
    return provider;
}

void installDeviceParameterDisplayTextProviderFactory() {
    const bool registered =
        registerParameterDisplayTextProviderFactory(makeDeviceParameterDisplayTextProvider);
    jassert(registered);
    juce::ignoreUnused(registered);
}

}  // namespace magda
