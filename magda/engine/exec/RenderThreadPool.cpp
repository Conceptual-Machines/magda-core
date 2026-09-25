#include "exec/RenderThreadPool.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <thread>

#include "exec/BlockProfile.hpp"

#if !JUCE_ARM
    #include <immintrin.h>
#endif

#if JUCE_WINDOWS
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sched.h>
#endif

namespace magda::engine {

namespace {

/// Tracktion's setThreadPriority(thread, 10) for the calling thread.
void setTracktionWorkerPriority() {
#if JUCE_WINDOWS
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    sched_param param{};
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);
#endif
}

}  // namespace

class RenderThreadPool::Worker final : public juce::Thread {
  public:
    Worker(RenderThreadPool& pool, int index, bool realtime)
        : juce::Thread("MAGDA render " + juce::String(index)), pool_(pool), realtime_(realtime) {}

    void run() override {
        // For the thread rather than per job: flush-to-zero is a CPU mode this
        // thread carries, and a worker does nothing but render (#2240).
        const juce::ScopedNoDenormals noDenormals;

        if (realtime_)
            setTracktionWorkerPriority();

        // On this thread for as long as it runs: a token has to die on the thread it joined.
        juce::WorkgroupToken token;
        int pauses = 0;

        while (!threadShouldExit()) {
            joinWorkgroupIfChanged(token);

            if (pool_.takeWork(*this)) {
                if (BlockProfile::enabled())
                    BlockProfile::count(BlockProfile::ChainsOnWorkers);
                wokenEmpty_ = false;
            } else {
                if (wokenEmpty_ && BlockProfile::enabled())
                    BlockProfile::count(BlockProfile::WakesWithoutWork);
                wokenEmpty_ = pool_.wait(pauses);
            }
        }
    }

    /**
     * @brief Where this worker is between reading the job and counting itself inside it.
     *
     * Ticked twice per arrival, so odd means inside that window and the value itself says
     * which arrival it is. release() waits for a mark it saw odd to move, which a later
     * arrival cannot hold open the way a shared count could.
     */
    std::atomic<std::uint64_t> arrival{0};

  private:
    void joinWorkgroupIfChanged(juce::WorkgroupToken& token) {
        const auto generation = pool_.workgroupGeneration_.load(std::memory_order_acquire);
        if (generation == workgroupSeen_)
            return;

        juce::AudioWorkgroup workgroup;
        {
            const juce::SpinLock::ScopedLockType lock(pool_.workgroupLock_);
            workgroup = pool_.workgroup_;
        }
        workgroup.join(token);
        workgroupSeen_ = generation;
    }

    RenderThreadPool& pool_;
    bool realtime_ = true;
    std::uint64_t workgroupSeen_ = 0;
    /// Just back from the semaphore, for the profile's count of wakes that found nothing.
    bool wokenEmpty_ = false;
};

RenderThreadPool::RenderThreadPool(int numWorkers, bool realtime) {
    workers_.reserve(static_cast<std::size_t>(std::max(0, numWorkers)));
    for (int index = 0; index < numWorkers; ++index) {
        auto worker = std::make_unique<Worker>(*this, index, realtime);
        worker->startThread(juce::Thread::Priority::high);
        workers_.push_back(std::move(worker));
    }
}

RenderThreadPool::~RenderThreadPool() {
    // Nothing is rendering: a pool is destroyed after the executors that used it.
    for (auto& worker : workers_)
        worker->signalThreadShouldExit();
    semaphore_.signal(static_cast<int>(workers_.size()));
    for (auto& worker : workers_)
        worker->stopThread(2000);
}

void RenderThreadPool::render(Job& job, int ready) {
    job_.store(&job, std::memory_order_seq_cst);

    const auto signalled = std::clamp(ready, 0, numThreads() - 1);
    if (BlockProfile::enabled())
        BlockProfile::count(BlockProfile::WorkersSignalled, signalled);
    if (signalled > 0)
        semaphore_.signal(signalled);

    job.finishOnCaller();
}

void RenderThreadPool::pause() {
    for (int i = 0; i < 2; ++i) {
#if JUCE_ARM && !JUCE_MSVC
        __asm__ __volatile__("yield");
#elif JUCE_ARM
        __yield();
#else
        _mm_pause();
#endif
    }
}

bool RenderThreadPool::wait(int& pauses) {
    if (queued_.load(std::memory_order_acquire) > 0) {
        pauses = 0;
        return false;
    }

    ++pauses;
    if (pauses < 25) {
        pause();
    } else if (pauses < 50) {
        std::this_thread::yield();
    } else {
        pauses = 0;
        if (BlockProfile::enabled())
            BlockProfile::count(BlockProfile::WorkerSleeps);
        semaphore_.wait();
        return true;
    }
    return false;
}

bool RenderThreadPool::takeWork(Worker& worker) {
    if (queued_.load(std::memory_order_acquire) <= 0)
        return false;

    // Arrival is marked before the job is read, so release() cannot look at an idle-seeming
    // pool and conclude that a worker on its way in will not arrive.
    worker.arrival.fetch_add(1, std::memory_order_seq_cst);

    auto* job = job_.load(std::memory_order_seq_cst);
    if (job != nullptr)
        job->workersInside.fetch_add(1, std::memory_order_seq_cst);

    worker.arrival.fetch_add(1, std::memory_order_seq_cst);

    if (job == nullptr)
        return false;

    const auto took = job->takeOne();
    job->workersInside.fetch_sub(1, std::memory_order_seq_cst);
    return took;
}

void RenderThreadPool::configure(double blockSeconds, juce::AudioWorkgroup workgroup) {
    juce::ignoreUnused(blockSeconds);

    // The recommendation counts the device's own thread, which renders too.
    const auto recommended = static_cast<int>(workgroup.getMaxParallelThreadCount());
    workerCap_.store(recommended > 0 ? recommended - 1 : std::numeric_limits<int>::max(),
                     std::memory_order_relaxed);

    {
        const juce::SpinLock::ScopedLockType lock(workgroupLock_);
        workgroup_ = std::move(workgroup);
    }
    workgroupGeneration_.fetch_add(1, std::memory_order_release);
}

// Modelled in specs/tla/plan_swap, with takeWork() and render().
void RenderThreadPool::release(Job& job) {
    // Only this job: another one may have been published since.
    auto* expected = &job;
    job_.compare_exchange_strong(expected, nullptr, std::memory_order_seq_cst);

    // First the workers that read the job pointer before it was taken away and have not yet
    // counted themselves against it, each by the mark it was on; then the ones inside.
    for (auto& worker : workers_) {
        const auto marked = worker->arrival.load(std::memory_order_seq_cst);
        if (marked % 2 == 0)
            continue;

        while (worker->arrival.load(std::memory_order_seq_cst) == marked)
            juce::Thread::yield();
    }

    while (job.workersInside.load(std::memory_order_seq_cst) != 0)
        juce::Thread::yield();
}

}  // namespace magda::engine
