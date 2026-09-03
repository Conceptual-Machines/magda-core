#pragma once

#include <juce_core/juce_core.h>

#include "DeviceState.hpp"
#include "audio/sequencer/StepPattern.hpp"

namespace magda::step_pattern {

/**
 * @brief The step sequencers' patterns inside a device state document.
 *
 * The model owns a step pattern, the way it owns MIDI notes: it is authored
 * state, so it lives in `DeviceInfo::pluginState` (the v2 document, #1887) and
 * the engine device is a projection of it (#2313). This is the adapter between
 * the two - the sequencing core's plain pattern types on one side, the
 * persisted `STEP` / `NOTE` element names on the other.
 *
 * The element and property names are the retired Tracktion plugins': saved
 * projects carry them, so they are a frozen persistence surface. The device
 * reads exactly the same names out of the tree it is restored from, which is
 * what keeps "what the model holds" and "what the device plays" one thing.
 */

using MonoPattern = daw::audio::sequencer::MonoPattern;
using PolyPattern = daw::audio::sequencer::PolyPattern;

/// Read the monophonic sequencer's pattern out of a decoded document.
MonoPattern readMono(const device_state::Doc& doc);

/// Replace the monophonic pattern in @p doc, leaving its other state alone.
void writeMono(device_state::Doc& doc, const MonoPattern& pattern);

/// Read the polyphonic sequencer's pattern out of a decoded document.
PolyPattern readPoly(const device_state::Doc& doc);

/// Replace the polyphonic pattern in @p doc, leaving its other state alone.
void writePoly(device_state::Doc& doc, const PolyPattern& pattern);

/// The pattern a device's saved state string carries, or an empty default when
/// the string is empty, legacy, or from a schema this build cannot read.
MonoPattern monoPatternOf(const juce::String& deviceStateText);
PolyPattern polyPatternOf(const juce::String& deviceStateText);

}  // namespace magda::step_pattern
