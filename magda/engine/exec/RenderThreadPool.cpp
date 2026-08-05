#include "exec/RenderThreadPool.hpp"

#include <algorithm>

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
        while (!threadShouldExit()) {
            wait(-1);
            if (threadShouldExit())
                return;
            pool_.takeWork();
        }
    }

  private:
    RenderThreadPool& pool_;
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

void RenderThreadPool::render(Job& job) {
    job_.store(&job, std::memory_order_seq_cst);

    for (auto& worker : workers_)
        worker->notify();

    // The caller is a worker too, and on a pool with none it is the only one.
    // Its return is what says the block is finished, which is why this is not
    // a barrier: the workers are still leaving, and there is nothing left for
    // them to do.
    job.takeWork();
}

void RenderThreadPool::takeWork() {
    // Announced before the job is read, so release() cannot see zero here and
    // conclude that a worker on its way in will not arrive.
    inJob_.fetch_add(1, std::memory_order_seq_cst);

    if (auto* job = job_.load(std::memory_order_seq_cst))
        job->takeWork();

    inJob_.fetch_sub(1, std::memory_order_seq_cst);
}

void RenderThreadPool::release(Job& job) {
    // Only this job: another one may have been published since, and taking that
    // one down with it would silence an epoch that is rendering.
    auto* expected = &job;
    job_.compare_exchange_strong(expected, nullptr, std::memory_order_seq_cst);

    // Anything inside a job now is either this one, which has to be waited out,
    // or a newer one, which is a block and will end. Either way this is a wait
    // measured in one callback, on a thread that is allowed to wait.
    while (inJob_.load(std::memory_order_seq_cst) != 0)
        juce::Thread::yield();
}

}  // namespace magda::engine
