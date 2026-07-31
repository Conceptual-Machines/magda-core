#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace magda::daw::audio::tracktion_adapter {

namespace te = tracktion::engine;

/**
 * Translation between a live Tracktion plugin and MAGDA's engine-neutral device
 * state document (`core/DeviceState.hpp`, schema v2).
 *
 * This is the only place that knows both shapes. Everything above it — project
 * serialization, device presets, racks, undo — moves the v2 JSON string around
 * as opaque text; everything below it is engine detail. When the native engine
 * lands, only this file is replaced.
 *
 * Capture drops the engine's own plugin-state vocabulary (object ids, enabled /
 * process / frozen flags, editor window bounds, modifier assignments, macro and
 * sidechain wiring). MAGDA already owns all of those in `DeviceInfo`, and
 * keeping them would put engine chrome back into user files. What survives is
 * the device's own property/child vocabulary plus its parameter values keyed by
 * frozen index.
 */

/// Capture a live internal device as a v2 document (JSON text). Empty when the
/// plugin has no usable state.
juce::String captureInternalDeviceState(te::Plugin& plugin);

/// Build the engine plugin tree used to construct or restore a device from
/// saved state. Accepts v2 JSON and legacy (v1) engine XML; returns an invalid
/// tree when `savedState` is empty or unusable.
juce::ValueTree devicePluginTreeFromState(const juce::String& savedState);

/// Apply a v2 document's frozen parameter values on top of an already
/// constructed plugin. No-op for legacy state, which carries no separate
/// parameter record.
void applyDeviceStateParameters(te::Plugin& plugin, const juce::String& savedState);

}  // namespace magda::daw::audio::tracktion_adapter
