// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "clip/SessionPlayback.hpp"
#include "exec/PlanExecutor.hpp"
#include "launch/LaunchRequests.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * @file AheadLaunches.hpp
 * @brief The launch handles the ahead side renders session tracks from (#3316).
 */

namespace magda::engine {

class ClipSnapshotFeed;

/**
 * @brief Copies of the handles and section states of the tracks one epoch renders ahead.
 *
 * The callback keeps advancing the real handles, which the UI, takes and capture read; these are
 * advanced over the blocks the ahead side renders. Every request reaches both at a beat neither
 * has passed (advanceLaunchHandles), so they are one state a look-ahead apart, and a block the
 * callback renders whole sets these back to the real ones.
 *
 * Threading: built and reshaped on the publishing thread. @ref forward and @ref renderedWhole run
 * on the audio thread and @ref beginAheadBlock on the ahead side's, the last two under the
 * handoff claim.
 */
class AheadLaunches final : public AheadSlots, public PlanExecutor::AheadState {
  public:
    /// Copies of the slots @p real holds on @p aheadTracks, unseeded: the epoch's first block
    /// renders whole, which seeds them.
    AheadLaunches(const LaunchHandleTable& real, std::span<const TrackId> aheadTracks,
                  LaunchHandleFeed& realHandles, ClipSnapshotFeed& realClips);

    bool covers(TrackId trackId) const override;
    void forward(const LaunchRequest& request) override;

    void beginAheadBlock(const BlockInfo& block) override;
    void renderedWhole() override;

    /// What the ahead side's blocks read in place of the real tables.
    const LaunchHandleTable& handles() const {
        return table_;
    }
    const TrackSectionTable& sections() const {
        return sections_;
    }

    /** @brief The copies a new handle table needs, made ready for @ref adopt. */
    struct Reshape {
        std::vector<LaunchHandle> copies;
        std::vector<LaunchHandleTable::Entry> entries;

        /// Per entry, the copy whose state it carries on with, or -1 for one starting stopped.
        std::vector<int> from;
    };

    /// What @p real changes about the slots copied here, or nothing. Publishing thread.
    std::optional<Reshape> reshapeFor(const LaunchHandleTable& real) const;

    /// Take @p next: a slot on the same handle keeps its state, a new one starts stopped as its
    /// real handle does. @p next is left with what it replaced. While neither side renders.
    void adopt(Reshape& next);

    /// Requests dropped because the ahead side was a ring of them behind. Any thread.
    int overflows() const {
        return overflows_.load(std::memory_order_relaxed);
    }

  private:
    static constexpr int kCapacity = LaunchRequestQueue::kCapacity;

    /// Every track rendered ahead, sorted: what is forwarded and gated here.
    std::vector<TrackId> tracks_;

    std::vector<LaunchHandle> copies_;
    LaunchHandleTable table_;
    std::vector<TrackSectionState> states_;
    TrackSectionTable sections_;

    LaunchHandleFeed& realHandles_;
    ClipSnapshotFeed& realClips_;

    std::array<LaunchRequest, kCapacity> forwarded_{};
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> read_{0};
    std::atomic<int> overflows_{0};
};

}  // namespace magda::engine
