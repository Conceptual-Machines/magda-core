#pragma once

#include <vector>

#include "core/ClipInfo.hpp"

/**
 * @file WarpMap.hpp
 * @brief A clip's own musical time, compiled into something playable.
 *
 * The model holds warp as `(sourceTime, warpTime)` pairs in source seconds,
 * piecewise linear between them and carrying on at slope 1 outside the marker
 * range (`AudioEvent::warpedSourceSeconds`). That direction answers "where does
 * this bit of file land musically", which is what the editors and the loop
 * region's beat views ask.
 *
 * Playback asks the other one. At an instant of the timeline the material has
 * reached some warp time, and what a reader needs is the source second sitting
 * there. So the map is inverted here, once, at compile time, and what reaches
 * the audio thread is a vector that is sorted in both coordinates and known to
 * be invertible.
 *
 * Known, rather than assumed. A marker list that doubles back has no inverse at
 * all, and the two places that could discover it are a compile that can say so
 * and a callback that would divide by a negative span. It is discovered here:
 * @ref compileWarpMap drops what it cannot invert and says how much it dropped.
 *
 * The map stays in the model's own coordinates -- the file as it sits on disk,
 * forwards -- and reverse is not baked into it. Reverse is a coordinate change
 * that EventPlacement.hpp already owns, and a reversed event walks this map
 * backwards from the far end of what it reads and mirrors the answer. Mirroring
 * the map here instead would need the length of the region the event reads,
 * which is itself an answer from the map, and the two would define each other.
 *
 * The incumbent cannot do reverse and warp together at all: it bakes warp into
 * a rendered proxy file and can only bake one thing per clip, so a reversed
 * warped clip there silently loses its markers
 * (`WaveAudioClip::createRenderJob` returns the reverse job before it ever
 * reaches the warp one). Computing both live has no such constraint.
 */

namespace magda::engine {

/**
 * @brief The inverted map: warp time to source time, and back.
 *
 * Empty is identity, which is what an event with warp off compiles to and what
 * makes every caller warp-agnostic. Non-empty is strictly increasing in both
 * coordinates, so either direction is a binary search and a lerp.
 */
struct WarpMap {
    struct Point {
        double sourceSeconds = 0.0;
        double warpSeconds = 0.0;

        bool operator==(const Point&) const = default;
    };

    /// Sorted ascending, strictly increasing in both coordinates. Never holds a
    /// pair whose span is zero on either side.
    std::vector<Point> points;

    bool empty() const noexcept {
        return points.empty();
    }

    /// The source second heard at @p warpSeconds. Slope 1 outside the range,
    /// offset by the nearest point, which is the model's rule.
    double sourceSecondsAt(double warpSeconds) const noexcept;

    /// The warp time @p sourceSeconds lands at. The model's own direction, and
    /// needed at playback for one thing: the event's anchor is a source
    /// position and its elapsed is warp time, so the two have to be brought
    /// into one domain before they can be added.
    double sourceToWarpSeconds(double sourceSeconds) const noexcept;

    /**
     * @brief The steepest source-per-warp any segment runs at, and never below 1.
     *
     * What a stretcher is sized against. A warped event's rate is not one
     * number -- that is the whole point of it -- so what the pre-roll and the
     * diagnostics need is the worst of them. Never below 1 because the map
     * carries on at slope 1 outside its markers, so every map has a stretch
     * that runs at unity.
     */
    double maxSourcePerWarp() const noexcept;

    bool operator==(const WarpMap&) const = default;
};

/**
 * @brief What compiling one event's markers produced, and what it cost.
 *
 * The count is for a diagnostic. A dropped marker is a marker the user placed
 * and cannot hear, so the compile says how many rather than leaving the clip
 * quietly playing a map that is not the one on screen.
 */
struct WarpCompileResult {
    WarpMap map;
    int droppedMarkers = 0;
};

/**
 * @brief Compile @p markers into an invertible map.
 *
 * Sorts by source time and drops what does not strictly increase on both sides.
 *
 * A marker that does not increase is dropped rather than the whole map being
 * refused: the rest of them still describe something, and a user who dragged
 * one marker past its neighbour should lose that marker rather than the clip's
 * timing. Coincident markers go the same way, which is what keeps every
 * segment's span positive and the lookup free of a zero divide.
 */
WarpCompileResult compileWarpMap(const std::vector<WarpMarker>& markers);

}  // namespace magda::engine
