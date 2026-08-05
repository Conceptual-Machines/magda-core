#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

/**
 * @file RenderThreadPool.hpp
 * @brief The threads a block is rendered across.
 *
 * Deliberately not a general-purpose pool: it runs one job at a time, that job
 * is always "finish this block", and every thread runs the same job rather than
 * being handed tasks. Which op a thread picks up is the job's business, because
 * that is where the plan's dependency counts are; all this owns is the threads
 * and the moment they are woken.
 *
 * It outlives plans. Threads are made once, at whatever priority the audio
 * device deserves, and every plan published afterwards renders on the same
 * ones: a structural edit is not a reason to tear down and remake the threads
 * an audio callback is about to wait for.
 */

namespace magda::engine {

class RenderThreadPool {
  public:
    /**
     * @brief One block of one plan, as the threads see it.
     *
     * The pool never asks for an op. It asks every thread to take work until
     * there is none left, and the job decides what that means, which is what
     * keeps the schedule and the threads independent of each other.
     */
    class Job {
      public:
        virtual ~Job() = default;

        /**
         * @brief Take ready work until the block is finished.
         *
         * Runs on the calling thread and on every worker at once. Returns only
         * when the block is done, on every thread that entered it: a thread
         * that finds nothing to take has to wait for the ops still running
         * rather than leave, because they will make more work for it.
         */
        virtual void takeWork() = 0;
    };

    /**
     * @brief Start @p numWorkers threads besides the one that renders.
     *
     * Zero is a pool that renders on the caller alone, which is the same walk
     * a single-threaded executor makes and is what the tests compare against.
     *
     * @p realtime asks the OS for audio priority, which is what a worker the
     * callback is waiting on needs and what a worker in a test does not: a test
     * that spins up realtime threads competes with whatever else is on the
     * machine for no benefit.
     */
    explicit RenderThreadPool(int numWorkers, bool realtime = true);
    ~RenderThreadPool();

    RenderThreadPool(const RenderThreadPool&) = delete;
    RenderThreadPool& operator=(const RenderThreadPool&) = delete;

    /// Threads a block is spread across: the workers, plus whoever calls
    /// render().
    int numThreads() const {
        return static_cast<int>(workers_.size()) + 1;
    }

    /**
     * @brief Render one block of @p job. On the audio thread.
     *
     * Wakes the workers, takes work on this thread too, and returns when the
     * job says the block is finished. Nothing here allocates or locks; waking a
     * sleeping worker is the one system call, and it is the price of not
     * spinning a core per thread between callbacks.
     *
     * A worker may still be inside takeWork() when this returns. It has no ops
     * left to run, because the job would not have finished otherwise, but it
     * has not necessarily noticed yet.
     */
    void render(Job& job);

    /**
     * @brief Let go of @p job, so it can be destroyed. Off the audio thread.
     *
     * Returns once no worker can enter it again, waiting out any that is inside
     * it. That wait is why this is not audio-thread code: it is bounded by a
     * block, and it belongs on the thread that retires an epoch rather than on
     * the one rendering the next.
     *
     * The caller still owes the ordering that makes the wait finite: a job
     * being rendered right now is a job whose render() call has to return
     * first, which for a plan swap is what publishing does.
     */
    void release(Job& job);

  private:
    class Worker;

    /// What a worker runs when it wakes. Separate from render() because a
    /// worker has to announce itself before it reads the job pointer, so that
    /// release() cannot conclude the coast is clear while one is arriving.
    void takeWork();

    std::vector<std::unique_ptr<Worker>> workers_;

    /// The job the workers render, or null between blocks that are the last of
    /// their epoch. Sequentially consistent, and so is inJob_: the two are read
    /// and written against each other by release(), which is exactly the
    /// handshake that acquire/release alone does not settle.
    std::atomic<Job*> job_{nullptr};

    /// Workers that have committed to reading job_ and have not yet left it.
    std::atomic<int> inJob_{0};
};

}  // namespace magda::engine
