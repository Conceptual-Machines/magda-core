#pragma once

#include <algorithm>
#include <cmath>

namespace magda::interaction {

// ============================================================================
// CLIP NUDGE (#1957)
//
// The keyboard counterpart of a clip drag: arrow keys move the selection along
// the timeline and across tracks. Both axes resolve to a single delta applied
// to every selected clip, exactly as a drag does, so a multi-clip selection
// keeps its internal spacing instead of collapsing onto one position/track.
//
// Plain data in, delta out — no components, no singletons — so every rule here
// is headless-testable (tests/test_clip_nudge.cpp).
// ============================================================================

/**
 * @brief Horizontal nudge input.
 *
 * Deliberately domain-agnostic: anchor and step are simply "positions" and a
 * "step" in the same unit. Bars/Beats mode feeds it beats and a beat fraction;
 * Seconds mode feeds it seconds and the second-based grid interval, because
 * that is the grid actually drawn on screen. Whichever goes in comes back out.
 */
struct HorizontalNudge {
    double anchorPosition = 0.0;  ///< Earliest selected clip's start.
    double gridStep = 1.0;        ///< Current grid step, same unit as the anchor.
    bool snapToGrid = true;       ///< Timeline snap state.
    int direction = 1;            ///< -1 = earlier, +1 = later.
};

/**
 * @brief Offset to add to every selected clip; 0 means the move is refused.
 *
 * With snap on the anchor lands *on* the grid rather than carrying its
 * off-grid offset along forever — one press from 0.5 with a step of 1 goes to
 * 1.0, not 1.5. With snap off it is a plain step of one grid unit. The
 * timeline starts at 0, so a selection near the start clamps flush against it
 * rather than refusing to move.
 */
inline double horizontalDelta(const HorizontalNudge& nudge) {
    if (nudge.direction == 0 || !(nudge.gridStep > 0.0))
        return 0.0;

    const int step = nudge.direction > 0 ? 1 : -1;
    double target = nudge.anchorPosition + (step * nudge.gridStep);

    if (nudge.snapToGrid) {
        // Work in grid units so the "already on the grid?" test is scale-free.
        // Without the tolerance, an anchor a hair below a grid line (the usual
        // residue of earlier snapped moves) reads as off-grid and the nudge
        // travels an invisible fraction of a step instead of a whole one.
        double gridUnits = nudge.anchorPosition / nudge.gridStep;
        const double nearestUnit = std::round(gridUnits);
        if (std::abs(gridUnits - nearestUnit) < 1e-6)
            gridUnits = nearestUnit;

        const double targetUnit =
            step > 0 ? std::floor(gridUnits) + 1.0 : std::ceil(gridUnits) - 1.0;
        target = targetUnit * nudge.gridStep;
    }

    target = std::max(target, 0.0);

    return target - nudge.anchorPosition;
}

/**
 * @brief Vertical nudge input: where the selection sits in the list of
 *        clip-hosting tracks, in display order.
 */
struct VerticalNudge {
    int minTrackIndex = 0;  ///< Topmost selected clip's track.
    int maxTrackIndex = 0;  ///< Bottommost selected clip's track.
    int trackCount = 0;     ///< Number of clip-hosting tracks.
    int direction = 1;      ///< -1 = up, +1 = down.
};

/**
 * @brief Track-index delta for the whole selection; 0 means the move is refused.
 *
 * The selection travels as a block or not at all. Clamping per clip the way a
 * drag does would pile the selection onto the end track and silently destroy
 * its vertical spread — recoverable only by undo.
 */
inline int verticalTrackDelta(const VerticalNudge& nudge) {
    if (nudge.direction == 0 || nudge.trackCount <= 0)
        return 0;
    if (nudge.minTrackIndex < 0 || nudge.maxTrackIndex >= nudge.trackCount ||
        nudge.minTrackIndex > nudge.maxTrackIndex)
        return 0;

    const int delta = nudge.direction > 0 ? 1 : -1;
    if (nudge.minTrackIndex + delta < 0 || nudge.maxTrackIndex + delta >= nudge.trackCount)
        return 0;

    return delta;
}

}  // namespace magda::interaction
