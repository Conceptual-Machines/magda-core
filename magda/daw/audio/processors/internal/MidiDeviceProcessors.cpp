#include "processors/internal/MidiDeviceProcessors.hpp"

#include <utility>

#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/MidiStrumPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"

namespace magda {

// =============================================================================
// ArpeggiatorProcessor
// =============================================================================

ArpeggiatorProcessor::ArpeggiatorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

void ArpeggiatorProcessor::customiseParameterInfo(int index, ParameterInfo& info) const {
    // Depth (5) and skew (6) default to bipolar; all others unipolar
    info.bipolarModulation = (index == 5 || index == 6);
}

// =============================================================================
// StrumProcessor
// =============================================================================

StrumProcessor::StrumProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

void StrumProcessor::customiseParameterInfo(int index, ParameterInfo& info) const {
    switch (index) {
        case 0:  // Trigger
            info.scale = ParameterScale::Discrete;
            info.choices = {"Chord", "Sync"};
            break;
        case 1:  // Order
            info.scale = ParameterScale::Discrete;
            info.choices = {"Up", "Down", "Up-Down", "As Played"};
            break;
        case 2:  // Shape
            info.scale = ParameterScale::Discrete;
            info.choices = {"Linear", "Ease In", "Ease Out",  "Snap",
                            "Spike",  "S-Curve", "Overshoot", "Bounce"};
            break;
        case 3:  // Cycles
            info.scale = ParameterScale::Discrete;
            info.choices = {"1", "2", "3", "4", "5", "6", "7", "8"};
            break;
        case 4:  // Strum Length
        case 5:  // Sync Interval
            info.unit = "ms";
            break;
        default:
            break;
    }
}

// =============================================================================
// StepSequencerProcessor
// =============================================================================

StepSequencerProcessor::StepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

void StepSequencerProcessor::customiseParameterInfo(int index, ParameterInfo& info) const {
    // Timing Depth (6) and Timing Skew (7) are bipolar
    info.bipolarModulation = (index == 6 || index == 7);
}

// =============================================================================
// PolyStepSequencerProcessor
// =============================================================================

PolyStepSequencerProcessor::PolyStepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

void PolyStepSequencerProcessor::customiseParameterInfo(int index, ParameterInfo& info) const {
    // Timing Depth (4) and Timing Skew (5) are bipolar
    info.bipolarModulation = (index == 4 || index == 5);
}

// =============================================================================
// DrumGridProcessor
// =============================================================================

DrumGridProcessor::DrumGridProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

void DrumGridProcessor::customiseParameterInfo(int index, ParameterInfo& info) const {
    // Pan params (odd indices) are bipolar
    info.bipolarModulation = (index % 2 == 1);
}

}  // namespace magda
