#include "InternalPluginAliases.hpp"

#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"

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
    const char* pluginKey;  // "eq", "compressor", ...
    const AliasSpec* aliases;
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

// ------------------------------------------------------------------
// Pitch Shift ("pitchshift") — TE PitchShiftPlugin single param.
// ------------------------------------------------------------------
constexpr AliasSpec kPitchShiftAliases[] = {
    {"semitones", 0, "Semitones"},
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
    {"pitchshift", kPitchShiftAliases, (int)std::size(kPitchShiftAliases)},
};

}  // namespace

std::map<juce::String, StoredAlias> collectInternalPluginCuratedAliases() {
    std::map<juce::String, StoredAlias> out;
    auto addAlias = [&out](const juce::String& pluginKey, const char* aliasName, int paramIndex,
                           const char* paramNameAtSetTime) {
        StoredAlias alias;
        alias.pluginTypeKey = pluginKey;
        alias.paramIndex = paramIndex;
        alias.paramNameAtSetTime = paramNameAtSetTime;
        alias.path = std::nullopt;  // Resolved at runtime.

        const juce::String canonicalName = pluginKey + "." + aliasName;
        out[canonicalName] = alias;
    };

    for (const auto& plugin : kPluginSpecs) {
        for (int i = 0; i < plugin.aliasCount; ++i) {
            const auto& spec = plugin.aliases[i];
            addAlias(plugin.pluginKey, spec.alias, spec.paramIndex, spec.paramName);
        }
    }

    for (const auto* plugin : daw::audio::compiled::getAllCompiledPluginSpecs()) {
        if (plugin == nullptr || plugin->aliases == nullptr)
            continue;

        const juce::String pluginKey =
            plugin->aliasKey != nullptr ? plugin->aliasKey : plugin->pluginId;
        for (int i = 0; i < plugin->aliasCount; ++i) {
            const auto& spec = plugin->aliases[i];
            addAlias(pluginKey, spec.alias, spec.paramIndex, spec.paramName);
        }
    }
    return out;
}

}  // namespace magda
