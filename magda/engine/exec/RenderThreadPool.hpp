#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

// lightweightsemaphore.h expects concurrentqueue.h ahead of it, for this macro.
#ifndef MOODYCAMEL_DELETE_FUNCTION
    #define MOODYCAMEL_DELETE_FUNCTION = delete
#endif
#include <moodycamel/lightweightsemaphore.h>

/**
 * @file RenderThreadPool.hpp
 * @brief The threads a block is rendered across.
 *
 * Tracktion's lock-free player's pool (ThreadPoolSemHybrid), copied (#2786): workers loop
 * taking ready work and wait when there is none, pausing, then yielding, then sleeping on a
 * lightweight semaphore. A block signals as many workers as it has ops ready. Which op a
 * thread takes is the job's business; all this owns is the threads.
 *
 * It outlives plans. Threads are made once and every plan published afterwards renders on the
 * same ones.
 */

namespace magda::engine {

class RenderThreadPool {
  public:
    /// One block of one plan, as the threads see it.
    class Job {
      public:
        virtual ~Job() = default;

        /// Take one ready op and the chain it releases, on a worker. False when none was ready.
        virtual bool takeOne() = 0;

        /// Take ready work on the thread that called render() until the block is finished.
        virtual void finishOnCaller() = 0;

      private:
        friend class RenderThreadPool;

        /// Workers inside this job, which release() waits out. Per job rather than per pool,
        /// so a session rendering back to back cannot keep a retiring epoch waiting.
        std::atomic<int> workersInside{0};
    };

    /// Workers a session on this machine renders with: the audio thread plus one per other core.
    /// MAGDA_RENDER_WORKERS overrides it, for measuring what a count costs.
    static int workersForThisMachine() {
        if (const auto* value = std::getenv("MAGDA_RENDER_WORKERS"))
            return std::max(0, juce::String(value).getIntValue());
        return std::max(0, juce::SystemStats::getNumCpus() - 1);
    }

    /**
     * @brief @p numWorkers threads, besides whoever calls render().
     *
     * @p realtime sets them the scheduling Tracktion's workers get (SCHED_RR at the top of its
     * range); false leaves them at an ordinary high priority, for an offline render.
     */
    explicit RenderThreadPool(int numWorkers, bool realtime = true);
    ~RenderThreadPool();

    RenderThreadPool(const RenderThreadPool&) = delete;
    RenderThreadPool& operator=(const RenderThreadPool&) = delete;

    /// Threads a block is spread across: the workers the device's workgroup
    /// allows, plus whoever calls render().
    int numThreads() const {
        return std::min(static_cast<int>(workers_.size()),
                        workerCap_.load(std::memory_order_relaxed)) +
               1;
    }

    /**
     * @brief Render @p job, signalling up to @p ready workers, and return when it is done.
     *
     * @p ready is how many ops the job has ready to take, which is how many workers
     * Tracktion's player signals. Audio thread.
     */
    void render(Job& job, int ready);

    /// Tracktion's core::pause(), twice: what a thread with nothing ready does before yielding.
    static void pause();

    /// Count @p count ops made ready or taken (negative), which is what a waiting worker reads.
    void noteQueued(int count) {
        queued_.fetch_add(count, std::memory_order_acq_rel);
    }

    /**
     * @brief Wait until no worker can be inside @p job.
     *
     * Off the audio thread, before whatever @p job points at is freed or reallocated. The block
     * being rendered right now is a job whose render() call has to return first.
     */
    void release(Job& job);

    /**
     * @brief What the device delivers.
     *
     * Joined to @p workgroup on macOS, which is what gives the workers the device thread's
     * scheduling, and no more workers are signalled than its recommended thread count minus
     * the device's own. Off the audio thread; the workers pick it up on their next round.
     */
    void configure(double blockSeconds, juce::AudioWorkgroup workgroup);

  private:
    class Worker;

    /// Take one op of the current job on @p worker, if any is ready.
    bool takeWork(Worker& worker);

    /// Pause, yield, then sleep, as Tracktion's hybrid pool does. Returns at once while work
    /// is queued.
    void wait(int& pauses);

    std::atomic<int> queued_{0};
    moodycamel::LightweightSemaphore semaphore_;

    juce::SpinLock workgroupLock_;
    juce::AudioWorkgroup workgroup_;
    std::atomic<std::uint64_t> workgroupGeneration_{0};

    /// Most workers a block may signal. Unbounded without a workgroup that recommends a count.
    std::atomic<int> workerCap_{std::numeric_limits<int>::max()};

    std::vector<std::unique_ptr<Worker>> workers_;

    /// The job the workers render, or null between epochs. Sequentially consistent, and so are
    /// the workers' arrival marks: release() reads the two against each other.
    std::atomic<Job*> job_{nullptr};
};

}  // namespace magda::engine
