#include "processors/internal/MidiDeviceProcessors.hpp"

#include <utility>

#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
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
// StepSequencerProcessor
// =============================================================================

StepSequencerProcessor::StepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

void StepSequencerProcessor::customiseParameterInfo(int index, ParameterInfo& info) const {
    // Timing Depth (6) and Timing Skew (7) are bipolar
    info.bipolarModulation = (index == 6 || index == 7);
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
