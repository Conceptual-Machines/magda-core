#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "core/LegacyDeviceAliases.hpp"

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
///
/// `existingState` is what the device currently has saved. When that came from a
/// schema this build cannot read, it is returned UNCHANGED: `decode` refused it,
/// so the device is sitting at its defaults, and writing a freshly captured v2
/// document over it would permanently downgrade a project written by a newer
/// build just because it was opened here. The parameter is mandatory so a
/// capture site cannot silently skip that check.
juce::String captureInternalDeviceState(te::Plugin& plugin, const juce::String& existingState);

/// Build the engine plugin tree used to construct or restore a device from
/// saved state. Accepts v2 JSON and legacy (v1) engine XML; returns an invalid
/// tree when `savedState` is empty or unusable.
juce::ValueTree devicePluginTreeFromState(const juce::String& savedState);

/// Apply a v2 document's frozen parameter values on top of an already
/// constructed plugin. No-op for legacy state, which carries no separate
/// parameter record.
void applyDeviceStateParameters(te::Plugin& plugin, const juce::String& savedState);

/**
 * @brief Rewrite a nested retired-device plugin tree onto its compiled
 *        successor, before the engine is asked to build it.
 *
 * Anything MAGDA models as a `DeviceInfo` is converted on the model at project
 * load (`core/LegacyDeviceAliases.hpp`). A device nested inside another one — an
 * FX chain on a Drum Grid pad — has no `DeviceInfo`: the owning device restores
 * it straight from the saved plugin tree, with its values still in the engine's
 * own property vocabulary.
 *
 * This has to run BEFORE construction. Tracktion registers every one of the
 * retired effects as a built-in type and checks that list ahead of
 * `EngineBehaviour::createCustomPlugin`, so a nested tree still naming one would
 * otherwise be built as the Tracktion plugin it always was, and MAGDA's load
 * aliases would never be consulted.
 *
 * The tree's `type` adopts the successor's id and the converted properties are
 * consumed, so the nested tree stops being legacy the next time its owner is
 * saved. Returns the converted values for `applyRetiredSlotValues` to seat once
 * the plugin exists; empty (and the tree untouched) for any live device.
 */
std::vector<magda::legacy_devices::RetiredSlotValue> adoptRetiredNestedPluginTree(
    const juce::ValueTree& state);

/// Seat the values returned by `adoptRetiredNestedPluginTree` on the plugin
/// built from that tree, whether the successor is a compiled Faust device or a
/// native one. No-op for an empty list.
void applyRetiredSlotValues(te::Plugin& plugin,
                            const std::vector<magda::legacy_devices::RetiredSlotValue>& values);

}  // namespace magda::daw::audio::tracktion_adapter
