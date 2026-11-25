// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// clang-format off
// rigtorp's queue uses std::aligned_storage without including <type_traits>.
#include <type_traits>
#include <rigtorp/MPMCQueue.h>
// clang-format on

#include "exec/PlanExecutor.hpp"
#include "exec/RenderThreadPool.hpp"

/**
 * @file ParallelPlanExecutor.hpp
 * @brief The executor that ships: the same ops, spread across threads.
 *
 * It renders through PlanExecutor and adds one thing, which is the order ops
 * run in. Nothing about the arithmetic is duplicated here, and that is the
 * point: its output is bit-identical to the reference executor's at every
 * thread count, because the only thing it changes is which thread reaches an op
 * and when.
 *
 * That identity survives threading for two reasons. Everything that sums does
 * so in compiled order rather than in the order its inputs finished, which is
 * what makes float addition's non-associativity a non-issue. And ports share
 * buffers on a test over the dependency DAG rather than over the op list (see
 * assignBuffers), so a slot reused under the reference executor's walk is
 * reused safely under every other valid schedule too.
 *
 * The schedule itself costs nothing to work out: the plan already carries the
 * dependency counts, the reversed edges and the initially-ready set, baked when
 * it was compiled (bakeScheduling). A block copies the counts, seeds the ready
 * set and runs; nothing walks the graph on the callback.
 */

namespace magda::engine {

class ParallelPlanExecutor final : private PlanExecutor::CallbackRenderer {
  public:
    /**
     * @brief Render on @p pool, or on the calling thread alone when it is null.
     *
     * The pool is the host's and outlives every plan: threads are made once and
     * every epoch published afterwards renders on them. A null pool is not a
     * degraded mode but the same executor with one thread, which is what makes
     * "identical at every thread count" a claim about one code path.
     */
    explicit ParallelPlanExecutor(RenderThreadPool* pool = nullptr);
    ~ParallelPlanExecutor() override;

    ParallelPlanExecutor(const ParallelPlanExecutor&) = delete;
    ParallelPlanExecutor& operator=(const ParallelPlanExecutor&) = delete;

    /**
     * @brief Bind a plan to its runtime objects and size the schedule.
     *
     * Off the audio thread, and everything PlanExecutor::prepare says applies:
     * @p previous is the executor this one replaces, and what the differ
     * matches is carried rather than rebuilt.
     *
     * A plan whose scheduling constants are missing or inconsistent is refused
     * the way a malformed one is. They are not something this can work out for
     * itself: they are baked into the plan when it is compiled, and a plan that
     * arrived without them did not come from the compiler.
     */
    std::vector<std::string> prepare(const RenderPlan& plan, const PlanBindings& bindings,
                                     const RenderContext& context,
                                     const ParallelPlanExecutor* previous = nullptr,
                                     const PlanValues* values = nullptr);

    /// @copydoc PlanExecutor::reportUnboundInputs
    std::vector<std::string> reportUnboundInputs(const PlanValues* values) {
        return core_.reportUnboundInputs(values);
    }

    /**
     * @brief Render one block into @p output. On the audio thread.
     *
     * No allocation, no locks, no waiting on anything but the block's own work.
     */
    void process(const PlanValues& values, const BlockInfo& block,
                 juce::AudioBuffer<float>& output);

    /// One side of one block (PlanExecutor::processSide), across the pool like process(): the
    /// callback's in its slot, and the ahead side's in the other, given up between ops (#3361).
    bool processSide(int side, std::uint64_t sequence, const PlanValues& values,
                     const BlockInfo& block, juce::AudioBuffer<float>& output,
                     const std::function<bool()>& underClaim = {}) {
        return core_.processSide(side, sequence, values, block, output, underClaim);
    }

    /// @copydoc PlanExecutor::takeCallbackTally
    PlanExecutor::CallbackTally takeCallbackTally() {
        return core_.takeCallbackTally();
    }

    /// @copydoc PlanExecutor::setRenderAheadDepth
    void setRenderAheadDepth(int blocks) {
        core_.setRenderAheadDepth(blocks);
    }

    /// @copydoc PlanExecutor::setMasterTap
    void setMasterTap(juce::AudioBuffer<float>* tap) {
        core_.setMasterTap(tap);
    }

    /// @copydoc PlanExecutor::setRenderLoad
    void setRenderLoad(RenderLoad* load) {
        core_.setRenderLoad(load);
    }

    /// @copydoc PlanExecutor::setAheadState
    void setAheadState(PlanExecutor::AheadState* state) {
        core_.setAheadState(state);
    }

    /// @copydoc PlanExecutor::withAheadClaim
    template <typename Fn> void withAheadClaim(Fn&& fn) {
        core_.withAheadClaim(std::forward<Fn>(fn));
    }

    /// Threads a block is spread across.
    int numThreads() const {
        return pool_ != nullptr ? pool_->numThreads() : 1;
    }

    /// The most ops the prepared plan can have ready at once: the widest level of its DAG.
    /// What decides how many workers a block wakes.
    int parallelism() const {
        return parallelism_;
    }

    /// The prepared plan, as the reference executor sees it: what it bound,
    /// what it allocated, and what it resolved. Both executors share all of it,
    /// so there is one answer to those questions rather than two.
    const PlanExecutor& reference() const {
        return core_;
    }

    bool fitsParameters(const PlanValues& values) const {
        return core_.fitsParameters(values);
    }

    int latencySamples() const {
        return core_.latencySamples();
    }
    int boundMeterCount() const {
        return core_.boundMeterCount();
    }
    int boundValueTapCount() const {
        return core_.boundValueTapCount();
    }
    void clearUnboundValueTaps() {
        core_.clearUnboundValueTaps();
    }
    void commitReroutes(std::uint64_t epoch) {
        core_.commitReroutes(epoch);
    }
    int audioBufferCount() const {
        return core_.audioBufferCount();
    }
    int midiBufferCount() const {
        return core_.midiBufferCount();
    }
    int midiDelayOverflows() const {
        return core_.midiDelayOverflows();
    }
    int carriedDelayLines() const {
        return core_.carriedDelayLines();
    }
    std::vector<char> unfinishedCrossfades() const {
        return core_.unfinishedCrossfades();
    }
    int activeCrossfades() const {
        return core_.activeCrossfades();
    }
    int carriedCrossfades() const {
        return core_.carriedCrossfades();
    }
    int carriedModifiers() const {
        return core_.carriedModifiers();
    }
    bool isPrepared() const {
        return plan_ != nullptr;
    }
    const RenderContext& preparedContext() const {
        return core_.preparedContext();
    }
    bool appliesValues(const PlanValues& values) const {
        return core_.appliesValues(values);
    }

  private:
    /**
     * @brief One side's schedule of a block, which the pool renders as a job.
     *
     * Two, so the ahead side drains beside the callback's (#3361): the callback's also renders
     * whole blocks, and the ahead side's is given up once the callback waits for its claim.
     */
    class Schedule final : public RenderThreadPool::Job {
      public:
        Schedule(ParallelPlanExecutor& owner, RenderThreadPool::Slot slot)
            : owner_(owner), slot_(slot) {}

        /// Sized for a plan of @p numOps ops. Off the audio thread, once no worker is inside.
        void size(std::size_t numOps);

        /// The drain of @p side (-1 for the whole block), then its tail; an abandonable block's
        /// tail and publish run on whichever thread finishes its last op. False when given up.
        bool render(const PlanValues& values, const BlockInfo& block,
                    juce::AudioBuffer<float>& output, bool applyValues, int side, bool abandonable);

        /// Wait until no worker can be inside this job. Off the audio thread.
        void letGoOfPool();

        /// One ready op of the block in hand, on the calling thread. False when none is ready.
        bool help() {
            return takeOne();
        }

      private:
        bool takeOne() override;
        void finishOnCaller() override;

        /// Render @p op and everything it releases that this thread carries straight on with.
        /// A released consumer is carried when it is the op's only consumer or
        /// its last one, and queued for another thread otherwise.
        void runChain(OpId op);
        void renderProfiled(OpId op);
        void renderTail(OpId op);
        /// The ahead block's tail and publish, on the thread that finished its last op (#3366).
        void finishAheadBlock();
        /// The drain on this thread alone, in plan order, which is dependency order.
        void renderInPlanOrder();
        /// Seed the counts and queue the ops ready at the top of a block. Answers how many of
        /// them a worker would render.
        int startSchedule();
        void enqueue(OpId op);
        /// Whether the callback is waiting for this block, which stops what is not running yet.
        bool givenUp();

        const OpValue& valueOf(OpId op) const {
            return applyValues_ ? values_->ops[static_cast<std::size_t>(op)] : kUnityValue;
        }

        ParallelPlanExecutor& owner_;
        const RenderThreadPool::Slot slot_;

        /// Whether the pool has ever been handed this job. Relaxed: the plan swap that took
        /// the callers off this executor already orders the read after the write.
        std::atomic<bool> handedToPool_{false};

        /// This block, as every thread reads it. Written before the ready set is seeded and
        /// read only by a thread that has taken an op out of it.
        BlockInfo block_;
        const PlanValues* values_ = nullptr;
        bool applyValues_ = false;
        juce::AudioBuffer<float>* output_ = nullptr;

        /// The side this drain renders (PlanExecutor::sideOf), or -1 for the whole block. The
        /// other side's ops still pass through, unrendered, to release their consumers.
        int side_ = -1;
        bool abandonable_ = false;
        std::atomic<bool> abandoned_{false};

        /// Producers each op is still waiting for, copied from the plan every block.
        std::vector<std::atomic<std::uint16_t>> pending_;

        /// The ready set, first in first out, sized so an op is queued at most once a block.
        std::unique_ptr<rigtorp::MPMCQueue<OpId>> ready_;

        /// Ops still to finish this block. Zero is what "the block is done" means.
        alignas(64) std::atomic<int> remaining_{0};

        /// An ahead block published or given up, and which; written by finishAheadBlock().
        std::atomic<bool> finished_{false};
        bool published_ = false;
    };

    void renderWhole(const PlanValues& values, const BlockInfo& block,
                     juce::AudioBuffer<float>& output) override;
    void renderCallbackSide(const PlanValues& values, const BlockInfo& block,
                            juce::AudioBuffer<float>& output, bool applyValues) override;
    bool renderAheadSide(const PlanValues& values, const BlockInfo& block,
                         juce::AudioBuffer<float>& output, bool applyValues) override;
    bool helpAheadSide() override {
        return ahead_.help();
    }

    /// Whether @p op renders in the drain of @p side rather than in the prefix or the tail.
    bool rendersInDrain(OpId op, int side) const;

    /// Wait until no worker can be inside either schedule, so what they own may be resized
    /// or destroyed. Off the audio thread.
    void letGoOfPool();

    PlanExecutor core_;
    RenderThreadPool* pool_ = nullptr;
    int parallelism_ = 1;

    /// Null until a plan with a usable schedule has been prepared. Not the same
    /// question as core_.isPrepared(): a plan can bind cleanly and still arrive
    /// without the constants this needs.
    const RenderPlan* plan_ = nullptr;

    /// Ops that drive the hardware output. Run by whoever drove the block, in
    /// plan order, once the graph has drained: they all accumulate into the one
    /// buffer, and a schedule deciding which of them adds itself first is a
    /// schedule deciding the arithmetic.
    std::vector<OpId> outputOps_;

    /// Modulation taps, rendered after the drain for the reason the outputs
    /// are: each detects through a scratch buffer shared with every other tap,
    /// and two of them are schedulable at once whenever their source tracks
    /// have disjoint subgraphs.
    std::vector<OpId> modSourceOps_;

    /// Hardware insert sends, rendered after the drain for the same reason: two sends add into
    /// the same callback channels and push into the same MIDI port.
    std::vector<OpId> insertSendOps_;

    Schedule callback_{*this, RenderThreadPool::Slot::callback};
    Schedule ahead_{*this, RenderThreadPool::Slot::ahead};
};

}  // namespace magda::engine
