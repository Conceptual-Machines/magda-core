#pragma once

#include <cstdint>
#include <optional>

#include "launch/LaunchHandle.hpp"

/**
 * @file FollowActions.hpp
 * @brief What a slot does when its run reaches the end (#2304).
 *
 * The one launch the engine makes itself. Every other arrives down the queue in
 * LaunchRequests.hpp; this one is a beat the clip named when it was written, so
 * the audio thread works it out and acts on it in the block it falls in.
 *
 * It reaches the handle through `LaunchHandle::play` and `stop`, which is what a
 * user launch reaches, so the slot it starts publishes itself through its own
 * tap exactly as a clicked one does and there is nothing to adopt (#1894).
 */

namespace magda::engine {

struct LaunchHandleTable;

/// What a slot asks for at the end of its run. The model's `FollowAction` at the
/// launcher's altitude, named apart from it: a slot rather than a clip.
enum class SlotAction : std::uint8_t {
    /// Stop, or carry on for ever when the slot loops.
    none,

    stop,

    /// Launch this slot again, which restarts the run.
    again,

    /// The next slot of this track that holds a clip, or nothing at the last.
    next,

    /// The previous one, or nothing at the first.
    previous,

    /// Any slot of this track that holds a clip, this one included.
    random,
};

/// What ends one slot's run: the action, and how long the run gets first.
struct SlotFollow {
    SlotAction action = SlotAction::none;

    /// Passes before it fires. Counted only while the slot re-triggers.
    int loopCount = 1;

    /// Beats added after those passes.
    double delayBeats = 0.0;

    /// The slot's own length, which is what a pass is worth when the handle is
    /// not re-triggering. Zero leaves the run with no end.
    double lengthBeats = 0.0;

    bool operator==(const SlotFollow&) const = default;
};

/**
 * @brief The monotonic beat @p handle's run ends on, if it ends.
 *
 * Counted from `LaunchHandle::scheduleBeat`, which survives a re-trigger; the
 * run's origin does not, so a third pass of three would never arrive.
 *
 * Absent for a slot that loops with nothing to do at the end.
 */
std::optional<double> followDueBeat(const LaunchHandle& handle, const SlotFollow& follow);

/**
 * @brief The handle @p follow starts at @p dueBeat, or null when it starts none.
 *
 * Null for a stop, and for a neighbour past either end of the track. The caller
 * owns the source's stop, since a run that reached its end stops whether or not
 * anything follows it.
 *
 * Named against @p table as it stands now, so a slot deleted or moved since the
 * project loaded is not in the list this searches.
 */
LaunchHandle* followTarget(const LaunchHandleTable& table, const SlotKey& key,
                           const SlotFollow& follow, double dueBeat);

}  // namespace magda::engine
