// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "plan/RenderPlan.hpp"

/**
 * @file RenderLoad.hpp
 * @brief Live render time per track and device, on the callback and the ahead thread (#3357).
 */

namespace magda::engine {

/**
 * @brief Running sums of the time each track and device takes to render, read by the UI.
 *
 * Writers are the callback, its pool workers and the ahead thread; none locks. A slot is claimed
 * once per key and never freed, so a reader diffs two reads to get a window's share.
 */
class RenderLoad {
  public:
    enum Thread : int { Callback = 0, Ahead = 1 };

    struct Entry {
        TrackId track = INVALID_TRACK_ID;
        ChainSegment segment = ChainSegment::Fx;
        /// INVALID_DEVICE_ID for the track's own ops: clips, mixes, faders, meters.
        DeviceId device = INVALID_DEVICE_ID;
        std::array<std::uint64_t, 2> nanos{};
    };

    /// What the ahead thread did since the last take: its busy time and its worst block.
    struct AheadShare {
        double busyMs = 0.0;
        /// The slowest block's render time over that block's own length.
        double worstBlock = 0.0;
    };

    static constexpr std::size_t kSlots = 2048;

    /// Time @p op took, tallied to the thread rendering it. Any render thread.
    void addOp(const PlanOp& op, std::chrono::steady_clock::duration elapsed);

    /// A callback of @p numSamples has been rendered: the audio time the sums are shares of.
    void addBudget(int numSamples, double sampleRate);

    /// One block the ahead thread rendered, or gave up. Ahead thread.
    void addAheadBlock(std::chrono::steady_clock::duration elapsed, int numSamples,
                       double sampleRate);

    /// The ahead thread's time since the last take, cleared. Audio thread.
    AheadShare takeAhead();

    /// Audio time rendered so far, in nanoseconds.
    std::uint64_t budgetNanos() const {
        return budgetNanos_.load(std::memory_order_relaxed);
    }

    /// Calls @p fn with every slot claimed so far. Any thread.
    template <typename Fn> void forEach(Fn&& fn) const {
        for (const auto& slot : slots_) {
            const auto key = slot.key.load(std::memory_order_acquire);
            if (key == 0)
                continue;
            auto entry = decode(key);
            entry.nanos = {slot.nanos[0].load(std::memory_order_relaxed),
                           slot.nanos[1].load(std::memory_order_relaxed)};
            fn(entry);
        }
    }

    /// Marks the current thread as rendering ahead for the scope's lifetime: the render-ahead
    /// thread, or a worker inside an ahead op. Nests.
    class AheadScope {
      public:
        AheadScope();
        ~AheadScope();
        AheadScope(const AheadScope&) = delete;
        AheadScope& operator=(const AheadScope&) = delete;

      private:
        bool previous_;
    };

  private:
    struct Slot {
        std::atomic<std::uint64_t> key{0};
        std::array<std::atomic<std::uint64_t>, 2> nanos{};
    };

    static std::uint64_t encode(TrackId track, ChainSegment segment, DeviceId device);
    static Entry decode(std::uint64_t key);

    std::array<Slot, kSlots> slots_;
    std::atomic<std::uint64_t> budgetNanos_{0};
    std::atomic<std::uint64_t> aheadNanos_{0};
    std::atomic<double> aheadWorst_{0.0};
};

}  // namespace magda::engine
