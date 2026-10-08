#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

struct DeviceSlotStepRecordingPaintState {
    bool active = false;
    int position = 0;
    int maxSteps = 1;
};

struct DeviceSlotContentPaintState {
    const DeviceSlotTraits& traits;
    magda::DeviceLoadState loadState = magda::DeviceLoadState::Loaded;
    bool collapsed = false;
    bool bypassed = false;
    bool internalDevice = false;
    bool hasCustomUI = false;
    juce::String manufacturer;
    juce::String deviceName;
    DeviceSlotStepRecordingPaintState stepRecording;
};

void paintDeviceSlotContent(juce::Graphics& g, juce::Rectangle<int> contentArea,
                            const DeviceSlotContentPaintState& state, int meterStripWidth,
                            int paginationHeight, int faustHeaderHeight);

/** @brief What the device header shows after the name, and in what colour. */
struct DeviceSlotSubtitle {
    juce::String text;
    juce::Colour colour;
};

DeviceSlotSubtitle deviceSlotSubtitle(const DeviceSlotContentPaintState& state);

}  // namespace magda::daw::ui
