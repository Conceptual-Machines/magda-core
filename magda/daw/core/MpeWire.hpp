// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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

/// The timbre dimension's controller, and where timbre and pressure rest.
constexpr int kTimbreController = 74;
constexpr int kTimbreRest = 64;
constexpr int kPressureRest = 0;

/// A timbre or pressure value, 0..1, as its 7-bit wire value.
inline int wireForValue(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 127.0)), 0, 127);
}

/// A 7-bit timbre or pressure wire value as the model's 0..1.
constexpr double valueForWire(int wire) {
    return std::clamp(wire, 0, 127) / 127.0;
}

}  // namespace magda::mpe
