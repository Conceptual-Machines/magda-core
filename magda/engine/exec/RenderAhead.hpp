// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <mutex>
#include <vector>

#include "core/TypeIds.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderContext.hpp"
#include "transport/TransportClock.hpp"

/**
 * @file RenderAhead.hpp
 * @brief The thread that renders a plan's ahead side before the callback asks for it (#1898).
 */

namespace magda::engine {

class AheadLaunches;
class RenderThreadPool;
class ParallelPlanExecutor;
struct ClipSnapshot;
struct ClipStreamTable;
class ClipStreamFeed;

/**
 * @brief The transport each callback renders under, by the sequence it starts at (#1898).
 *
 * A change that moves nothing (a tempo, the click) takes effect at the first block not rendered
 * ahead when it arrived, so both sides agree on every block's beats and the blocks already
 * rendered are heard rather than thrown away. A step stays until the callback passes the next.
 */
struct SequencedTransport {
    struct Step {
        std::uint64_t from = 0;
        TransportSnapshot transport;
    };
    std::vector<Step> steps = std::vector<Step>(1);

    const TransportSnapshot& at(std::uint64_t sequence) const {
        for (auto step = steps.rbegin(); step != steps.rend(); ++step)
            if (step->from <= sequence)
                return step->transport;
        return steps.front().transport;
    }

    /// @p transport from @p from on, dropping what it supersedes and what the callback, next at
    /// @p pending, has passed.
    void stepTo(TransportSnapshot transport, std::uint64_t from, std::uint64_t pending) {
        while (!steps.empty() && steps.back().from >= from)
            steps.pop_back();
        while (steps.size() > 1 && steps[1].from <= pending)
            steps.erase(steps.begin());
        steps.push_back({from, std::move(transport)});
    }
};

/**
 * @brief Renders the ahead side of the blocks the callback is predicted to ask for next.
 *
 * The callback hands over its clock after each advance; this thread advances a copy with the
 * same callback size to predict the blocks to come, numbers them the way the callback will,
 * and renders each into the executor's handoff ring with a view of its own on the clips. A
 * prediction that turns out wrong is a block the callback finds missing.
 *
 * Threading: @ref advanced on the audio thread; the setters on the publishing thread; the
 * rendering on this thread, or on the caller of @ref renderOnce when constructed without one.
 */
class RenderAhead final : private juce::Thread {
  public:
    /** @brief What the ahead side renders against: one epoch's executor and what it reads. */
    struct Epoch {
        /// Keeps the executor alive while this thread renders through it.
        std::shared_ptr<const void> owner;
        ParallelPlanExecutor* executor = nullptr;

        /// Which publish it is, never reused, unlike an executor's address.
        std::uint64_t id = 0;

        /// Tracks whose clips the ahead side renders, sorted: the streams it cues.
        std::vector<TrackId> tracks;

        /// The handles its session tracks play from, or null (#3316). Kept alive by @ref owner.
        AheadLaunches* launches = nullptr;
    };

    /// With @p scheduling, the thread renders as that pool's workers do: their priority and the
    /// device's workgroup, which is what keeps it on time under load from other processes.
    explicit RenderAhead(bool runInBackground = true, const RenderThreadPool* scheduling = nullptr);
    ~RenderAhead() override;

    RenderAhead(const RenderAhead&) = delete;
    RenderAhead& operator=(const RenderAhead&) = delete;

    /// Blocks rendered ahead of the callback at most.
    void setDepth(int blocks);

    /// Render against @p epoch once the callback has rendered a block with it. Waits for a round
    /// in progress, so once it returns nothing renders through the epoch it replaced.
    void setEpoch(Epoch epoch);
    void setValues(std::shared_ptr<const PlanValues> values);
    void setTransport(std::shared_ptr<const SequencedTransport> transport);

    /// The first block nothing has been rendered ahead for. Only inside @ref whileIdle.
    std::uint64_t firstUnrendered();
    /// The block the callback renders next.
    std::uint64_t firstPending();
    void setClips(std::shared_ptr<const ClipSnapshot> clips);
    void setStreamFeed(const ClipStreamFeed* streams);

    /// Run @p fn between rounds, so nothing renders through the epoch meanwhile. Not on the
    /// audio thread.
    template <typename Fn> void whileIdle(Fn&& fn) {
        const std::lock_guard<std::mutex> round(rendering_);
        fn();
    }

    /**
     * @brief The callback has rendered @p epoch, advanced @p core, and will number its next
     *        block @p nextSequence.
     *
     * On the audio thread, once per callback of @p numSamples, after its blocks are released.
     * Lock-free; wakes the thread, which then has the callback's whole period to render in.
     */
    void advanced(const ClockCore& core, std::uint64_t nextSequence, int numSamples,
                  double sampleRate, std::uint64_t epoch);

    /// One round of rendering, without the thread. Returns how many blocks it rendered.
    int renderOnce();

    /// Whether a round that began after the latest wake has finished. Readable from anywhere.
    bool caughtUp() const {
        return served_.load(std::memory_order_acquire) >= wakes_.load(std::memory_order_acquire);
    }

    /// One past the last block rendered ahead for the current epoch. Readable from anywhere.
    std::uint64_t renderedThrough() const {
        return renderedThrough_.load(std::memory_order_acquire);
    }

  private:
    struct Clock {
        ClockCore core;
        std::uint64_t nextSequence = 0;
        int numSamples = 0;
        double sampleRate = 0.0;
        std::uint64_t renderedEpoch = 0;
        bool valid = false;
    };

    void run() override;

    farbot::RealtimeObject<Clock, farbot::RealtimeObjectOptions::realtimeMutatable> clock_;
    std::atomic<std::uint64_t> wakes_{0};
    std::atomic<std::uint64_t> renderedThrough_{0};
    std::atomic<std::uint64_t> served_{0};
    std::atomic<bool> stopping_{false};
    const bool runsInBackground_;
    const RenderThreadPool* scheduling_;

    std::mutex lock_;
    Epoch epoch_;
    std::shared_ptr<const PlanValues> values_;
    std::shared_ptr<const SequencedTransport> transport_;
    std::shared_ptr<const ClipSnapshot> clips_;
    const ClipStreamFeed* streamFeed_ = nullptr;
    int depth_ = 1;

    /// Held for a whole round, so a new epoch waits for the one in flight.
    std::mutex rendering_;

    /// This thread's own: one past the last block it rendered, and where it renders into.
    std::uint64_t rendered_ = 0;
    std::uint64_t renderedFor_ = 0;
    std::uint64_t seenDiscards_ = 0;
    juce::AudioBuffer<float> scratch_;
};

}  // namespace magda::engine
