#include "exec/RenderThreadPool.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>

namespace magda::engine {

/**
 * @brief One worker: sleep, take work, sleep again.
 *
 * juce::Thread's own wait/notify rather than an event of its own, because it is
 * the same latching primitive and stopThread() already knows how to wake it. A
 * notify that arrives while the worker is still working is not lost: it is
 * remembered, and the next wait returns at once. That matters, because a worker
 * that is late leaving one block is exactly the one being woken for the next.
 */
class RenderThreadPool::Worker final : public juce::Thread {
  public:
    Worker(RenderThreadPool& pool, int index)
        : juce::Thread("MAGDA render " + juce::String(index)), pool_(pool) {}

    void run() override {
        // For the thread rather than per job: flush-to-zero is a CPU mode this
        // thread carries, and a worker does nothing but render (#2240).
        const juce::ScopedNoDenormals noDenormals;

        while (!threadShouldExit()) {
            joinWorkgroupIfChanged();

            if (!awaitBlock())
                return;

            seen_ = wanted.load(std::memory_order_seq_cst);
            pool_.takeWork(*this);
        }
    }

    /// Whether this worker is on its wait, which render() reads before paying a notify.
    std::atomic<bool> sleeping{false};

    /// The block this worker is wanted for. Only render() moves it, and only for the workers
    /// the plan has work for, so an idle worker spins for nobody.
    std::atomic<std::uint64_t> wanted{0};

    /**
     * @brief Where this worker is between waking and reaching a job's count.
     *
     * Ticked twice per arrival, so odd means inside that window and the value
     * itself says which arrival it is. That is what release() needs and what a
     * count could not give it: a count has to reach zero, and on a pool
     * rendering block after block the arrivals of later ones keep it off zero,
     * so a retirement waits on a window that reopens rather than on one that
     * closes. A mark is per worker and monotonic, so a worker that has moved on
     * has moved on whatever anyone starts afterwards.
     */
    std::atomic<std::uint64_t> arrival{0};

  private:
    /// Spin until a block arrives or the spin budget runs out, then sleep for one. False when
    /// the thread should exit instead.
    bool awaitBlock() {
        const auto spin =
            std::chrono::nanoseconds(pool_.spinNanos_.load(std::memory_order_relaxed));
        const auto deadline = std::chrono::steady_clock::now() + spin;

        for (;;) {
            if (threadShouldExit())
                return false;
            if (wanted.load(std::memory_order_seq_cst) != seen_)
                return true;
            if (std::chrono::steady_clock::now() >= deadline)
                break;
            juce::Thread::yield();
        }

        // Sleeping is announced before the mark is read again, and render() moves the mark
        // before it reads the announcement, so one of the two always sees the other.
        sleeping.store(true, std::memory_order_seq_cst);
        if (wanted.load(std::memory_order_seq_cst) == seen_)
            wait(-1);
        sleeping.store(false, std::memory_order_seq_cst);
        return !threadShouldExit();
    }

    void joinWorkgroupIfChanged() {
        const auto generation = pool_.workgroupGeneration_.load(std::memory_order_acquire);
        if (generation == workgroupSeen_)
            return;

        juce::AudioWorkgroup workgroup;
        {
            const juce::SpinLock::ScopedLockType lock(pool_.workgroupLock_);
            workgroup = pool_.workgroup_;
        }
        workgroup.join(token_);
        workgroupSeen_ = generation;
    }

    RenderThreadPool& pool_;
    std::uint64_t seen_ = 0;
    std::uint64_t workgroupSeen_ = 0;
    juce::WorkgroupToken token_;
};

RenderThreadPool::RenderThreadPool(int numWorkers, bool realtime) {
    workers_.reserve(static_cast<std::size_t>(std::max(0, numWorkers)));
    for (int index = 0; index < numWorkers; ++index) {
        auto worker = std::make_unique<Worker>(*this, index);

        // Priority alone, with no period: how long a block takes and how often
        // one arrives are the audio device's to say, and this pool is made
        // before any plan has been prepared for a device. What the OS is being
        // told is that these threads run with the callback rather than behind
        // it, which is the part that does not change per plan.
        //
        // Realtime scheduling is a request, and on a machine that does not
        // grant it (a Linux box with no rtprio limit raised, a container) it is
        // refused. A worker that was never started is one the audio thread
        // waits on for nothing, so the refusal falls back to the highest
        // ordinary priority rather than leaving the thread unstarted.
        if (!realtime ||
            !worker->startRealtimeThread(juce::Thread::RealtimeOptions{}.withPriority(9)))
            worker->startThread(juce::Thread::Priority::high);

        workers_.push_back(std::move(worker));
    }
}

RenderThreadPool::~RenderThreadPool() {
    // Nothing is rendering: a pool is destroyed after the executors that used
    // it, which is what release() is for. The wait is for a worker that is
    // between blocks, and it has nothing to finish.
    for (auto& worker : workers_)
        worker->signalThreadShouldExit();
    for (auto& worker : workers_)
        worker->stopThread(2000);
}

void RenderThreadPool::render(Job& job, int workers) {
    job_.store(&job, std::memory_order_seq_cst);
    const auto generation = generation_.fetch_add(1, std::memory_order_seq_cst) + 1;

    // A spinning worker sees its mark move for itself; only a sleeping one costs a notify.
    const auto woken = std::min(static_cast<std::size_t>(std::max(0, workers)), workers_.size());
    for (std::size_t index = 0; index < woken; ++index) {
        auto& worker = *workers_[index];
        worker.wanted.store(generation, std::memory_order_seq_cst);
        if (worker.sleeping.load(std::memory_order_seq_cst))
            worker.notify();
    }

    // The caller is a worker too, and on a pool with none it is the only one.
    // Its return is what says the block is finished, which is why this is not
    // a barrier: the workers are still leaving, and there is nothing left for
    // them to do.
    job.takeWork();
}

void RenderThreadPool::takeWork(Worker& worker) {
    // Arrival is marked before the job is read, so release() cannot look at an
    // idle-seeming pool and conclude that a worker on its way in will not
    // arrive. Then the worker moves itself onto the job's own count and leaves
    // the arrival window, which is what makes a retirement wait for its own
    // workers rather than for whoever is rendering now.
    worker.arrival.fetch_add(1, std::memory_order_seq_cst);

    auto* job = job_.load(std::memory_order_seq_cst);
    if (job != nullptr)
        job->workersInside.fetch_add(1, std::memory_order_seq_cst);

    worker.arrival.fetch_add(1, std::memory_order_seq_cst);

    if (job == nullptr)
        return;

    job->takeWork();
    job->workersInside.fetch_sub(1, std::memory_order_seq_cst);
}

void RenderThreadPool::configure(double blockSeconds, juce::AudioWorkgroup workgroup) {
    // Long enough to catch a block that is already on its way, short against any period: a
    // worker spinning between blocks is a core burnt for the whole gap.
    juce::ignoreUnused(blockSeconds);
    spinNanos_.store(50'000, std::memory_order_relaxed);

    {
        const juce::SpinLock::ScopedLockType lock(workgroupLock_);
        workgroup_ = std::move(workgroup);
    }
    workgroupGeneration_.fetch_add(1, std::memory_order_release);
}

void RenderThreadPool::release(Job& job) {
    // Only this job: another one may have been published since, and taking that
    // one down with it would silence an epoch that is rendering.
    auto* expected = &job;
    job_.compare_exchange_strong(expected, nullptr, std::memory_order_seq_cst);

    // Two waits, and they are for different things.
    //
    // The first is for workers that read the job pointer before it was taken
    // away and have not yet counted themselves against it. Each is waited for
    // by name: the mark this worker was on when it was looked at, and then that
    // mark moving. Only a load and an increment live inside that window, so it
    // is over as soon as the thread holding it gets a core, and an arrival that
    // starts afterwards moves the mark rather than holding the wait open. That
    // is the whole difference from asking a count to reach zero, which on a
    // pool rendering block after block it need never do.
    //
    // The second is for workers actually inside this job, which is bounded by
    // the block they are finishing.
    //
    // In this order, because a worker still arriving is one whose increment
    // against the job has not happened yet.
    for (auto& worker : workers_) {
        const auto marked = worker->arrival.load(std::memory_order_seq_cst);
        if (marked % 2 == 0)
            continue;  // between arrivals, and the next one is after the swap

        while (worker->arrival.load(std::memory_order_seq_cst) == marked)
            juce::Thread::yield();
    }

    while (job.workersInside.load(std::memory_order_seq_cst) != 0)
        juce::Thread::yield();
}

}  // namespace magda::engine
