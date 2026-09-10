#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace magda {

/** @brief Highest absolute sample in a block, ignoring NaN (#2151).
 *
 *  The scalar `peak = max(peak, fabs(x))` form does not vectorise: reassociating
 *  a float reduction needs -ffast-math, which the release build does not set.
 *  It also ignores NaN, because the compare against it fails, while still
 *  counting an infinity. Both are kept here.
 *
 *  findMinAndMax cannot be handed a NaN: its SIMD min/max differ by
 *  architecture, and on the lane that met the NaN the running extreme is
 *  dropped, so a real peak can go missing without the result being NaN at all.
 *  The block is screened first, which is an integer reduction over a compare and
 *  vectorises where the float one will not.
 */
inline float peakMagnitude(const float* samples, int numSamples) noexcept {
    if (samples == nullptr || numSamples <= 0)
        return 0.0f;

    int nanCount = 0;
    for (int i = 0; i < numSamples; ++i)
        nanCount += static_cast<int>(samples[i] != samples[i]);

    if (nanCount == 0) {
        const auto range = juce::FloatVectorOperations::findMinAndMax(samples, numSamples);
        return juce::jmax(range.getStart(), -range.getStart(), range.getEnd(), -range.getEnd());
    }

    // A NaN in the block is an upstream bug, so this path is not the one worth
    // making fast: it is the loop this function replaces, which walks past a
    // NaN and reports the loudest real sample around it.
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::fabs(samples[i]));
    return peak;
}

}  // namespace magda
