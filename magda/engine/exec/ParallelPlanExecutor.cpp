#include "exec/ParallelPlanExecutor.hpp"

#include <algorithm>
#include <vector>

#include "exec/BlockProfile.hpp"

namespace magda::engine {
namespace {
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

ParallelPlanExecutor::ParallelPlanExecutor(RenderThreadPool* pool) : pool_(pool) {}

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
    ready_ = std::make_unique<rigtorp::MPMCQueue<OpId>>(std::max<std::size_t>(1, numOps));

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
    return messages;
}

void ParallelPlanExecutor::enqueue(OpId op) {
    // Queued before it is counted, so a worker that sees the count finds the op.
    ready_->push(op);
    pool_->noteQueued(1);
}

void ParallelPlanExecutor::renderInPlanOrder() {
    for (std::size_t index = 0; index < plan_->ops.size(); ++index)
        if (const auto op = static_cast<OpId>(index); rendersInDrain(op))
            renderProfiled(op);
}

void ParallelPlanExecutor::renderProfiled(OpId op) {
    const auto kind = plan_->ops[static_cast<std::size_t>(op)].kind;
    const ProfileScope timed([kind](auto elapsed) { BlockProfile::addOp(kind, elapsed); });
    core_.renderOp(op, valueOf(op), block_, *output_);
}

void ParallelPlanExecutor::runChain(OpId op) {
    int ran = 0;
    while (op != INVALID_OP_ID) {
        if (rendersInDrain(op))
            renderProfiled(op);
        ++ran;

        const auto first = plan_->consumerOffsets[static_cast<std::size_t>(op)];
        const auto last = plan_->consumerOffsets[static_cast<std::size_t>(op) + 1];
        auto next = INVALID_OP_ID;
        for (auto edge = first; edge < last; ++edge) {
            const auto consumer = plan_->consumerEdges[static_cast<std::size_t>(edge)];

            // Acquire-release: the thread that takes the count to zero has seen everything its
            // other producers wrote, and the queue's release passes it on.
            if (pending_[static_cast<std::size_t>(consumer)].fetch_sub(
                    1, std::memory_order_acq_rel) != 1)
                continue;

            if (last - first == 1 || edge == last - 1) {
                next = consumer;
                break;
            }
            enqueue(consumer);
        }
        op = next;
    }

    // After the queueing: the block is finished when this reaches zero, and it must not while
    // work this chain released is still on its way into the queue.
    remaining_.fetch_sub(ran, std::memory_order_acq_rel);
}

bool ParallelPlanExecutor::takeOne() {
    auto op = INVALID_OP_ID;
    if (!ready_->try_pop(op))
        return false;

    pool_->noteQueued(-1);
    runChain(op);
    return true;
}

void ParallelPlanExecutor::finishOnCaller() {
    while (remaining_.load(std::memory_order_acquire) > 0)
        if (!takeOne())
            RenderThreadPool::pause();
}

int ParallelPlanExecutor::startSchedule() {
    const auto numOps = plan_->ops.size();
    for (std::size_t i = 0; i < numOps; ++i)
        pending_[i].store(plan_->dependencyCounts[i], std::memory_order_relaxed);

    // Before anything is queued, so no op can finish and count against a total not yet set.
    remaining_.store(static_cast<int>(numOps), std::memory_order_relaxed);

    for (const auto op : plan_->initialReadyOps)
        enqueue(op);
    return static_cast<int>(plan_->initialReadyOps.size());
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

    {
        const ProfileScope timed(
            [](auto elapsed) { BlockProfile::addPhase(BlockProfile::Drain, elapsed); });
        if (pool_ != nullptr && pool_->numThreads() > 1 && parallelism_ > 1) {
            const auto ready = startSchedule();
            handedToPool_.store(true, std::memory_order_relaxed);
            pool_->render(*this, ready);
        } else {
            renderInPlanOrder();
        }
    }
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
