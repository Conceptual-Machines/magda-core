#include "ParameterInfo.hpp"

#include "TrackManager.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/processors/base/DeviceProcessor.hpp"
#include "engine/AudioEngine.hpp"

namespace magda {

juce::String ParameterInfo::DisplayTextProvider::format(float normalizedValue) const {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (!engine)
        return {};
    auto* bridge = engine->getAudioBridge();
    if (!bridge)
        return {};

    auto path = devicePath;
    if (!path.isValid() && deviceId != INVALID_DEVICE_ID)
        path = TrackManager::getInstance().findDevicePath(deviceId);
    if (!path.isValid())
        return {};

    auto* processor = bridge->getDeviceProcessor(path);
    if (!processor)
        return {};
    return processor->formatParameterValue(paramIndex, normalizedValue);
}

}  // namespace magda
