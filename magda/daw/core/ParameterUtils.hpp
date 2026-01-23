#pragma once

#include <juce_core/juce_core.h>

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
 * @brief Format a real value for display with appropriate units
 *
 * @param realValue Real parameter value
 * @param info Parameter metadata for unit and scale
 * @param decimalPlaces Number of decimal places (default: 1)
 * @return Formatted string (e.g., "440 Hz", "100 ms", "50%")
 *
 * Handles special formatting:
 * - Frequency: Shows kHz for values >= 1000 Hz
 * - Time: Shows s for values >= 1000 ms
 * - Discrete: Returns choice name instead of number
 * - Boolean: Returns "On" or "Off"
 */
juce::String formatValue(float realValue, const ParameterInfo& info, int decimalPlaces = 1);

/**
 * @brief Get the choice string for a discrete parameter value
 *
 * @param index Choice index (0-based)
 * @param info Parameter metadata with choices
 * @return Choice string, or empty string if index out of range
 */
juce::String getChoiceString(int index, const ParameterInfo& info);

}  // namespace ParameterUtils
}  // namespace magda
