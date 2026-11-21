// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exec/ParallelPlanExecutor.hpp"

#include <algorithm>
#include <vector>

#include "exec/BlockProfile.hpp"
#include "exec/RenderLoad.hpp"
#include "exec/RenderProbe.hpp"

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
    callback_.letGoOfPool();
    ahead_.letGoOfPool();
}

void ParallelPlanExecutor::Schedule::letGoOfPool() {
    // A worker can still be inside the last block's takeWork(): render() returns
    // as soon as the block is finished, not as soon as everyone has noticed. It
    // is inside this object while it is, so nothing here may be freed or resized
    // until the pool says otherwise.
    //
    // Skipped for a job the pool has never been given, which is every executor
    // that has not rendered yet. Letting go waits out whatever is in a job right
    // now, and making a publish wait a block for an executor no thread could
    // possibly be inside would be a stall bought for nothing.
    if (owner_.pool_ != nullptr && handedToPool_.load(std::memory_order_relaxed))
        owner_.pool_->release(*this);
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
    core_.setCallbackRenderer(nullptr);
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
    callback_.size(numOps);
    ahead_.size(numOps);

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
    core_.setCallbackRenderer(this);
    return messages;
}

void ParallelPlanExecutor::Schedule::size(std::size_t numOps) {
    pending_ = std::vector<std::atomic<std::uint16_t>>(numOps);
    ready_ = std::make_unique<rigtorp::MPMCQueue<OpId>>(std::max<std::size_t>(1, numOps));
}

void ParallelPlanExecutor::Schedule::enqueue(OpId op) {
    // Queued before it is counted, so a worker that sees the count finds the op.
    ready_->push(op);
    if (owner_.pool_ != nullptr)
        owner_.pool_->noteQueued(1);
}

bool ParallelPlanExecutor::Schedule::givenUp() {
    if (!abandonable_)
        return false;
    if (abandoned_.load(std::memory_order_relaxed))
        return true;
    if (!owner_.core_.callbackWaiting())
        return false;
    // Sticky: the callback may stop waiting, and a block missing an op must not be published.
    abandoned_.store(true, std::memory_order_relaxed);
    return true;
}

void ParallelPlanExecutor::Schedule::renderInPlanOrder() {
    for (std::size_t index = 0; index < owner_.plan_->ops.size(); ++index) {
        const auto op = static_cast<OpId>(index);
        if (!owner_.rendersInDrain(op, side_))
            continue;
        if (givenUp())
            return;
        renderProfiled(op);
    }
}

void ParallelPlanExecutor::Schedule::renderProfiled(OpId op) {
    const render_probe::OpTimer probed(owner_.plan_->ops[static_cast<std::size_t>(op)].key.trackId,
                                       slot_ != RenderThreadPool::Slot::callback);
    // The profile is the callback's (BlockProfile); an ahead op counts as ahead load wherever
    // it runs.
    if (slot_ != RenderThreadPool::Slot::callback) {
        const RenderLoad::AheadScope ahead;
        owner_.core_.renderOp(op, valueOf(op), block_, *output_);
        return;
    }
    const auto kind = owner_.plan_->ops[static_cast<std::size_t>(op)].kind;
    const ProfileScope timed([kind](auto elapsed) { BlockProfile::addOp(kind, elapsed); });
    owner_.core_.renderOp(op, valueOf(op), block_, *output_);
}

void ParallelPlanExecutor::Schedule::runChain(OpId op) {
    const auto& plan = *owner_.plan_;
    int ran = 0;
    while (op != INVALID_OP_ID) {
        // An op given up still releases its consumers, so the block drains and can be abandoned.
        if (owner_.rendersInDrain(op, side_) && !givenUp())
            renderProfiled(op);
        ++ran;

        const auto first = plan.consumerOffsets[static_cast<std::size_t>(op)];
        const auto last = plan.consumerOffsets[static_cast<std::size_t>(op) + 1];
        auto next = INVALID_OP_ID;
        for (auto edge = first; edge < last; ++edge) {
            const auto consumer = plan.consumerEdges[static_cast<std::size_t>(edge)];

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
    // By the slot, which never changes: once the count is zero the caller may begin the next block.
    if (remaining_.fetch_sub(ran, std::memory_order_acq_rel) == ran &&
        slot_ == RenderThreadPool::Slot::ahead)
        finishAheadBlock();
}

void ParallelPlanExecutor::Schedule::finishAheadBlock() {
    const RenderLoad::AheadScope ahead;
    bool rendered = !abandoned_.load(std::memory_order_relaxed);
    for (const auto* ops : {&owner_.modSourceOps_, &owner_.insertSendOps_, &owner_.outputOps_})
        for (const auto op : *ops) {
            if (!rendered || givenUp()) {
                rendered = false;
                break;
            }
            renderTail(op);
        }
    owner_.core_.finishAheadBlock(rendered);
    published_ = rendered;
    // Last: the caller may start the next block, which reuses everything above, once it sees this.
    finished_.store(true, std::memory_order_release);
}

bool ParallelPlanExecutor::Schedule::takeOne() {
    auto op = INVALID_OP_ID;
    if (!ready_->try_pop(op))
        return false;

    if (owner_.pool_ != nullptr)
        owner_.pool_->noteQueued(-1);
    runChain(op);
    return true;
}

void ParallelPlanExecutor::Schedule::finishOnCaller() {
    while (remaining_.load(std::memory_order_acquire) > 0)
        if (!takeOne())
            RenderThreadPool::pause();
    while (abandonable_ && !finished_.load(std::memory_order_acquire))
        RenderThreadPool::pause();
}

int ParallelPlanExecutor::Schedule::startSchedule() {
    const auto& plan = *owner_.plan_;
    const auto numOps = plan.ops.size();
    for (std::size_t i = 0; i < numOps; ++i)
        pending_[i].store(plan.dependencyCounts[i], std::memory_order_relaxed);

    // Before anything is queued, so no op can finish and count against a total not yet set.
    remaining_.store(static_cast<int>(numOps), std::memory_order_relaxed);
    finished_.store(false, std::memory_order_relaxed);
    if (numOps == 0 && abandonable_)
        finishAheadBlock();

    // A worker is woken only for an op it would render: one this thread renders itself
    // (the MIDI prefix, outputs, taps) only releases its consumers, and a worker woken for it
    // spins through its wait for nothing.
    int renderable = 0;
    for (const auto op : plan.initialReadyOps) {
        enqueue(op);
        if (owner_.rendersInDrain(op, side_))
            ++renderable;
    }

    if (BlockProfile::enabled() && slot_ == RenderThreadPool::Slot::callback) {
        const auto ready = static_cast<int>(plan.initialReadyOps.size());
        BlockProfile::count(BlockProfile::InitialReady, ready);
        BlockProfile::count(BlockProfile::InitialReadyCallerOnly, ready - renderable);
    }
    return renderable;
}

bool ParallelPlanExecutor::rendersInDrain(OpId op, int side) const {
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
           !core_.inMidiPrefix(op) && (side < 0 || core_.sideOf(op) == side);
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

    callback_.render(values, start.block, output, start.applyValues, -1, false);
}

void ParallelPlanExecutor::renderWhole(const PlanValues& values, const BlockInfo& block,
                                       juce::AudioBuffer<float>& output) {
    process(values, block, output);
}

void ParallelPlanExecutor::renderCallbackSide(const PlanValues& values, const BlockInfo& block,
                                              juce::AudioBuffer<float>& output, bool applyValues) {
    callback_.render(values, block, output, applyValues, 1, false);
}

bool ParallelPlanExecutor::renderAheadSide(const PlanValues& values, const BlockInfo& block,
                                           juce::AudioBuffer<float>& output, bool applyValues) {
    return ahead_.render(values, block, output, applyValues, 0, true);
}

void ParallelPlanExecutor::Schedule::renderTail(OpId op) {
    if (side_ < 0 || owner_.core_.sideOf(op) == side_)
        owner_.core_.renderOp(op, valueOf(op), block_, *output_);
}

bool ParallelPlanExecutor::Schedule::render(const PlanValues& values, const BlockInfo& block,
                                            juce::AudioBuffer<float>& output, bool applyValues,
                                            int side, bool abandonable) {
    block_ = block;
    values_ = &values;
    applyValues_ = applyValues;
    output_ = &output;
    side_ = side;
    abandonable_ = abandonable;
    abandoned_.store(false, std::memory_order_relaxed);

    const auto profiled = slot_ == RenderThreadPool::Slot::callback;
    {
        const ProfileScope timed([profiled](auto elapsed) {
            if (profiled)
                BlockProfile::addPhase(BlockProfile::Drain, elapsed);
        });
        auto* pool = owner_.pool_;
        if (pool != nullptr && pool->numThreads() > 1 && owner_.parallelism_ > 1) {
            const auto ready = startSchedule();
            handedToPool_.store(true, std::memory_order_relaxed);
            pool->render(*this, ready, slot_);
        } else if (abandonable) {
            // Queued even alone, so the callback can help it finish (helpAheadSide).
            startSchedule();
            finishOnCaller();
        } else {
            renderInPlanOrder();
        }
    }
    if (abandonable)
        return published_;
    if (abandoned_.load(std::memory_order_relaxed))
        return false;

    const ProfileScope timed([profiled](auto elapsed) {
        if (profiled)
            BlockProfile::addPhase(BlockProfile::SerialTail, elapsed);
    });

    // The graph has drained, so this thread is the only one with anything to
    // do. In plan order: the taps detect one at a time, and the sum reaching
    // the hardware is compiled like every other sum in the plan.
    for (const auto* ops : {&owner_.modSourceOps_, &owner_.insertSendOps_, &owner_.outputOps_})
        for (const auto op : *ops) {
            if (givenUp())
                return false;
            renderTail(op);
        }
    return true;
}

}  // namespace magda::engine
