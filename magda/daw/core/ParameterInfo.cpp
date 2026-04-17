#include "ParameterInfo.hpp"

#include "TrackManager.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/DeviceProcessor.hpp"
#include "engine/AudioEngine.hpp"

namespace magda {

juce::String ParameterInfo::DisplayTextProvider::format(float normalizedValue) const {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (!engine)
        return {};
    auto* bridge = engine->getAudioBridge();
    if (!bridge)
        return {};
    auto* processor = bridge->getDeviceProcessor(deviceId);
    if (!processor)
        return {};
    return processor->formatParameterValue(paramIndex, normalizedValue);
}

}  // namespace magda
