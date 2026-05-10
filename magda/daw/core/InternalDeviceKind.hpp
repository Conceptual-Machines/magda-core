#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief Compile-time identifier for every internal plugin kind MAGDA
 *        knows how to dispatch on.
 *
 * Replaces the previous `device.pluginId.containsIgnoreCase("…")` chains
 * scattered through PluginManagerSync / DeviceSlotComponent /
 * DeviceCustomUIManager. Substring matching was the source of recurring
 * routing bugs — most recently `containsIgnoreCase("delay")` swallowing
 * the compiled MAGDA delay's "magda_delay" pluginId and routing it to
 * TE's DelayPlugin.
 *
 * Kinds map 1:1 onto the canonical `xmlTypeName` of each plugin (see
 * each plugin's `static const char* xmlTypeName` for the source of
 * truth). Keep this list aligned with what the engine factory in
 * MagdaEngineBehaviour can actually instantiate.
 *
 * Anything that doesn't match a known internal pluginId — VST3 / AU /
 * AAX descriptors, junk strings, future plugins — resolves to
 * `External`.
 */
enum class InternalDeviceKind {
    External,  // VST3 / AU / AAX / unrecognised
    // --- TE built-in plugins -------------------------------------------
    TeEq,
    TeCompressor,
    TeReverb,
    TeDelay,
    TeChorus,
    TePhaser,
    TeLowpass,
    TePitchShift,
    TeImpulseResponse,
    TeVolumeAndPan,
    TeFourOsc,
    TeToneGenerator,
    TeLevelMeter,
    // --- MAGDA native instrument / MIDI plugins ------------------------
    MagdaSampler,
    DrumGrid,
    MidiReceive,
    MidiChordEngine,
    Arpeggiator,
    StepSequencer,
    SidechainMonitor,
    AudioSidechainMonitor,
    InstrumentMeterTap,
    SessionMonitor,
    // --- Faust ---------------------------------------------------------
    Faust,               // interpreter-based, runs arbitrary user .dsp
    CompiledFilter,      // magda_filter
    CompiledSaturator,   // magda_saturator
    CompiledDelay,       // magda_delay
    CompiledGrainDelay,  // magda_grain_delay
    CompiledGrit,        // magda_grit
};

/**
 * @brief Resolve a `DeviceInfo::pluginId` to a strongly-typed kind.
 *
 * Uses exact case-insensitive equality against each plugin's
 * `xmlTypeName`. Single source of truth — the dispatch chains never
 * touch raw strings again.
 */
InternalDeviceKind classifyInternalDevice(const juce::String& pluginId);

}  // namespace magda
