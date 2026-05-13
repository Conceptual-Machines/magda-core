#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

class DrumGridUI;
class ParamHostComponent;

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

struct DeviceSlotContentBodyControls {
    juce::Component* faustHeader = nullptr;
    juce::Component* faustCustomView = nullptr;
    int faustCustomViewPreferredHeight = 0;
    juce::Component* compiledPanel = nullptr;
    int compiledPanelPreferredHeight = 0;
    DrumGridUI* drumGridUI = nullptr;
    juce::Component* activeCustomUI = nullptr;
    ParamHostComponent* paramGrid = nullptr;
};

bool prepareDeviceSlotContentFrame(juce::Rectangle<int>& contentArea,
                                   const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                   bool collapsed, bool internalDevice, bool pluginPresetsAvailable,
                                   DeviceSlotContentFrameControls controls, int meterStripWidth,
                                   int contentHeaderHeight);

void layoutDeviceSlotContentBody(juce::Rectangle<int> contentArea, const DeviceSlotTraits& traits,
                                 bool internalDevice, bool hasCustomUI,
                                 DeviceSlotContentBodyControls controls, int faustHeaderHeight);

}  // namespace magda::daw::ui
