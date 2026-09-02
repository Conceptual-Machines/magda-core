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
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

// =============================================================================
// StrumProcessor
// =============================================================================

StrumProcessor::StrumProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

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

    // The id the pad's parameter was registered under, carried through so a
    // consumer can find a pad's level or pan by name. The plan compiler binds a
    // pad's fader with it (#2200), which is what lets a lane or a macro over the
    // pad's level reach the fader rather than stopping at the device.
    const auto params = getAutomatableParameters();
    if (index >= 0 && index < params.size() && params[index] != nullptr)
        info.stableId = params[index]->paramID;
}

}  // namespace magda
