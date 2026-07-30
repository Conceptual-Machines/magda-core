#pragma once

#include <juce_core/juce_core.h>

#include "../compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

struct DeviceSlotTraits {
    bool isDrumGrid = false;
    bool isChordEngine = false;
    bool isArpeggiator = false;
    bool isStrum = false;
    bool isStepSequencer = false;
    bool isPolyStepSequencer = false;
    bool isFaust = false;            // runtime Faust effect: pool-block pages
    bool isFaustInstrument = false;  // runtime Faust instrument: author-group pages
    bool isAnalysis = false;  // oscilloscope / spectrum / levels: passthrough, no gain/macros/mods
    bool hasAnalyzerPopout = false;  // scope/spectrum pop into a floating window; levels does not
    bool isAISupported = false;
    bool isSoundDesignSupported = false;
    bool isTracktionDevice = false;
    const CompiledPresentationSpec* compiledPresentation = nullptr;
};

DeviceSlotTraits makeDeviceSlotTraits(const juce::String& pluginId);
DeviceSlotTraits makeDeviceSlotTraits(const magda::DeviceInfo& device);

}  // namespace magda::daw::ui
