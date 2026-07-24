#include "ParameterInfo.hpp"

#include "TrackManager.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/processors/base/DeviceProcessor.hpp"
#include "engine/AudioEngine.hpp"

namespace magda {

juce::String formatParameterDisplayTextFromDevice(
    const ParameterInfo::DisplayTextProvider& provider, float normalizedValue) {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (!engine)
        return {};
    auto* bridge = engine->getAudioBridge();
    if (!bridge)
        return {};

    auto path = provider.devicePath;
    if (!path.isValid() && provider.deviceId != INVALID_DEVICE_ID)
        path = TrackManager::getInstance().findDevicePath(provider.deviceId);
    if (!path.isValid())
        return {};

    auto* processor = bridge->getDeviceProcessor(path);
    if (!processor)
        return {};
    return processor->formatParameterValue(provider.paramIndex, normalizedValue);
}

}  // namespace magda
