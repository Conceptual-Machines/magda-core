#pragma once

#include <optional>

#include "slot/DeviceSlotContentPainter.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

class DeviceCustomUIManager;

struct SequencerDeviceHeaderState {
    bool available = false;
    bool recording = false;
    DeviceSlotStepRecordingPaintState stepRecording;
};

bool isSequencerDevice(const DeviceSlotTraits& traits);

SequencerDeviceHeaderState getSequencerDeviceHeaderState(const DeviceSlotTraits& traits,
                                                         const DeviceCustomUIManager& customUI);

bool randomizeSequencerPattern(const DeviceSlotTraits& traits, DeviceCustomUIManager& customUI);

std::optional<bool> toggleSequencerStepRecording(const DeviceSlotTraits& traits,
                                                 DeviceCustomUIManager& customUI);

}  // namespace magda::daw::ui
