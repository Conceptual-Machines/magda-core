#include "slot/SequencerDeviceControls.hpp"

#include "slot/DeviceCustomUIManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kMaxSequencerSteps = 32;

void applyStepRecordingState(const DeviceCustomUIManager& customUI, bool polyphonic,
                             SequencerDeviceHeaderState& state) {
    int position = 0;
    int maxSteps = kMaxSequencerSteps;
    state.available = true;
    state.recording = customUI.getSequencerStepRecordingState(polyphonic, position, maxSteps);
    if (state.recording) {
        state.stepRecording.active = true;
        state.stepRecording.position = position;
        state.stepRecording.maxSteps = juce::jlimit(1, kMaxSequencerSteps, maxSteps);
    }
}

}  // namespace

bool isSequencerDevice(const DeviceSlotTraits& traits) {
    return traits.isStepSequencer || traits.isPolyStepSequencer;
}

SequencerDeviceHeaderState getSequencerDeviceHeaderState(const DeviceSlotTraits& traits,
                                                         const DeviceCustomUIManager& customUI) {
    SequencerDeviceHeaderState state;

    if (traits.isPolyStepSequencer) {
        applyStepRecordingState(customUI, true, state);
        return state;
    }

    if (traits.isStepSequencer) {
        applyStepRecordingState(customUI, false, state);
    }

    return state;
}

bool randomizeSequencerPattern(const DeviceSlotTraits& traits, DeviceCustomUIManager& customUI) {
    return isSequencerDevice(traits) &&
           customUI.randomizeSequencerPattern(traits.isPolyStepSequencer);
}

std::optional<bool> toggleSequencerStepRecording(const DeviceSlotTraits& traits,
                                                 DeviceCustomUIManager& customUI) {
    if (!isSequencerDevice(traits))
        return std::nullopt;
    return customUI.toggleSequencerStepRecording(traits.isPolyStepSequencer);
}

}  // namespace magda::daw::ui
