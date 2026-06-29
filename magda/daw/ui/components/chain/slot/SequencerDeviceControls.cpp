#include "slot/SequencerDeviceControls.hpp"

#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "slot/DeviceCustomUIManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kMaxSequencerSteps = 32;

}  // namespace

bool isSequencerDevice(const DeviceSlotTraits& traits) {
    return traits.isStepSequencer || traits.isPolyStepSequencer;
}

SequencerDeviceHeaderState getSequencerDeviceHeaderState(const DeviceSlotTraits& traits,
                                                         const DeviceCustomUIManager& customUI) {
    SequencerDeviceHeaderState state;

    if (traits.isPolyStepSequencer) {
        auto* plugin = customUI.getPolyStepSeqPlugin();
        if (plugin == nullptr)
            return state;

        state.available = true;
        state.recording = plugin->isStepRecording();
        if (state.recording) {
            state.stepRecording.active = true;
            state.stepRecording.position =
                plugin->stepRecordPosition_.load(std::memory_order_relaxed);
            state.stepRecording.maxSteps =
                juce::jlimit(1, kMaxSequencerSteps, plugin->numSteps.get());
        }
        return state;
    }

    if (traits.isStepSequencer) {
        auto* plugin = customUI.getStepSeqPlugin();
        if (plugin == nullptr)
            return state;

        state.available = true;
        state.recording = plugin->isStepRecording();
        if (state.recording) {
            state.stepRecording.active = true;
            state.stepRecording.position =
                plugin->stepRecordPosition_.load(std::memory_order_relaxed);
            state.stepRecording.maxSteps =
                juce::jlimit(1, kMaxSequencerSteps, plugin->numSteps.get());
        }
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
