#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

struct DeviceSlotHeaderControls {
    juce::Component* gainLabel = nullptr;
    juce::Component* macroButton = nullptr;
    juce::Component* modButton = nullptr;
    juce::Component* aiButton = nullptr;
    juce::Component* learnButton = nullptr;
    juce::Component* sidechainButton = nullptr;
    juce::Component* multiOutButton = nullptr;
    juce::Component* uiButton = nullptr;
    juce::Component* powerButton = nullptr;
    juce::Component* presetButton = nullptr;
    juce::Component* exportClipButton = nullptr;
};

void layoutExpandedDeviceSlotHeader(juce::Rectangle<int>& headerArea,
                                    const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                    bool isInternalDevice, DeviceSlotHeaderControls controls,
                                    int buttonSize);

}  // namespace magda::daw::ui
