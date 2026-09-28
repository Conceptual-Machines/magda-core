#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace magda::test {

/**
 * @brief Holds a file read starting at or after a position until opened (#2700).
 *
 * The reading thread calls @ref pass; the test waits in @ref waitUntilHeld, so
 * an in-flight read is a state the test observes rather than a delay it guesses.
 * Both waits are bounded, so a regression fails rather than hanging CI.
 */
class ReaderGate {
  public:
    void closeFrom(std::int64_t position) {
        const std::scoped_lock guard(lock_);
        from_ = position;
        passes_ = 0;
        open_ = false;
    }

    /// Let @p reads more reads through, then hold the next.
    void closeAfter(int reads) {
        const std::scoped_lock guard(lock_);
        from_ = std::numeric_limits<std::int64_t>::min();
        passes_ = reads;
        open_ = false;
    }

    void open() {
        {
            const std::scoped_lock guard(lock_);
            open_ = true;
        }
        changed_.notify_all();
    }

    void pass(std::int64_t start) {
        std::unique_lock guard(lock_);
        if (open_ || start < from_)
            return;
        if (passes_ > 0) {
            --passes_;
            return;
        }
        held_ = true;
        changed_.notify_all();
        changed_.wait_for(guard, kTimeout, [this] { return open_; });
        held_ = false;
    }

    /// Whether a read reached the gate. False means it never did.
    bool waitUntilHeld() {
        std::unique_lock guard(lock_);
        return changed_.wait_for(guard, kTimeout, [this] { return held_; });
    }

  private:
    static constexpr std::chrono::seconds kTimeout{10};

    std::mutex lock_;
    std::condition_variable changed_;
    std::int64_t from_ = std::numeric_limits<std::int64_t>::max();
    int passes_ = 0;
    bool open_ = true;
    bool held_ = false;
};

/**
 * @brief Reads on a worker thread that is released and joined however the scope ends.
 *
 * A failed assertion inside the scope throws, and a thread still blocked at the
 * gate would reach ~thread joinable and terminate the process.
 */
class GatedWorker {
  public:
    template <typename Work>
    GatedWorker(ReaderGate& gate, Work work) : gate_(gate), worker_(std::move(work)) {}

    ~GatedWorker() {
        release();
    }

    GatedWorker(const GatedWorker&) = delete;
    GatedWorker& operator=(const GatedWorker&) = delete;

    /// Open the gate and wait for the round to finish, for a test that asserts after it.
    void release() {
        gate_.open();
        if (worker_.joinable())
            worker_.join();
    }

  private:
    ReaderGate& gate_;
    std::thread worker_;
};

}  // namespace magda::test
