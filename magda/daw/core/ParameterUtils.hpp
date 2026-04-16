#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <utility>
#include <vector>

#include "ParameterInfo.hpp"

namespace magda {
namespace ParameterUtils {

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
