// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "exec/RenderThreadPool.hpp"

/// @file The pool rendering the callback's job and a job rendered ahead at once (#3360).

using namespace magda::engine;

namespace {

/// Independent ops that each spin for a while, counting where and how often they ran.
class CountingJob final : public RenderThreadPool::Job {
  public:
    CountingJob(int ops, std::chrono::microseconds work) : runs_(ops), work_(work) {}

    /// Renders one block in @p slot on the calling thread, which is the job's caller.
    void renderBlock(RenderThreadPool& pool, RenderThreadPool::Slot slot) {
        const auto ops = static_cast<int>(runs_.size());
        for (auto& runs : runs_)
            runs.store(0, std::memory_order_relaxed);
        caller_ = std::this_thread::get_id();
        pool_ = &pool;
        remaining_.store(ops, std::memory_order_relaxed);
        available_.store(ops, std::memory_order_release);
        pool.noteQueued(ops);
        pool.render(*this, ops, slot);

        for (const auto& runs : runs_)
            if (runs.load(std::memory_order_relaxed) != 1)
                wrongCounts.fetch_add(1, std::memory_order_relaxed);
    }

    bool takeOne() override {
        auto available = available_.load(std::memory_order_acquire);
        while (available > 0 && !available_.compare_exchange_weak(available, available - 1,
                                                                  std::memory_order_acq_rel)) {
        }
        if (available <= 0)
            return false;

        pool_->noteQueued(-1);
        const auto until = std::chrono::steady_clock::now() + work_;
        while (std::chrono::steady_clock::now() < until) {
        }
        runs_[static_cast<std::size_t>(available - 1)].fetch_add(1, std::memory_order_relaxed);
        if (std::this_thread::get_id() != caller_)
            offCaller.fetch_add(1, std::memory_order_relaxed);
        remaining_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    void finishOnCaller() override {
        while (remaining_.load(std::memory_order_acquire) > 0)
            if (!takeOne())
                RenderThreadPool::pause();
    }

    std::atomic<int> wrongCounts{0};
    std::atomic<int> offCaller{0};

  private:
    std::vector<std::atomic<int>> runs_;
    std::chrono::microseconds work_;
    std::atomic<int> available_{0};
    std::atomic<int> remaining_{0};
    std::thread::id caller_;
    RenderThreadPool* pool_ = nullptr;
};

}  // namespace

TEST_CASE("A job rendered ahead renders beside the callback's, every op once a block",
          "[engine][exec][parallel][3360]") {
    RenderThreadPool pool(3, false);
    CountingJob callback(12, std::chrono::microseconds(5));
    CountingJob ahead(12, std::chrono::microseconds(5));

    std::thread callbackThread([&] {
        for (int block = 0; block < 300; ++block)
            callback.renderBlock(pool, RenderThreadPool::Slot::callback);
    });
    std::thread aheadThread([&] {
        for (int block = 0; block < 300; ++block)
            ahead.renderBlock(pool, RenderThreadPool::Slot::ahead);
    });
    callbackThread.join();
    aheadThread.join();
    pool.release(callback);
    pool.release(ahead);

    CHECK(callback.wrongCounts.load() == 0);
    CHECK(ahead.wrongCounts.load() == 0);
}

TEST_CASE("A job rendered ahead runs on the workers, not only on its own thread",
          "[engine][exec][parallel][3360]") {
    RenderThreadPool pool(3, false);
    CountingJob ahead(8, std::chrono::microseconds(200));
    for (int block = 0; block < 20; ++block)
        ahead.renderBlock(pool, RenderThreadPool::Slot::ahead);
    pool.release(ahead);

    CHECK(ahead.wrongCounts.load() == 0);
    CHECK(ahead.offCaller.load() > 0);
}

TEST_CASE("Retiring a job rendered ahead ends while the callback's keeps rendering",
          "[engine][exec][parallel][3360]") {
    // As the executor retiring an epoch does: released, then freed, with the pool busy.
    RenderThreadPool pool(4, false);
    CountingJob callback(12, std::chrono::microseconds(5));

    std::mutex swapMutex;
    std::shared_ptr<CountingJob> current;
    std::atomic<int> published{0};
    std::atomic<int> rendered{-1};
    std::atomic<bool> stop{false};

    std::thread callbackThread([&] {
        while (!stop.load())
            callback.renderBlock(pool, RenderThreadPool::Slot::callback);
    });
    std::thread aheadThread([&] {
        while (!stop.load()) {
            std::shared_ptr<CountingJob> job;
            int generation = 0;
            {
                const std::lock_guard<std::mutex> lock(swapMutex);
                job = current;
                generation = published.load();
            }
            if (job == nullptr) {
                std::this_thread::yield();
                continue;
            }
            job->renderBlock(pool, RenderThreadPool::Slot::ahead);
            rendered.store(generation);
        }
    });

    for (int swap = 0; swap < 20; ++swap) {
        auto next = std::make_shared<CountingJob>(12, std::chrono::microseconds(5));
        std::shared_ptr<CountingJob> retiring;
        {
            const std::lock_guard<std::mutex> lock(swapMutex);
            retiring = std::move(current);
            current = std::move(next);
            published.fetch_add(1);
        }
        while (rendered.load() < published.load())
            std::this_thread::yield();

        if (retiring != nullptr) {
            pool.release(*retiring);
            CHECK(retiring->wrongCounts.load() == 0);
        }
    }
    stop.store(true);
    callbackThread.join();
    aheadThread.join();
    pool.release(callback);
    pool.release(*current);

    CHECK(callback.wrongCounts.load() == 0);
    CHECK(current->wrongCounts.load() == 0);
}
