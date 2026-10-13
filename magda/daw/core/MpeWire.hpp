#pragma once

#include <algorithm>
#include <cmath>

/**
 * @file MpeWire.hpp
 * @brief How a note's pitch expression goes on and off the wire (#1571).
 *
 * Playback writes it and recording reads it, so both take the zone and the
 * range from here.
 */

namespace magda::mpe {

/// The wheel at rest.
constexpr int kPitchWheelRest = 8192;

/// The fixed MPE conversion range, which is what the model's semitones mean.
constexpr double kSemitoneRange = 48.0;

/// Lower zone: master on channel 1, members on 2 to 16.
constexpr int kFirstMemberChannel = 2;
constexpr int kLastMemberChannel = 16;

/// @p channel is 1-based.
constexpr bool isMemberChannel(int channel) {
    return channel >= kFirstMemberChannel && channel <= kLastMemberChannel;
}

/// The model's semitones as a 14-bit wheel value.
inline int wheelForSemitones(double semitones) {
    const auto clamped = std::clamp(semitones, -kSemitoneRange, kSemitoneRange);
    return std::clamp(
        static_cast<int>(std::lround(kPitchWheelRest + clamped / kSemitoneRange * 8191.0)), 0,
        16383);
}

/// A 14-bit wheel value as the model's semitones.
inline double semitonesForWheel(int wheel) {
    return std::clamp(static_cast<double>(wheel - kPitchWheelRest) / 8191.0 * kSemitoneRange,
                      -kSemitoneRange, kSemitoneRange);
}

}  // namespace magda::mpe
