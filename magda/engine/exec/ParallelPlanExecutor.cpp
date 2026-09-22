#include "exec/ParallelPlanExecutor.hpp"

#include <algorithm>
#include <limits>
#include <vector>

#include "exec/BlockProfile.hpp"

namespace magda::engine {
namespace {

/// Failed pops before a thread stops asking and lets the scheduler have the
/// core. The block is not over (something is still running, or nothing would be
/// waiting), so leaving is not an option; what this decides is only how hard a
/// thread with nothing to do leans on the ready set while it waits.
constexpr int kSpinsBeforeYield = 200;

/// How long a worker finds nothing to take before it leaves the block. A worker behind a long
/// op spun until the block ended: on a project whose instrument is most of the block that was
/// most of a core per worker (#2786).
constexpr std::chrono::microseconds kWorkerIdleBeforeLeaving{25};

/// Serial work a worker has to be able to take before waking it pays for the wake. Measured on
/// the parity bench (#2786): a 43-pad kit at 60 to 130 us of serial work renders slower with
/// every worker added, and a 340 us block is fastest with two.
constexpr std::chrono::microseconds kWorkPerWorker{150};

/// Blocks between the ones whose ops are timed, varied inside this range so the timing does not
/// land on the same musical phase every time.
constexpr int kTimedBlockEvery = 48;
constexpr int kTimedBlockJitter = 32;

/// Ops the calling thread renders between looks at the clock while it drains alone. It also
/// looks before every device, which is where the time goes.
constexpr std::size_t kOpsPerClockCheck = 4;

std::int64_t ticksIn(std::chrono::nanoseconds duration) {
    return static_cast<std::int64_t>(
        static_cast<double>(duration.count()) *
        static_cast<double>(juce::Time::getHighResolutionTicksPerSecond()) / 1.0e9);
}

constexpr std::uint64_t packReady(OpId op, std::uint32_t tag) {
    return (static_cast<std::uint64_t>(tag) << 32) | static_cast<std::uint32_t>(op);
}

constexpr OpId readyOp(std::uint64_t packed) {
    return static_cast<OpId>(static_cast<std::int32_t>(packed & 0xffffffffULL));
}

constexpr std::uint32_t readyTag(std::uint64_t packed) {
    return static_cast<std::uint32_t>(packed >> 32);
}

static_assert(packReady(INVALID_OP_ID, 0) == kEmptyReadyStack,
              "an executor that has not started a block must not look like one with op 0 ready");

/// The widest level of the plan's DAG, levelled from its baked schedule.
int widthOf(const RenderPlan& plan) {
    const auto numOps = plan.ops.size();
    std::vector<int> level(numOps, 0);
    std::vector<std::uint16_t> pending(plan.dependencyCounts.begin(), plan.dependencyCounts.end());
    std::vector<int> perLevel;
    std::vector<OpId> ready(plan.initialReadyOps.begin(), plan.initialReadyOps.end());

    while (!ready.empty()) {
        const auto index = static_cast<std::size_t>(ready.back());
        ready.pop_back();

        const auto depth = static_cast<std::size_t>(level[index]);
        if (depth >= perLevel.size())
            perLevel.resize(depth + 1, 0);
        ++perLevel[depth];

        for (auto edge = plan.consumerOffsets[index]; edge < plan.consumerOffsets[index + 1];
             ++edge) {
            const auto consumer = plan.consumerEdges[static_cast<std::size_t>(edge)];
            const auto at = static_cast<std::size_t>(consumer);
            level[at] = std::max(level[at], level[index] + 1);
            if (--pending[at] == 0)
                ready.push_back(consumer);
        }
    }

    return perLevel.empty() ? 1 : *std::max_element(perLevel.begin(), perLevel.end());
}

}  // namespace

ParallelPlanExecutor::ParallelPlanExecutor(RenderThreadPool* pool)
    : pool_(pool), idleBeforeLeavingTicks_(ticksIn(kWorkerIdleBeforeLeaving)) {
    setWorkPerWorker(kWorkPerWorker);
}

void ParallelPlanExecutor::setWorkPerWorker(std::chrono::nanoseconds work) {
    workPerWorkerTicks_ = ticksIn(work);
}

void ParallelPlanExecutor::assumeWork(std::chrono::nanoseconds work) {
    workEstimateTicks_.store(ticksIn(work), std::memory_order_relaxed);
}

int ParallelPlanExecutor::workersWorthWaking() const {
    // Every worker the plan can use until a block says otherwise, so the first block of a heavy
    // session is not the one that finds out it needed them.
    const auto estimate = workEstimateTicks_.load(std::memory_order_relaxed);
    if (estimate < 0 || workPerWorkerTicks_ <= 0)
        return std::numeric_limits<int>::max();
    return static_cast<int>(
        std::min<std::int64_t>(estimate / workPerWorkerTicks_, std::numeric_limits<int>::max()));
}

void ParallelPlanExecutor::noteWork(std::int64_t ticks) {
    // Up at once, down slowly: a block that needed more workers than it got is a deadline at
    // risk, and one that got more than it needed costs a few wake-ups.
    const auto estimate = workEstimateTicks_.load(std::memory_order_relaxed);
    const auto next = ticks >= estimate ? ticks : estimate - (estimate - ticks) / 16;
    workEstimateTicks_.store(next, std::memory_order_relaxed);
}

void ParallelPlanExecutor::letGoOfPool() {
    // A worker can still be inside the last block's takeWork(): render() returns
    // as soon as the block is finished, not as soon as everyone has noticed. It
    // is inside this object while it is, so nothing here may be freed or resized
    // until the pool says otherwise.
    //
    // Skipped for a job the pool has never been given, which is every executor
    // that has not rendered yet. Letting go waits out whatever is in a job right
    // now, and making a publish wait a block for an executor no thread could
    // possibly be inside would be a stall bought for nothing.
    if (pool_ != nullptr && handedToPool_.load(std::memory_order_relaxed))
        pool_->release(*this);
    handedToPool_.store(false, std::memory_order_relaxed);
}

ParallelPlanExecutor::~ParallelPlanExecutor() {
    letGoOfPool();
}

std::vector<std::string> ParallelPlanExecutor::prepare(const RenderPlan& plan,
                                                       const PlanBindings& bindings,
                                                       const RenderContext& context,
                                                       const ParallelPlanExecutor* previous,
                                                       const PlanValues* values) {
    // Everything below reallocates what a worker still finishing the last block
    // would read, so this comes first, the same as at destruction.
    letGoOfPool();

    plan_ = nullptr;
    outputOps_.clear();
    modSourceOps_.clear();
    insertSendOps_.clear();

    auto messages = core_.prepare(plan, bindings, context,
                                  previous != nullptr ? &previous->core_ : nullptr, values);
    if (!core_.isPrepared())
        return messages;

    // Checked against the topology rather than taken on trust, because every
    // way of being wrong here costs more than a wrong render: an op nobody
    // releases means a block that never finishes and a callback that spins for
    // ever, and an op released early is a race on a buffer another op is still
    // writing. Recomputing is the same linear pass that produced them, off the
    // audio thread, once per prepare.
    //
    // Refused rather than rebaked. A plan is immutable and shared, this is not
    // the place that owns what its schedule should be, and a plan whose
    // constants disagree with its ops did not come from the compiler.
    if (!carriesSchedule(plan)) {
        core_.reset();
        messages.emplace_back(
            "plan does not carry the schedule its topology implies: its dependency counts, "
            "consumer edges or initially-ready set are missing or disagree with its ops, so it "
            "cannot be scheduled");
        return messages;
    }

    const auto numOps = plan.ops.size();
    pending_ = std::vector<std::atomic<std::uint16_t>>(numOps);
    nextReady_ = std::vector<std::atomic<OpId>>(numOps);
    handOverReady_.clear();
    handOverReady_.reserve(numOps);
    opTicks_.assign(numOps, 0);
    ownedOp_.store(INVALID_OP_ID, std::memory_order_relaxed);
    blocksUntilTimed_ = 0;

    for (std::size_t i = 0; i < numOps; ++i) {
        if (plan.ops[i].kind == OpKind::Output)
            outputOps_.push_back(static_cast<OpId>(i));
        else if (plan.ops[i].kind == OpKind::ModSource)
            modSourceOps_.push_back(static_cast<OpId>(i));
        else if (plan.ops[i].kind == OpKind::InsertSend)
            insertSendOps_.push_back(static_cast<OpId>(i));
    }

    parallelism_ = widthOf(plan);
    plan_ = &plan;

    // The session it replaces is the best guess at this one's weight: most publishes are edits.
    if (previous != nullptr)
        workEstimateTicks_.store(previous->workEstimateTicks_.load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
    return messages;
}

void ParallelPlanExecutor::push(OpId op) {
    const auto index = static_cast<std::size_t>(op);
    auto top = readyTop_.load(std::memory_order_relaxed);
    std::uint64_t pushed = 0;

    do {
        nextReady_[index].store(readyOp(top), std::memory_order_relaxed);
        pushed = packReady(op, readyTag(top) + 1);
    } while (!readyTop_.compare_exchange_weak(top, pushed, std::memory_order_release,
                                              std::memory_order_relaxed));
}

OpId ParallelPlanExecutor::pop() {
    auto top = readyTop_.load(std::memory_order_acquire);

    for (;;) {
        const auto op = readyOp(top);
        if (op == INVALID_OP_ID)
            return INVALID_OP_ID;

        // Read under the tag: if the top is still the word this came from, the
        // link is the one whoever pushed it wrote, and if it is not, the swap
        // below fails and none of this counted.
        const auto next = nextReady_[static_cast<std::size_t>(op)].load(std::memory_order_relaxed);
        if (readyTop_.compare_exchange_weak(top, packReady(next, readyTag(top) + 1),
                                            std::memory_order_acquire, std::memory_order_acquire))
            return op;
    }
}

bool ParallelPlanExecutor::rendersInDrain(OpId op) const {
    // Two kinds are left for the thread that drove the block, and both for the
    // same reason: each shares a buffer with the others of its kind rather than
    // owning one. Output ops add into the one buffer reaching the hardware, and
    // modulation taps detect through one scratch buffer apiece on the executor
    // and on the runtime, so two taps running at once, which two listened-to
    // tracks with disjoint subgraphs make schedulable, would corrupt each
    // other's detection. Counted anyway, because what waits on what is the
    // plan's business and not this decision's; neither can stall the block,
    // because nothing consumes either.
    const auto kind = plan_->ops[static_cast<std::size_t>(op)].kind;
    return kind != OpKind::Output && kind != OpKind::ModSource && kind != OpKind::InsertSend &&
           !core_.inMidiPrefix(op);
}

std::size_t ParallelPlanExecutor::renderInPlanOrder(std::int64_t started) {
    // An estimate is only the blocks before this one. The block the transport starts on, or
    // the one that strikes a dozen idle instruments, finds out here and hands the rest over.
    const bool canHandOver = pool_ != nullptr && everyUsefulWorker() > 0 && workPerWorkerTicks_ > 0;

    for (std::size_t index = 0; index < plan_->ops.size(); ++index) {
        const auto kind = plan_->ops[index].kind;
        if (canHandOver && (kind == OpKind::Device || index % kOpsPerClockCheck == 0) &&
            juce::Time::getHighResolutionTicks() - started > workPerWorkerTicks_)
            return index;

        const auto op = static_cast<OpId>(index);
        if (!rendersInDrain(op))
            continue;

        renderTimed(op, true);
    }
    return plan_->ops.size();
}

void ParallelPlanExecutor::renderTimed(OpId op, bool onCaller) {
    const auto kind = plan_->ops[static_cast<std::size_t>(op)].kind;
    const ProfileScope timed([this, op, onCaller, kind](auto elapsed) {
        BlockProfile::addOp(kind, elapsed);
        if (op == ownedOp_.load(std::memory_order_relaxed))
            BlockProfile::addDevice(onCaller ? "owned op on the callback" : "owned op on a worker",
                                    elapsed);
    });

    if (!timingBlock_.load(std::memory_order_relaxed)) {
        core_.renderOp(op, valueOf(op), block_, *output_);
        return;
    }

    // One op, one thread, one entry: whichever thread runs an op writes its time, and the
    // block's completion is what hands it to the callback.
    const auto started = juce::Time::getHighResolutionTicks();
    core_.renderOp(op, valueOf(op), block_, *output_);
    opTicks_[static_cast<std::size_t>(op)] = juce::Time::getHighResolutionTicks() - started;
}

void ParallelPlanExecutor::chooseOwnedOp() {
    // The heaviest op of the timed block, kept on the callback thread until the next one says
    // otherwise, so a device that dominates the block renders on one thread every block.
    auto heaviest = INVALID_OP_ID;
    std::int64_t most = 0;
    for (std::size_t index = 0; index < opTicks_.size(); ++index)
        if (opTicks_[index] > most && rendersInDrain(static_cast<OpId>(index))) {
            most = opTicks_[index];
            heaviest = static_cast<OpId>(index);
        }
    ownedOp_.store(heaviest, std::memory_order_relaxed);
}

void ParallelPlanExecutor::beginTimedBlock() {
    const bool timing = --blocksUntilTimed_ <= 0;
    if (timing) {
        jitter_ = jitter_ * 1664525U + 1013904223U;
        blocksUntilTimed_ =
            kTimedBlockEvery + static_cast<int>((jitter_ >> 16) % kTimedBlockJitter);
    }
    timingBlock_.store(timing, std::memory_order_relaxed);
}

bool ParallelPlanExecutor::releaseToCaller(OpId op) {
    if (op != ownedOp_.load(std::memory_order_relaxed))
        return false;
    ownedReady_.store(op, std::memory_order_release);
    return true;
}

OpId ParallelPlanExecutor::runOp(OpId op, bool onCaller) {
    if (rendersInDrain(op))
        renderTimed(op, onCaller);

    OpId carryOn = INVALID_OP_ID;

    const auto first = plan_->consumerOffsets[static_cast<std::size_t>(op)];
    const auto last = plan_->consumerOffsets[static_cast<std::size_t>(op) + 1];
    for (auto edge = first; edge < last; ++edge) {
        const auto consumer = plan_->consumerEdges[static_cast<std::size_t>(edge)];

        // Acquire-release, not relaxed: this is where a consumer inherits
        // everything its other producers wrote, whichever threads they ran on.
        // The thread that takes the count to zero has seen all of it, and the
        // release on the push (or the hand-over below) passes it on.
        if (pending_[static_cast<std::size_t>(consumer)].fetch_sub(1, std::memory_order_acq_rel) !=
            1)
            continue;

        // The owned op waits for the callback thread whoever released it, a join included.
        if (releaseToCaller(consumer))
            continue;

        if (carryOn == INVALID_OP_ID)
            carryOn = consumer;
        else
            push(consumer);
    }

    return carryOn;
}

void ParallelPlanExecutor::takeWork() {
    const bool onCaller =
        juce::Thread::getCurrentThreadId() == callerThread_.load(std::memory_order_relaxed);
    int emptyPops = 0;
    std::int64_t idleSince = 0;

    while (remaining_.load(std::memory_order_acquire) > 0) {
        auto op = INVALID_OP_ID;
        if (onCaller && ownedReady_.load(std::memory_order_relaxed) != INVALID_OP_ID)
            op = ownedReady_.exchange(INVALID_OP_ID, std::memory_order_acquire);
        if (op == INVALID_OP_ID)
            op = pop();
        if (op == INVALID_OP_ID) {
            // A worker leaves once it has found nothing for a while. The callback thread never
            // does, so whatever is released after it goes is still taken.
            if (!onCaller) {
                const auto now = juce::Time::getHighResolutionTicks();
                if (idleSince == 0)
                    idleSince = now;
                else if (now - idleSince > idleBeforeLeavingTicks_)
                    return;
            }

            // Nothing ready, and the block is not over: something is running
            // that will release more. Spin for a while, because the wait is
            // usually shorter than the system call that avoids it, then stand
            // aside for whatever is holding the block up.
            if (++emptyPops >= kSpinsBeforeYield) {
                emptyPops = 0;
                juce::Thread::yield();
            }
            continue;
        }

        emptyPops = 0;
        idleSince = 0;
        const auto started = juce::Time::getHighResolutionTicks();
        int ran = 0;
        for (; op != INVALID_OP_ID; ++ran)
            op = runOp(op, onCaller);
        busyTicks_.fetch_add(juce::Time::getHighResolutionTicks() - started,
                             std::memory_order_relaxed);

        // Last, and after the pushes: a block is finished when this reaches zero,
        // and it must not reach zero while work a chain released is still on its
        // way into the ready set. The busy time goes first, so it is whole when
        // the block is.
        remaining_.fetch_sub(ran, std::memory_order_acq_rel);
    }
}

void ParallelPlanExecutor::startSchedule(std::size_t done) {
    const auto numOps = plan_->ops.size();
    for (std::size_t i = 0; i < numOps; ++i)
        pending_[i].store(plan_->dependencyCounts[i], std::memory_order_relaxed);

    // What this thread already rendered, in plan order and so a prefix closed under its own
    // producers, releases its consumers here, before any other thread can look.
    for (std::size_t op = 0; op < done; ++op)
        for (auto edge = plan_->consumerOffsets[op]; edge < plan_->consumerOffsets[op + 1];
             ++edge) {
            auto& count = pending_[static_cast<std::size_t>(
                plan_->consumerEdges[static_cast<std::size_t>(edge)])];
            count.store(static_cast<std::uint16_t>(count.load(std::memory_order_relaxed) - 1),
                        std::memory_order_relaxed);
        }

    // Emptied, but the tag carries on from wherever the last block left it: a
    // thread that read the top of the previous block's stack and has not looked
    // since must not find a word it recognises.
    readyTop_.store(
        packReady(INVALID_OP_ID, readyTag(readyTop_.load(std::memory_order_relaxed)) + 1),
        std::memory_order_relaxed);

    // Before the ready set is seeded, so nothing can finish an op and count it
    // against a total that has not been set yet.
    remaining_.store(static_cast<int>(numOps - done), std::memory_order_relaxed);
    busyTicks_.store(0, std::memory_order_relaxed);
    ownedReady_.store(INVALID_OP_ID, std::memory_order_relaxed);

    if (done == 0) {
        for (const auto op : plan_->initialReadyOps)
            if (!releaseToCaller(op))
                push(op);
        return;
    }

    // Gathered whole before any is published: a worker still leaving the last block takes what
    // is pushed, and the consumer it releases would otherwise be found ready and pushed again.
    handOverReady_.clear();
    for (auto op = done; op < numOps; ++op)
        if (pending_[op].load(std::memory_order_relaxed) == 0)
            handOverReady_.push_back(static_cast<OpId>(op));

    for (const auto op : handOverReady_)
        if (!releaseToCaller(op))
            push(op);
}

int ParallelPlanExecutor::everyUsefulWorker() const {
    return std::max(0, std::min(parallelism_ - 1, numThreads() - 1));
}

void ParallelPlanExecutor::process(const PlanValues& values, const BlockInfo& requestedBlock,
                                   juce::AudioBuffer<float>& output) {
    // This thread's share of the block (#2240). The workers set their own, in
    // RenderThreadPool::Worker, since the mode is per thread and a worker only
    // ever renders.
    const juce::ScopedNoDenormals noDenormals;
    const ProfileScope whole(
        [](auto elapsed) { BlockProfile::addPhase(BlockProfile::WholeBlock, elapsed); });

    const auto start = [&] {
        const ProfileScope timed(
            [](auto elapsed) { BlockProfile::addPhase(BlockProfile::BeginBlock, elapsed); });
        return core_.beginBlock(values, requestedBlock, output);
    }();
    if (!start.render || plan_ == nullptr)
        return;

    // Before any worker starts, and in this order. The MIDI a block has before
    // it has audio is rendered on this thread, so the notes in it reach the
    // modifiers of this block rather than the next one; then the parameters
    // resolve, and every op that could read one is about to become runnable.
    //
    // The prefix ops are still scheduled below, so their consumers are released
    // exactly as they would be otherwise; what the schedule skips is running
    // them a second time, the same way it skips the outputs it renders at the
    // end. Anything else would consume a live input queue twice.
    {
        const ProfileScope timed(
            [](auto elapsed) { BlockProfile::addPhase(BlockProfile::MidiPrefix, elapsed); });
        core_.renderMidiPrefix(values, start.block);
    }
    {
        const ProfileScope timed(
            [](auto elapsed) { BlockProfile::addPhase(BlockProfile::ResolveParameters, elapsed); });
        core_.resolveParameters(values, start.block);
    }

    block_ = start.block;
    values_ = &values;
    applyValues_ = start.applyValues;
    output_ = &output;

    // One worker per op that can run beside this thread's, none for a plan nothing in can run
    // beside, and no more than the work pays for: a wake-up costs more than a small op, and
    // every worker woken spins on the ready stack until the block drains.
    const auto workers = std::min(everyUsefulWorker(), workersWorthWaking());
    lastWorkers_ = pool_ != nullptr ? workers : 0;
    callerThread_.store(juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);
    beginTimedBlock();
    {
        const ProfileScope timed(
            [](auto elapsed) { BlockProfile::addPhase(BlockProfile::Drain, elapsed); });
        if (pool_ != nullptr && workers > 0) {
            startSchedule(0);
            handedToPool_.store(true, std::memory_order_relaxed);
            pool_->render(*this, workers);
            noteWork(busyTicks_.load(std::memory_order_relaxed));
        } else {
            const auto started = juce::Time::getHighResolutionTicks();
            const auto done = renderInPlanOrder(started);
            const auto alone = juce::Time::getHighResolutionTicks() - started;

            if (done < plan_->ops.size()) {
                lastWorkers_ = everyUsefulWorker();
                startSchedule(done);
                handedToPool_.store(true, std::memory_order_relaxed);
                pool_->render(*this, lastWorkers_);
                noteWork(alone + busyTicks_.load(std::memory_order_relaxed));
            } else {
                noteWork(alone);
            }
        }
    }
    if (timingBlock_.load(std::memory_order_relaxed))
        chooseOwnedOp();
    const ProfileScope tail(
        [](auto elapsed) { BlockProfile::addPhase(BlockProfile::SerialTail, elapsed); });

    // The graph has drained, so this thread is the only one with anything to
    // do. In plan order: the taps detect one at a time, and the sum reaching
    // the hardware is compiled like every other sum in the plan.
    for (const auto op : modSourceOps_)
        core_.renderOp(op, valueOf(op), block_, output);

    for (const auto op : insertSendOps_)
        core_.renderOp(op, valueOf(op), block_, output);

    for (const auto op : outputOps_)
        core_.renderOp(op, valueOf(op), block_, output);
}

}  // namespace magda::engine
