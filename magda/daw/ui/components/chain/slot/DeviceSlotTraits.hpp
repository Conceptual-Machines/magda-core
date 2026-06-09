#pragma once

#include <juce_core/juce_core.h>

#include "compiled/CompiledPluginPresentation.hpp"

namespace magda::daw::ui {

struct DeviceSlotTraits {
    bool isDrumGrid = false;
    bool isChordEngine = false;
    bool isArpeggiator = false;
    bool isStepSequencer = false;
    bool isFaust = false;
    bool isAnalysis = false;  // oscilloscope / spectrum / levels: passthrough, no gain/macros/mods
    bool hasAnalyzerPopout = false;  // scope/spectrum pop into a floating window; levels does not
    bool isAISupported = false;
    bool isSoundDesignSupported = false;
    bool isTracktionDevice = false;
    const CompiledPresentationSpec* compiledPresentation = nullptr;
};

DeviceSlotTraits makeDeviceSlotTraits(const juce::String& pluginId);

}  // namespace magda::daw::ui
