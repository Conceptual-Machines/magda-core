#include "InternalPluginAliases.hpp"

namespace magda {

namespace {

// Compact authoring shape — one entry per (alias name, paramIndex,
// drift-fallback display name). Translated to StoredAlias entries below.
struct AliasSpec {
    const char* alias;
    int paramIndex;
    const char* paramName;  // Used as paramNameAtSetTime for drift recovery.
};

struct PluginSpec {
    const char* pluginKey;     // "eq", "compressor", ...
    const AliasSpec* aliases;  // Null-terminated by zero-init sentinel
    int aliasCount;
};

// ------------------------------------------------------------------
// Equaliser ("eq") — 12 EQ band params + 1 phase-invert.
// Matches EqualiserProcessor::populateParameters parameter ordering.
// ------------------------------------------------------------------
constexpr AliasSpec kEqAliases[] = {
    {"low_shelf_freq", 0, "Low-shelf freq"},
    {"low_shelf_gain", 1, "Low-shelf gain"},
    {"low_shelf_q", 2, "Low-shelf Q"},
    {"mid_freq_1", 3, "Mid freq 1"},
    {"mid_gain_1", 4, "Mid gain 1"},
    {"mid_q_1", 5, "Mid Q 1"},
    {"mid_freq_2", 6, "Mid freq 2"},
    {"mid_gain_2", 7, "Mid gain 2"},
    {"mid_q_2", 8, "Mid Q 2"},
    {"high_shelf_freq", 9, "High-shelf freq"},
    {"high_shelf_gain", 10, "High-shelf gain"},
    {"high_shelf_q", 11, "High-shelf Q"},
    {"phase_invert", 12, "Phase Invert"},
};

// ------------------------------------------------------------------
// Compressor ("compressor") — TE Compressor exposes threshold, ratio,
// attack, release; MAGDA's CompressorProcessor adds make-up gain at 4.
// ------------------------------------------------------------------
constexpr AliasSpec kCompressorAliases[] = {
    {"threshold", 0, "Threshold"},     {"ratio", 1, "Ratio"},
    {"attack", 2, "Attack"},           {"release", 3, "Release"},
    {"makeup_gain", 4, "Output gain"},
};

// ------------------------------------------------------------------
// Reverb ("reverb") — TE ReverbPlugin: room size / damping / wet / dry /
// width / mode (freeze).
// ------------------------------------------------------------------
constexpr AliasSpec kReverbAliases[] = {
    {"room_size", 0, "Room Size"}, {"damping", 1, "Damping"}, {"wet", 2, "Wet Level"},
    {"dry", 3, "Dry Level"},       {"width", 4, "Width"},     {"freeze", 5, "Freeze"},
};

// ------------------------------------------------------------------
// Delay ("delay") — TE DelayPlugin: feedback (dB), mix proportion +
// MAGDA's virtual length-in-ms at index 2.
// ------------------------------------------------------------------
constexpr AliasSpec kDelayAliases[] = {
    {"feedback", 0, "Feedback"},
    {"mix", 1, "Mix proportion"},
    {"length", 2, "Length"},
};

// ------------------------------------------------------------------
// Chorus ("chorus") — virtual params from ChorusProcessor: depth, speed,
// width, mix.
// ------------------------------------------------------------------
constexpr AliasSpec kChorusAliases[] = {
    {"depth", 0, "Depth"},
    {"speed", 1, "Speed"},
    {"width", 2, "Width"},
    {"mix", 3, "Mix"},
};

// ------------------------------------------------------------------
// Phaser ("phaser") — virtual params from PhaserProcessor.
// ------------------------------------------------------------------
constexpr AliasSpec kPhaserAliases[] = {
    {"depth", 0, "Depth"},
    {"rate", 1, "Rate"},
    {"feedback", 2, "Feedback"},
};

// ------------------------------------------------------------------
// Filter ("filter") — TE LowPassPlugin: frequency + MAGDA's virtual
// mode toggle (lowpass / highpass).
// ------------------------------------------------------------------
constexpr AliasSpec kFilterAliases[] = {
    {"frequency", 0, "Frequency"},
    {"mode", 1, "Mode"},
};

constexpr AliasSpec kCompiledFilterAliases[] = {
    {"cutoff", 0, "Cutoff"}, {"resonance", 1, "Resonance"}, {"drive", 2, "Drive"},
    {"engine", 3, "Engine"}, {"mode", 4, "Mode"},           {"limit", 5, "Limit"},
};

// ------------------------------------------------------------------
// Saturator ("magda_saturator") — slot order matches
// MagdaSaturatorCompiledPlugin::k*Slot and the [idx:N] pins in
// magda_saturator.dsp.
// ------------------------------------------------------------------
constexpr AliasSpec kCompiledSaturatorAliases[] = {
    {"drive", 0, "Drive"}, {"mode", 1, "Mode"}, {"bias", 2, "Bias"},
    {"tone", 3, "Tone"},   {"mix", 4, "Mix"},   {"output", 5, "Output"},
};

// ------------------------------------------------------------------
// Compiled Delay ("magda_delay") — slot order matches
// MagdaDelayCompiledPlugin::k*Slot and the [idx:N] pins in
// magda_delay.dsp. The legacy "delay" entry above maps to TE's
// DelayPlugin and stays as a separate alias key for projects that
// already had it instantiated; new chains use this one.
// ------------------------------------------------------------------
constexpr AliasSpec kCompiledDelayAliases[] = {
    {"time", 0, "Time"},         {"division", 1, "Division"}, {"sync", 2, "Sync"},
    {"feedback", 3, "Feedback"}, {"mix", 4, "Mix"},           {"tone", 5, "Tone"},
    {"cross", 6, "Cross"},
};

constexpr AliasSpec kCompiledGrainDelayAliases[] = {
    {"time", 0, "Time"},         {"division", 1, "Division"}, {"sync", 2, "Sync"},
    {"size", 3, "Size"},         {"pitch", 4, "Pitch"},       {"spray", 5, "Spray"},
    {"feedback", 6, "Feedback"}, {"mix", 7, "Mix"},
};

// ------------------------------------------------------------------
// Grit ("magda_grit") — slot order matches MagdaGritCompiledPlugin::k*Slot
// and the [idx:N] pins in magda_grit.dsp.
// ------------------------------------------------------------------
constexpr AliasSpec kCompiledGritAliases[] = {
    {"frequency", 0, "Frequency"},
    {"width", 1, "Width"},
    {"amount", 2, "Amount"},
    {"mode", 3, "Mode"},
};

// ------------------------------------------------------------------
// Multiband ("magda_multiband") — slot order matches
// MagdaMultibandCompiledPlugin::k*Slot and the [idx:N] pins in
// magda_multiband.dsp.
// ------------------------------------------------------------------
constexpr AliasSpec kCompiledMultibandAliases[] = {
    {"low_xo", 0, "Low XO"},
    {"high_xo", 1, "High XO"},
    {"depth", 2, "Depth"},
    {"time", 3, "Time"},
    {"low_gain", 4, "Low Gain"},
    {"mid_gain", 5, "Mid Gain"},
    {"high_gain", 6, "High Gain"},
    {"mix", 7, "Mix"},
    {"output", 8, "Output"},
    {"low_thresh_above", 9, "Low Thresh Above"},
    {"low_thresh_below", 10, "Low Thresh Below"},
    {"low_ratio", 11, "Low Ratio"},
    {"mid_thresh_above", 12, "Mid Thresh Above"},
    {"mid_thresh_below", 13, "Mid Thresh Below"},
    {"mid_ratio", 14, "Mid Ratio"},
    {"high_thresh_above", 15, "High Thresh Above"},
    {"high_thresh_below", 16, "High Thresh Below"},
    {"high_ratio", 17, "High Ratio"},
};

// ------------------------------------------------------------------
// Phaser ("magda_phaser") — slot order matches
// MagdaPhaserCompiledPlugin::k*Slot and the [idx:N] pins in
// magda_phaser.dsp.
// ------------------------------------------------------------------
constexpr AliasSpec kCompiledPhaserAliases[] = {
    {"rate", 0, "Rate"},     {"depth", 1, "Depth"},   {"feedback", 2, "Feedback"},
    {"stages", 3, "Stages"}, {"min_hz", 4, "Min Hz"}, {"max_hz", 5, "Max Hz"},
    {"mix", 6, "Mix"},
};

// ------------------------------------------------------------------
// Pitch Shift ("pitchshift") — TE PitchShiftPlugin single param.
// ------------------------------------------------------------------
constexpr AliasSpec kPitchShiftAliases[] = {
    {"semitones", 0, "Semitones"},
};

// ------------------------------------------------------------------
// Utility ("utility") — UtilityProcessor: volume, pan, polarity.
// ------------------------------------------------------------------
constexpr AliasSpec kUtilityAliases[] = {
    {"volume", 0, "Volume"},
    {"pan", 1, "Pan"},
    {"polarity", 2, "Polarity"},
};

// Plugins not curated yet (4OSC, Sampler, DrumGrid, Arpeggiator,
// StepSequencer, IR Reverb, Tone Generator). Their AutoGen names are
// already user-readable so the chained @plugin.param popup works without
// curated entries; we can author short canonical names later.

constexpr PluginSpec kPluginSpecs[] = {
    {"eq", kEqAliases, (int)std::size(kEqAliases)},
    {"compressor", kCompressorAliases, (int)std::size(kCompressorAliases)},
    {"reverb", kReverbAliases, (int)std::size(kReverbAliases)},
    {"delay", kDelayAliases, (int)std::size(kDelayAliases)},
    {"chorus", kChorusAliases, (int)std::size(kChorusAliases)},
    {"phaser", kPhaserAliases, (int)std::size(kPhaserAliases)},
    {"filter", kFilterAliases, (int)std::size(kFilterAliases)},
    {"magda_filter", kCompiledFilterAliases, (int)std::size(kCompiledFilterAliases)},
    {"magda_saturator", kCompiledSaturatorAliases, (int)std::size(kCompiledSaturatorAliases)},
    {"magda_delay_compiled", kCompiledDelayAliases, (int)std::size(kCompiledDelayAliases)},
    {"magda_grain_delay", kCompiledGrainDelayAliases, (int)std::size(kCompiledGrainDelayAliases)},
    {"magda_grit", kCompiledGritAliases, (int)std::size(kCompiledGritAliases)},
    {"magda_multiband", kCompiledMultibandAliases, (int)std::size(kCompiledMultibandAliases)},
    {"magda_phaser", kCompiledPhaserAliases, (int)std::size(kCompiledPhaserAliases)},
    {"pitchshift", kPitchShiftAliases, (int)std::size(kPitchShiftAliases)},
    {"utility", kUtilityAliases, (int)std::size(kUtilityAliases)},
};

}  // namespace

std::map<juce::String, StoredAlias> collectInternalPluginCuratedAliases() {
    std::map<juce::String, StoredAlias> out;
    for (const auto& plugin : kPluginSpecs) {
        for (int i = 0; i < plugin.aliasCount; ++i) {
            const auto& spec = plugin.aliases[i];
            StoredAlias alias;
            alias.pluginTypeKey = plugin.pluginKey;
            alias.paramIndex = spec.paramIndex;
            alias.paramNameAtSetTime = spec.paramName;
            alias.path = std::nullopt;  // Resolved at runtime.

            const juce::String canonicalName = juce::String(plugin.pluginKey) + "." + spec.alias;
            out[canonicalName] = alias;
        }
    }
    return out;
}

}  // namespace magda
