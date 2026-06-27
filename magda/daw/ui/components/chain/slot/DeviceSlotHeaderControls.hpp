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
    juce::Component* randomButton = nullptr;        // step-sequencer pattern randomize
    juce::Component* stepRecordButton = nullptr;    // step-sequencer step record toggle
    juce::Component* midiThruButton = nullptr;      // step-sequencer MIDI thru toggle
    juce::Component* instMidiThruButton = nullptr;  // MIDI source/thru toggle
};

struct DeviceSlotCollapsedControls {
    juce::Component* levelMeter = nullptr;
    juce::Component* midiNoteStrip = nullptr;
    DeviceSlotHeaderControls headerControls;
};

void layoutExpandedDeviceSlotHeader(juce::Rectangle<int>& headerArea,
                                    const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                    bool isInternalDevice, DeviceSlotHeaderControls controls,
                                    int buttonSize);

void layoutCollapsedDeviceSlotControls(juce::Rectangle<int>& area,
                                       juce::Rectangle<int> collapsedMeterArea,
                                       const DeviceSlotTraits& traits,
                                       const magda::DeviceInfo& device, bool isInternalDevice,
                                       DeviceSlotCollapsedControls controls, int maxButtonSize);

void applyMidiOnlyDeviceHeaderVisibility(const DeviceSlotTraits& traits,
                                         const magda::DeviceInfo& device,
                                         juce::Component* modButton, juce::Component* macroButton);

}  // namespace magda::daw::ui
