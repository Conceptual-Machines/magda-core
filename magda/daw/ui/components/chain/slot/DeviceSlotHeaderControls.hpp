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
    juce::Component* deltaButton = nullptr;
    juce::Component* powerButton = nullptr;
    juce::Component* presetButton = nullptr;
    juce::Component* exportClipButton = nullptr;
    juce::Component* randomButton = nullptr;      // step-sequencer pattern randomize
    juce::Component* stepRecordButton = nullptr;  // step-sequencer step record toggle
    juce::Component* midiThruButton = nullptr;    // MIDI source/thru toggle
};

struct DeviceSlotCollapsedControls {
    juce::Component* levelMeter = nullptr;
    juce::Component* midiNoteStrip = nullptr;
    DeviceSlotHeaderControls headerControls;
};

struct DeviceSlotHeaderMetrics {
    int buttonWidth = 24;
    int buttonHeight = 20;
    int gap = 4;
    int separatorMargin = 6;
    int separatorHeight = 14;
};

/// Where the expanded header's two separators landed; empty when not drawn.
struct DeviceSlotHeaderSeparators {
    juce::Rectangle<int> left, right;
};

/// Sidechain, multi-out, MIDI thru and delta keep their visibility here but are
/// placed by the footer and side strip, not the expanded header.
DeviceSlotHeaderSeparators layoutExpandedDeviceSlotHeader(juce::Rectangle<int>& headerArea,
                                                          const DeviceSlotTraits& traits,
                                                          const magda::DeviceInfo& device,
                                                          bool isInternalDevice,
                                                          DeviceSlotHeaderControls controls,
                                                          const DeviceSlotHeaderMetrics& metrics);

void layoutCollapsedDeviceSlotControls(juce::Rectangle<int>& area,
                                       juce::Rectangle<int> collapsedMeterArea,
                                       const DeviceSlotTraits& traits,
                                       const magda::DeviceInfo& device, bool isInternalDevice,
                                       DeviceSlotCollapsedControls controls, int maxButtonSize);

void applyMidiOnlyDeviceHeaderVisibility(const DeviceSlotTraits& traits,
                                         const magda::DeviceInfo& device,
                                         juce::Component* modButton, juce::Component* macroButton);

}  // namespace magda::daw::ui
