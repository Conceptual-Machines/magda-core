#pragma once

#include <algorithm>
#include <vector>

namespace magda::interaction {

// ============================================================================
// CLIP DRAG TARGETS (#2179)
//
// Where a clip drag may land. The mouse counterpart of ClipNudge: the pointer
// moves over every lane in the stack, and only some of them can hold what is
// being dragged, so the destination is resolved in the index space of the
// lanes that can rather than in raw lane indices.
//
// Working in that space is what lets the ghost step over a refusing lane the
// way the nudge already steps over one, and it is also what keeps a multi-clip
// selection together: one delta in slot space moves every clip past the same
// number of usable lanes, where one delta in lane space would move clips that
// started either side of a refusing lane by different amounts.
//
// "Lane" here is an index into the visible track list; "slot" is an index into
// the subset of those lanes that accept the whole selection. Plain integers in,
// plain integers out — no components, no singletons — so the rules are
// headless-testable (tests/test_clip_drag_targets.cpp).
// ============================================================================

/**
 * @brief Slot whose lane is nearest to @p lane; -1 when there are no slots.
 *
 * @p hostLanes must be ascending. A pointer sitting on a refusing lane resolves
 * to whichever usable lane is closest, which is what makes the ghost jump past
 * the refusal instead of stopping on it. Exactly between two, the upper lane
 * wins — arbitrary, but it has to be decided somewhere or the ghost flickers
 * between them on a pixel of mouse noise.
 */
inline int nearestHostSlot(const std::vector<int>& hostLanes, int lane) {
    if (hostLanes.empty())
        return -1;

    // Ascending, so the first lane at or past the pointer and the one before it
    // are the only two candidates.
    const auto at = std::lower_bound(hostLanes.begin(), hostLanes.end(), lane);
    if (at == hostLanes.begin())
        return 0;
    if (at == hostLanes.end())
        return static_cast<int>(hostLanes.size()) - 1;

    const auto below = at - 1;
    const int distanceAbove = *at - lane;
    const int distanceBelow = lane - *below;
    return static_cast<int>(
        std::distance(hostLanes.begin(), distanceAbove < distanceBelow ? at : below));
}

/**
 * @brief The part of @p rawDelta the whole selection can take.
 *
 * A drag is continuous, so a selection that reaches the end of the stack slides
 * up against it rather than refusing the way a keypress does. Trimmed as a
 * block rather than per clip: clamping each clip to the last slot on its own
 * would pile the selection onto one lane and flatten a vertical spread that
 * only undo could restore.
 */
inline int blockSlotDelta(int rawDelta, int minSlot, int maxSlot, int slotCount) {
    if (slotCount <= 0 || minSlot < 0 || maxSlot >= slotCount || minSlot > maxSlot)
        return 0;

    return std::clamp(rawDelta, -minSlot, slotCount - 1 - maxSlot);
}

}  // namespace magda::interaction
