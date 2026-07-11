#pragma once

#include <juce_core/juce_core.h>

#include <numeric>
#include <utility>

namespace magda::grid {

inline constexpr int minNumerator = 1;
inline constexpr int maxNumerator = 999;
inline constexpr int minDenominator = 1;
inline constexpr int maxDenominator = 64;

// The sole validation boundary for grid and automation snap fractions.
inline std::pair<int, int> normaliseFraction(int numerator, int denominator) {
    numerator = juce::jlimit(minNumerator, maxNumerator, numerator);
    denominator = juce::jlimit(minDenominator, maxDenominator, denominator);
    const int divisor = std::gcd(numerator, denominator);
    return {numerator / divisor, denominator / divisor};
}

}  // namespace magda::grid
