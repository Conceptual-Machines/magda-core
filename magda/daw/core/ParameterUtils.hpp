#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <utility>
#include <vector>

#include "ParameterInfo.hpp"

namespace magda {
namespace ParameterUtils {

/**
 * @brief The part of a ParameterInfo that decides what a normalized position means.
 *
 * A flat value type with no heap in it, because the conversion is not only a UI
 * concern: the native engine resolves a modulated parameter on the audio thread
 * every block (#2116), where a ParameterInfo cannot go. It carries strings, a
 * choice list and a shared display provider, none of which the curve reads.
 *
 * Split out rather than reimplemented on the engine side. Two implementations of
 * one curve is a difference nobody sees until a project sounds different in one
 * engine than the other, so the conversion below is the only one there is and
 * the ParameterInfo overloads are this one under another name.
 */
struct ParameterDomain {
    ParameterScale scale = ParameterScale::Linear;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float skewFactor = 1.0f;
    float scaleAnchor = 0.0f;

    /// Number of entries in the parameter's choice list, for Discrete. Zero
    /// everywhere else, and Discrete with zero choices converts to 0 the way
    /// an empty `choices` does.
    int choiceCount = 0;
};

/** @brief The conversion domain of @p info. */
ParameterDomain domainOf(const ParameterInfo& info);

/**
 * @brief Whether the domain's values are discrete steps rather than a continuum.
 *
 * A stepped parameter is never ramped: there is nothing between two of its
 * values to ramp through.
 */
bool isStepped(const ParameterDomain& domain);

/**
 * @brief Convert normalized value (0-1) to real parameter value
 *
 * @param normalized Normalized value in range [0, 1]
 * @param info Parameter metadata defining the conversion
 * @return Real value (e.g., Hz, ms, dB)
 *
 * Example:
 *   auto cutoff = ParameterPresets::frequency(0, "Cutoff");
 *   float realHz = normalizedToReal(0.5f, cutoff);  // ~632 Hz (geometric mean)
 */
float normalizedToReal(float normalized, const ParameterInfo& info);
float normalizedToReal(float normalized, const ParameterDomain& domain);

/**
 * @brief Convert real parameter value to normalized (0-1)
 *
 * @param real Real value (e.g., 440.0 Hz)
 * @param info Parameter metadata defining the conversion
 * @return Normalized value in range [0, 1]
 *
 * Example:
 *   auto cutoff = ParameterPresets::frequency(0, "Cutoff");
 *   float norm = realToNormalized(440.0f, cutoff);  // ~0.353
 */
float realToNormalized(float real, const ParameterInfo& info);
float realToNormalized(float real, const ParameterDomain& domain);

/**
 * @brief A normalized fader position as the linear gain `TrackManager` stores.
 *
 * `TrackInfo::volume` and `SendInfo::level` are linear gains, while the fader
 * that drives them is a dB scale, so every writer of one from the other does
 * `pow(10, dB / 20)` after `normalizedToReal`. This is that pair, in one place,
 * because it had grown four copies: the controller writer, its reader, the OSC
 * feedback projection, and the automation playback writeback. An inverse only
 * stays an inverse while both halves are read together.
 */
float gainFromNormalized(float normalized, const ParameterInfo& info);

/**
 * @brief The linear gain as the normalized fader position that produces it.
 *
 * Silence has no dB, so a gain of zero answers the bottom of the parameter's
 * range, which is where a fader at no gain sits.
 */
float normalizedFromGain(float gain, const ParameterInfo& info);

/**
 * @brief True when ParameterInfo real/display range matches the value range
 *        stored by the owning Tracktion AutomatableParameter.
 */
bool infoMatchesTeRange(const ParameterInfo& info);

/**
 * @brief True for MAGDA internal parameters that expose a normalized TE param
 *        but store/display a scaled real value in DeviceInfo.currentValue.
 */
bool isDisplayMappedInternalValue(const ParameterInfo& info);

/**
 * @brief Convert a MAGDA-normalized lane/controller value to the model value
 *        stored in DeviceInfo.currentValue and passed through TrackManager.
 *
 * For normal internal params and compiled/interpreted Faust params this returns
 * the scaled real/display value. For external plugin params with an AI-detect
 * display range override it returns the TE-native value, so UI preview and
 * automation echo do not fight ExternalPluginProcessor's native listener.
 */
ParameterModelValue normalizedToModelValue(ParameterNormalizedValue normalized,
                                           const ParameterInfo& info);

/**
 * @brief Inverse of normalizedToModelValue().
 */
ParameterNormalizedValue modelToNormalizedValue(ParameterModelValue model,
                                                const ParameterInfo& info);

/**
 * @brief Convert a DeviceInfo/model value to the raw value stored by the
 *        owning Tracktion AutomatableParameter.
 *
 * Display-mapped internal parameters (for example a compiled snare decay in
 * milliseconds backed by a 0..1 host parameter) are projected into the TE
 * range. Parameters whose model and TE ranges already match pass through.
 */
float modelToTeValue(ParameterModelValue model, const ParameterInfo& info);

/**
 * @brief True when DeviceInfo::currentValue for @p info is the TE-native value
 *        rather than a scaled real one.
 *
 * This is the external-plugin case: a saved parameter config or AI-Detect gave
 * the parameter a display range (or a choice list) while TE keeps storing it
 * normalized, and normalizedToModelValue() deliberately leaves the model in
 * TE's domain so it cannot fight the plugin's own listener. Everything else
 * (internal devices, external params without an override) carries the scaled
 * value the display range describes.
 */
bool modelHoldsTeNativeValue(const ParameterInfo& info);

/**
 * @brief The choice a Discrete parameter's model value selects.
 *
 * Internal devices store the choice index itself, so this is a rounding. An
 * external parameter whose saved config marks it discrete stores TE's
 * normalized value (see modelHoldsTeNativeValue), which has to be spread over
 * the choice list: Serum's Bend Up at 0.54167 with 49 choices is "+2", not
 * choice 1. Returns -1 when there are no choices.
 */
int choiceIndexForModelValue(ParameterModelValue model, const ParameterInfo& info);

/**
 * @brief The model value that selects choice @p index. Inverse of
 *        choiceIndexForModelValue().
 */
ParameterModelValue modelValueForChoiceIndex(int index, const ParameterInfo& info);

/**
 * @brief What one modulation link adds to a normalized value.
 *
 * The polarity and depth rule on its own, with no base and no clamp, because
 * the clamp belongs after every contribution has been added rather than after
 * each one. Both functions below are this plus a sum plus that clamp, and so is
 * the native engine's per-block resolution (#2116), which cannot call either of
 * them: one clamps too early and the other wants a vector.
 *
 * @param modValue Modulator output (0-1, e.g. LFO value)
 * @param amount   Modulation depth (-1 to 1)
 * @param bipolar  If true, modValue 0-1 maps to -1 to +1 before scaling
 */
float modulationOffset(float modValue, float amount, bool bipolar);

/**
 * @brief Apply modulation to a base normalized value
 *
 * @param baseNormalized Base parameter value (0-1)
 * @param modValue Modulator output (0-1, e.g., LFO value)
 * @param amount Modulation depth (0-1)
 * @param bipolar If true, modValue 0-1 maps to -1 to +1 offset
 * @return Clamped normalized value after modulation
 *
 * Example - Bipolar modulation:
 *   applyModulation(0.5f, 1.0f, 0.5f, true)
 *   // modValue 1.0 → offset +1.0 (bipolar)
 *   // delta = +1.0 * 0.5 = +0.5
 *   // result = 0.5 + 0.5 = 1.0 (clamped)
 *
 * Example - Unipolar modulation:
 *   applyModulation(0.5f, 1.0f, 0.5f, false)
 *   // modValue 1.0 → offset +1.0 (unipolar)
 *   // delta = +1.0 * 0.5 = +0.5
 *   // result = 0.5 + 0.5 = 1.0 (clamped)
 */
float applyModulation(float baseNormalized, float modValue, float amount, bool bipolar = true);

/**
 * @brief Apply multiple modulations to a base normalized value
 *
 * @param baseNormalized Base parameter value (0-1)
 * @param modsAndAmounts Vector of (modValue, amount) pairs
 * @param bipolar If true, modValues 0-1 map to -1 to +1 offsets
 * @return Clamped normalized value after all modulations
 *
 * Example:
 *   std::vector<std::pair<float, float>> mods = {{0.8f, 0.3f}, {0.2f, 0.5f}};
 *   float result = applyModulations(0.5f, mods, true);
 */
float applyModulations(float baseNormalized,
                       const std::vector<std::pair<float, float>>& modsAndAmounts,
                       bool bipolar = true);

/**
 * @brief Format a REAL parameter value for display.
 *
 * Dispatches on `info.displayFormat`:
 *   - Default: dispatch on `info.unit` (Hz/kHz, dB with sign, %, ms/s, bare).
 *   - Decibels: signed dB with "-inf" at minValue ("+3.0 dB", "-6.0 dB").
 *   - Pan: -1..+1 → "C", "L25", "R50".
 *   - Percent: the stored value is treated as a fraction [0..1]; displays "0%".."100%".
 *   - MidiNote: 0..127 → "C-1".."G9".
 *   - Beats: "2.25 beats".
 *   - BarsBeats: "1.1.000" (480 ticks/beat).
 * Discrete/Boolean scales bypass displayFormat and return choice name / "On"/"Off".
 *
 * Never fails — always returns a string.
 */
juce::String formatValue(float realValue, const ParameterInfo& info, int decimalPlaces = 1);

/**
 * @brief Parse user text → REAL parameter value.
 *
 * Accepts both display-style strings (what formatValue produces) AND raw
 * numbers. Clamps the result to [info.minValue, info.maxValue]. Returns
 * nullopt on unparseable input — caller keeps the prior value.
 *
 * Format-specific extras:
 *   - Decibels: "-inf", "inf", trailing "db"/"dB" optional.
 *   - Pan: "C"/"c"/"center", "L25", "R50", or bare number -100..+100.
 *   - Percent: trailing "%" optional; a bare number is interpreted as a percent.
 *   - MidiNote: note names ("C4", "Eb3", "D#5"), or bare MIDI number.
 *   - Default: trailing unit suffix optional (matches info.unit); "kHz" → x1000.
 */
std::optional<float> parseValue(const juce::String& text, const ParameterInfo& info);

/**
 * @brief Get the choice string for a discrete parameter value
 *
 * @param index Choice index (0-based)
 * @param info Parameter metadata with choices
 * @return Choice string, or empty string if index out of range
 */
juce::String getChoiceString(int index, const ParameterInfo& info);

/**
 * @brief Snap a normalized value (0-1) to the parameter's natural grid.
 *
 * Used by the automation curve editor's value-snap mode. Returns the
 * closest normalized value on the grid the parameter would draw in its
 * UI (dB ticks for fader volume, L/50L/C/50R/R for pan, 10% steps for
 * generic percent, discrete choices). If the parameter has no natural
 * grid, returns the input unchanged.
 */
double snapNormalizedToGrid(double normalized, const ParameterInfo& info);

}  // namespace ParameterUtils
}  // namespace magda
