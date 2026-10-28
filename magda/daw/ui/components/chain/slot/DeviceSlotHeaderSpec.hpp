// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "DeviceSlotTraits.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

enum class HeaderControlId {
    Macro,
    Mod,
    AI,
    Random,
    StepRecord,
    MidiThru,
    Learn,
    UI,
    Expand,
    MultiOut,
    Sidechain,
    ExportClip,
    Delta
};

enum class HeaderControlSide { Left, Right };

struct HeaderControlComponents {
    juce::Component* macroButton = nullptr;
    juce::Component* modButton = nullptr;
    juce::Component* aiButton = nullptr;
    juce::Component* learnButton = nullptr;
    juce::Component* sidechainButton = nullptr;
    juce::Component* multiOutButton = nullptr;
    juce::Component* uiButton = nullptr;
    juce::Component* expandButton = nullptr;
    juce::Component* exportClipButton = nullptr;
    juce::Component* randomButton = nullptr;
    juce::Component* stepRecordButton = nullptr;
    juce::Component* midiThruButton = nullptr;
    juce::Component* deltaButton = nullptr;
};

struct HeaderControlVisibility {
    bool macro = false;
    bool mod = false;
    bool ai = false;
    bool random = false;
    bool stepRecord = false;
    bool midiThru = false;
    bool learn = false;
    bool ui = false;
    bool expand = false;
    bool multiOut = false;
    bool sidechain = false;
    bool exportClip = false;
    bool delta = false;
    bool power = true;
    bool preset = true;
};

struct HeaderControlSpec {
    HeaderControlId id;
    HeaderControlSide side;
    juce::Component* component = nullptr;
    int expandedOrder = 0;
    int collapsedOrder = 0;
    bool expandedVisible = false;
    bool collapsedVisible = false;
};

bool isMidiUtilityDeviceSlot(const DeviceSlotTraits& traits);

HeaderControlVisibility getHeaderControlVisibility(const DeviceSlotTraits& traits,
                                                   const magda::DeviceInfo& device,
                                                   bool isInternalDevice);

std::vector<HeaderControlSpec> buildHeaderControlSpecs(const DeviceSlotTraits& traits,
                                                       const magda::DeviceInfo& device,
                                                       bool isInternalDevice,
                                                       HeaderControlComponents controls);

}  // namespace magda::daw::ui
