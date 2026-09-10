#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace magda {

/** @brief Highest absolute sample in a block (#2151).
 *
 *  The scalar `peak = max(peak, fabs(x))` form does not vectorise: reassociating
 *  a float reduction needs -ffast-math, which the release build does not set.
 *  juce::AudioBuffer::getMagnitude() is this for a buffer; this is the same
 *  reduction for a bare pointer, which is what a sidechain port and the follower
 *  scratch are.
 */
inline float peakMagnitude(const float* samples, int numSamples) noexcept {
    if (samples == nullptr || numSamples <= 0)
        return 0.0f;

    const auto range = juce::FloatVectorOperations::findMinAndMax(samples, numSamples);
    return juce::jmax(range.getStart(), -range.getStart(), range.getEnd(), -range.getEnd());
}

}  // namespace magda
