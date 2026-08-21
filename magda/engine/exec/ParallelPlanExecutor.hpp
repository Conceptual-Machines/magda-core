#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

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

/// An empty ready stack: no op on top, and a tag that has not moved yet. The
/// packing is in ParallelPlanExecutor.cpp, which asserts that this is what it
/// produces for an empty one.
inline constexpr std::uint64_t kEmptyReadyStack = 0xffffffffULL;

class ParallelPlanExecutor final : private RenderThreadPool::Job {
  public:
    /**
     * @brief Render on @p pool, or on the calling thread alone when it is null.
     *
     * The pool is the host's and outlives every plan: threads are made once and
     * every epoch published afterwards renders on them. A null pool is not a
     * degraded mode but the same executor with one thread, which is what makes
     * "identical at every thread count" a claim about one code path.
     */
    explicit ParallelPlanExecutor(RenderThreadPool* pool = nullptr) : pool_(pool) {}
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
                                     const ParamTable* params = nullptr);

    /**
     * @brief Render one block into @p output. On the audio thread.
     *
     * No allocation, no locks, no waiting on anything but the block's own work.
     */
    void process(const PlanValues& values, const BlockInfo& block,
                 juce::AudioBuffer<float>& output);

    /// Threads a block is spread across.
    int numThreads() const {
        return pool_ != nullptr ? pool_->numThreads() : 1;
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
    /// Take ready ops until the block is finished. Runs on every thread of the
    /// pool at once, and on the audio thread.
    void takeWork() override;

    /// Render @p op, release the ops waiting on it, and hand back one of them
    /// for this thread to carry straight on with. A chain therefore never
    /// touches the ready set at all: the thread that ran an op runs its
    /// consumer, which is both the cheapest schedule and the warmest cache.
    OpId runOp(OpId op);

    void push(OpId op);
    OpId pop();

    /// Wait until no worker can be inside this job, so what it owns may be
    /// resized or destroyed. Off the audio thread.
    void letGoOfPool();

    const OpValue& valueOf(OpId op) const {
        return applyValues_ ? values_->ops[static_cast<std::size_t>(op)] : kUnityValue;
    }

    PlanExecutor core_;
    RenderThreadPool* pool_ = nullptr;

    /// Whether the pool has ever been handed this job. Until it has, no worker
    /// can be inside it and there is nothing to wait out.
    ///
    /// Written on the audio thread and read on the thread that retires an
    /// epoch, so it is an atomic rather than a bool. Relaxed is enough: what
    /// makes the read see the write is the plan swap that took the audio thread
    /// off this executor, which is an ordering this flag does not have to
    /// establish a second time.
    std::atomic<bool> handedToPool_{false};

    /// Null until a plan with a usable schedule has been prepared. Not the same
    /// question as core_.isPrepared(): a plan can bind cleanly and still arrive
    /// without the constants this needs.
    const RenderPlan* plan_ = nullptr;

    /// This block, as every thread reads it. Written before the ready set is
    /// seeded and read only by a thread that has taken an op out of it, so the
    /// publication is the same release the schedule already needs.
    BlockInfo block_;
    const PlanValues* values_ = nullptr;
    bool applyValues_ = false;
    juce::AudioBuffer<float>* output_ = nullptr;

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

    /// Producers each op is still waiting for. The plan's dependencyCounts,
    /// copied in at the top of every block.
    std::vector<std::atomic<std::uint16_t>> pending_;

    /// The ready set: a stack threaded through this array, one link per op.
    /// An op is pushed at most once a block, when its last producer finishes,
    /// so a link is written once and read once and no op can come back while
    /// another thread is looking at it.
    std::vector<std::atomic<OpId>> nextReady_;

    /// Top of the ready stack: an op and a tag, packed. The tag moves on every
    /// push and every pop and never resets, so a thread that read the top,
    /// stalled, and came back to find the same op there cannot mistake it for
    /// the one it was looking at. That is the whole of the ABA argument, and it
    /// is why the tag survives across blocks rather than starting again.
    alignas(64) std::atomic<std::uint64_t> readyTop_{kEmptyReadyStack};

    /// Ops still to finish this block. Zero is what "the block is done" means,
    /// and it is the only thing every thread agrees to wait for.
    alignas(64) std::atomic<int> remaining_{0};
};

}  // namespace magda::engine
