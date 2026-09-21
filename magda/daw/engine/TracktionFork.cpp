#include "TracktionFork.hpp"

#include "../audio/AudioBridge.hpp"
#include "../core/TrackManager.hpp"
#include "TracktionEngineWrapper.hpp"

namespace magda::tracktion_fork {

namespace {

AudioBridge* renderingBridge() {
    auto* fork =
        dynamic_cast<TracktionEngineWrapper*>(TrackManager::getInstance().getAudioEngine());
    return fork != nullptr ? fork->getAudioBridge() : nullptr;
}

}  // namespace

bool isRendering() {
    return renderingBridge() != nullptr;
}

tracktion::engine::Plugin::Ptr pluginAt(const ChainNodePath& devicePath) {
    auto* bridge = renderingBridge();
    return bridge != nullptr ? bridge->getPlugin(devicePath) : nullptr;
}

tracktion::engine::AutomatableParameter* parameterFor(const ControlTarget& target) {
    auto* bridge = renderingBridge();
    return bridge != nullptr ? bridge->resolveControlTarget(target) : nullptr;
}

bool detectTransients(ClipId clipId) {
    auto* bridge = renderingBridge();
    return bridge != nullptr && bridge->getTransientTimes(clipId);
}

void setTransientSensitivity(ClipId clipId, float sensitivity) {
    if (auto* bridge = renderingBridge())
        bridge->setTransientSensitivity(clipId, sensitivity);
}

}  // namespace magda::tracktion_fork
