#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace magda {

/**
 * @brief Normalize a plugin parameter name to a canonical key.
 *
 * Rules applied in order:
 *   1. Strip diacritics (replace accented chars with ASCII equivalents).
 *   2. Lower-case all characters.
 *   3. Replace any run of non-alphanumeric characters with a single underscore.
 *   4. Strip leading/trailing underscores.
 *   5. Preserve digit runs as-is (e.g. "Cutoff 1" -> "cutoff_1").
 *
 * Examples:
 *   "Filter Cutoff"   -> "filter_cutoff"
 *   "Osc 1 Pitch"     -> "osc_1_pitch"
 *   "Reverb Size (%)" -> "reverb_size"
 *   "Freq"            -> "freq"
 */
juce::String normalizeParamName(const juce::String& input);

/**
 * @brief Make a list of names unique by appending _2, _3 ... for duplicates.
 *
 * The first occurrence keeps its original name; subsequent occurrences get a
 * numeric suffix starting at 2. Suffixes are appended before any existing
 * trailing digits to avoid collisions.
 *
 * Example:
 *   {"cutoff", "cutoff", "cutoff"} -> {"cutoff", "cutoff_2", "cutoff_3"}
 *   {"freq", "gain", "freq"}       -> {"freq", "gain", "freq_2"}
 */
std::vector<juce::String> uniquify(std::vector<juce::String> names);

}  // namespace magda
