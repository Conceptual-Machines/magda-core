#pragma once

#include "processors/base/AutomatablePluginProcessor.hpp"
#include "processors/base/MagdaDeviceProcessor.hpp"

namespace magda {

namespace te = tracktion;

namespace daw::audio {
class ArpeggiatorPlugin;
class MidiStrumPlugin;
class PolyStepSequencerPlugin;
class StepSequencerPlugin;
}  // namespace daw::audio

/**
 * @brief Processor for the Strum MIDI plugin.
 *
 * Exposes the strum scheduler's parameters so macros can target them, and
 * labels the discrete ones for the generic device UI.
 *
 * Strum is a MagdaDevice (#2299), so the chain holds the host's wrapper and
 * the display metadata (choices, units) comes from the device's own slots.
 */
class StrumProcessor : public MagdaDeviceProcessor {
  public:
    StrumProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the Arpeggiator plugin
 *
 * Exposes CachedValue-based parameters so macros can target them.
 * Parameters:
 * - 0: Pattern (discrete 0-5)
 * - 1: Rate (discrete 0-9)
 * - 2: Octaves (discrete 1-4)
 * - 3: Gate (0..1)
 * - 4: Swing (0..1)
 * - 5: Timing Depth (-1..1, ramp curve depth)
 * - 6: Timing Skew (-1..1, bipolar ramp curve skew)
 * - 7: Latch (0/1 boolean)
 * - 8: Velocity Mode (discrete 0-2)
 * - 9: Fixed Velocity (1-127)
 */
class ArpeggiatorProcessor : public AutomatablePluginProcessor {
  public:
    ArpeggiatorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

  protected:
    void customiseParameterInfo(int index, ParameterInfo& info) const override;
};

/**
 * @brief Processor for the Step Sequencer plugin
 *
 * Exposes CachedValue-based parameters so macros can target them.
 * Parameters:
 * - 0: Rate (discrete 0-9)
 * - 1: Direction (discrete 0-3)
 * - 2: Swing (0..1)
 * - 3: Glide Time (0..1)
 * - 4: Accent Velocity (1-127)
 * - 5: Normal Velocity (1-127)
 */
class StepSequencerProcessor : public AutomatablePluginProcessor {
  public:
    StepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

  protected:
    void customiseParameterInfo(int index, ParameterInfo& info) const override;
};

/**
 * @brief Processor for the Poly Step Sequencer plugin
 *
 * Exposes CachedValue-based parameters so macros can target them.
 * Parameters:
 * - 0: Rate (discrete 0-9)
 * - 1: Direction (discrete 0-3)
 * - 2: Swing (0..1)
 * - 3: Gate (0.05..1)
 * - 4: Timing Depth (-1..1, ramp curve depth)
 * - 5: Timing Skew (-1..1, bipolar ramp curve skew)
 */
class PolyStepSequencerProcessor : public AutomatablePluginProcessor {
  public:
    PolyStepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

  protected:
    void customiseParameterInfo(int index, ParameterInfo& info) const override;
};

/**
 * @brief Processor for the built-in Drum Grid device
 *
 * Minimal processor - the drum grid has no top-level automatable params initially.
 * Per-pad parameters live on child plugins inside DrumGridPlugin.
 */
class DrumGridProcessor : public AutomatablePluginProcessor {
  public:
    DrumGridProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

  protected:
    void customiseParameterInfo(int index, ParameterInfo& info) const override;
};

}  // namespace magda
