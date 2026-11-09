// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file CircleOfFifths.hpp
 * @brief The chord wheel's layout: twelve majors a fifth apart, each over its relative minor.
 */

#pragma once

#include <cmath>
#include <numbers>

#include "music/ChordEnums.hpp"

namespace magda::daw::ui::circle_of_fifths {

inline constexpr int kSegments = 12;

/** @brief @p step folded into [0, 12). */
inline constexpr int wrap(int step) {
    return ((step % kSegments) + kSegments) % kSegments;
}

/** @brief The major root @p step fifths clockwise from C. */
inline constexpr magda::music::ChordRoot majorRootAt(int step) {
    return static_cast<magda::music::ChordRoot>((wrap(step) * 7) % 12);
}

/** @brief The root of the relative minor of the major at @p step. */
inline constexpr magda::music::ChordRoot minorRootAt(int step) {
    return static_cast<magda::music::ChordRoot>((static_cast<int>(majorRootAt(step)) + 9) % 12);
}

/** @brief The step of a chord: a major's own segment, a minor's relative major's. */
inline constexpr int stepOf(magda::music::ChordRoot root, bool minor) {
    const int majorRoot = (static_cast<int>(root) + (minor ? 3 : 0)) % 12;
    return (majorRoot * 7) % 12;  // 7 is its own inverse mod 12
}

enum class Ring { None, Inner, Outer };

/** @brief A wheel segment: the minors' inner ring or the majors' outer one, at a step. */
struct Segment {
    Ring ring = Ring::None;
    int step = 0;

    bool operator==(const Segment&) const = default;
};

/** @brief The root of the chord on @p segment. */
inline constexpr magda::music::ChordRoot rootAt(Segment segment) {
    return segment.ring == Ring::Inner ? minorRootAt(segment.step) : majorRootAt(segment.step);
}

/** @brief The quality of the chord on @p segment: minor inside, major outside. */
inline constexpr magda::music::ChordQuality qualityAt(Segment segment) {
    return segment.ring == Ring::Inner ? magda::music::ChordQuality::Minor
                                       : magda::music::ChordQuality::Major;
}

/** @brief The radii of the wheel's hub and its two rings, in pixels. */
struct Radii {
    float hub = 0.0f;
    float inner = 0.0f;
    float outer = 0.0f;
};

/**
 * @brief The segment under the offset (@p dx, @p dy) from the wheel's centre, y pointing down.
 *
 * @p rotation is how far the wheel is turned, in steps: step @p rotation sits at the top.
 */
inline Segment segmentAt(float dx, float dy, float rotation, Radii radii) {
    const float distance = std::hypot(dx, dy);
    if (distance < radii.hub || distance >= radii.outer)
        return {};
    const float clockwiseFromTop = std::atan2(dx, -dy);
    const float stepAngle = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSegments);
    const int step = wrap(static_cast<int>(std::lround(clockwiseFromTop / stepAngle + rotation)));
    return {distance < radii.inner ? Ring::Inner : Ring::Outer, step};
}

}  // namespace magda::daw::ui::circle_of_fifths
