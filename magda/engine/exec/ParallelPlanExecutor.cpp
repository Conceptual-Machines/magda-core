#include "exec/ParallelPlanExecutor.hpp"

namespace magda::engine {
namespace {

/// Failed pops before a thread stops asking and lets the scheduler have the
/// core. The block is not over (something is still running, or nothing would be
/// waiting), so leaving is not an option; what this decides is only how hard a
/// thread with nothing to do leans on the ready set while it waits.
constexpr int kSpinsBeforeYield = 200;

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

}  // namespace

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
                                                       const ParamTable* params) {
    // Everything below reallocates what a worker still finishing the last block
    // would read, so this comes first, the same as at destruction.
    letGoOfPool();

    plan_ = nullptr;
    outputOps_.clear();

    auto messages = core_.prepare(plan, bindings, context,
                                  previous != nullptr ? &previous->core_ : nullptr, params);
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
        messages.push_back(
            "plan does not carry the schedule its topology implies: its dependency counts, "
            "consumer edges or initially-ready set are missing or disagree with its ops, so it "
            "cannot be scheduled");
        return messages;
    }

    const auto numOps = plan.ops.size();
    pending_ = std::vector<std::atomic<std::uint16_t>>(numOps);
    nextReady_ = std::vector<std::atomic<OpId>>(numOps);

    for (std::size_t i = 0; i < numOps; ++i)
        if (plan.ops[i].kind == OpKind::Output)
            outputOps_.push_back(static_cast<OpId>(i));

    plan_ = &plan;
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

OpId ParallelPlanExecutor::runOp(OpId op) {
    // Output ops are the exception the whole schedule is written around: they
    // add into a buffer they all share, so they are left for the thread that
    // drove the block to run in plan order. Counted here anyway, because what
    // waits on what is the plan's business and not this decision's.
    if (plan_->ops[static_cast<std::size_t>(op)].kind != OpKind::Output)
        core_.renderOp(op, valueOf(op), block_, *output_);

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

        if (carryOn == INVALID_OP_ID)
            carryOn = consumer;
        else
            push(consumer);
    }

    // Last, and after the pushes: a block is finished when this reaches zero,
    // and it must not reach zero while work this op released is still on its
    // way into the ready set. An op carried on with is still counted, so a
    // thread holding one cannot be the thread that says the block is over.
    remaining_.fetch_sub(1, std::memory_order_acq_rel);
    return carryOn;
}

void ParallelPlanExecutor::takeWork() {
    int emptyPops = 0;

    while (remaining_.load(std::memory_order_acquire) > 0) {
        auto op = pop();
        if (op == INVALID_OP_ID) {
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
        while (op != INVALID_OP_ID)
            op = runOp(op);
    }
}

void ParallelPlanExecutor::process(const PlanValues& values, const BlockInfo& requestedBlock,
                                   juce::AudioBuffer<float>& output) {
    const auto start = core_.beginBlock(values, requestedBlock, output);
    if (!start.render || plan_ == nullptr)
        return;

    // Before any worker starts: a device reads its parameters, and every op
    // that could be one is about to become runnable.
    core_.resolveParameters(values, start.block.numSamples);

    block_ = start.block;
    values_ = &values;
    applyValues_ = start.applyValues;
    output_ = &output;

    const auto numOps = plan_->ops.size();
    for (std::size_t i = 0; i < numOps; ++i)
        pending_[i].store(plan_->dependencyCounts[i], std::memory_order_relaxed);

    // Emptied, but the tag carries on from wherever the last block left it: a
    // thread that read the top of the previous block's stack and has not looked
    // since must not find a word it recognises.
    readyTop_.store(
        packReady(INVALID_OP_ID, readyTag(readyTop_.load(std::memory_order_relaxed)) + 1),
        std::memory_order_relaxed);

    // Before the ready set is seeded, so nothing can finish an op and count it
    // against a total that has not been set yet.
    remaining_.store(static_cast<int>(numOps), std::memory_order_relaxed);

    for (const auto op : plan_->initialReadyOps)
        push(op);

    if (pool_ != nullptr) {
        handedToPool_.store(true, std::memory_order_relaxed);
        pool_->render(*this);
    } else {
        takeWork();
    }

    // The graph has drained, so this thread is the only one with anything to
    // do. In plan order: the sum reaching the hardware is compiled, like every
    // other sum in the plan.
    for (const auto op : outputOps_)
        core_.renderOp(op, valueOf(op), block_, output);
}

}  // namespace magda::engine
