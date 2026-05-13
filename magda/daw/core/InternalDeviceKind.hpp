#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief Compile-time identifier for non-registry internal plugin kinds
 *        MAGDA knows how to dispatch on.
 *
 * Replaces the previous `device.pluginId.containsIgnoreCase("…")` chains
 * scattered through PluginManagerSync / DeviceSlotComponent /
 * DeviceCustomUIManager. Substring matching was the source of recurring
 * routing bugs. Compiled Faust devices are intentionally excluded from
 * this enum and resolved through `CompiledPluginRegistry`.
 *
 * Kinds map 1:1 onto the canonical `xmlTypeName` of each non-registry
 * plugin (see each plugin's `static const char* xmlTypeName` for the
 * source of truth).
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
    Faust,  // interpreter-based, runs arbitrary user .dsp
};

struct InternalDeviceMetadata {
    InternalDeviceKind kind = InternalDeviceKind::External;
    const char* displayName = "";
    const char* codename = "";
    const char* category = "";
    const char* description = "";
};

/**
 * @brief Resolve a `DeviceInfo::pluginId` to a strongly-typed kind.
 *
 * Uses exact case-insensitive equality against each plugin's
 * `xmlTypeName`. Single source of truth — the dispatch chains never
 * touch raw strings again.
 */
InternalDeviceKind classifyInternalDevice(const juce::String& pluginId);

const InternalDeviceMetadata* getInternalDeviceMetadata(InternalDeviceKind kind);
const InternalDeviceMetadata* getInternalDeviceMetadataForPluginId(const juce::String& pluginId);

}  // namespace magda
