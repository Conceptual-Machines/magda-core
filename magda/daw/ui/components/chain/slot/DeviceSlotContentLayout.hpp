#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

struct DeviceSlotContentFrameControls {
    juce::Component* pluginPresetsButton = nullptr;
    juce::Component* levelMeter = nullptr;
    juce::Component* midiNoteStrip = nullptr;
    juce::Component* gainSlider = nullptr;
    juce::Component* paramGrid = nullptr;
    juce::Component* gainLabel = nullptr;
    juce::Component* magdaPresetButton = nullptr;
    juce::Component* activeCustomUI = nullptr;
    juce::Component* compiledPanel = nullptr;
    juce::Component* modButton = nullptr;
    juce::Component* macroButton = nullptr;
    juce::Component* uiButton = nullptr;
    juce::Component* powerButton = nullptr;
};

bool prepareDeviceSlotContentFrame(juce::Rectangle<int>& contentArea,
                                   const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                   bool collapsed, bool internalDevice, bool pluginPresetsAvailable,
                                   DeviceSlotContentFrameControls controls, int meterStripWidth,
                                   int contentHeaderHeight);

}  // namespace magda::daw::ui
