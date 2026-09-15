#pragma once

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>

namespace magda::test {

/**
 * @brief Holds a file read starting at or after a position until opened (#2700).
 *
 * The reading thread calls @ref pass; the test waits in @ref waitUntilHeld, so
 * an in-flight read is a state the test observes rather than a delay it guesses.
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
        changed_.wait(guard, [this] { return open_; });
        held_ = false;
    }

    void waitUntilHeld() {
        std::unique_lock guard(lock_);
        changed_.wait(guard, [this] { return held_; });
    }

  private:
    std::mutex lock_;
    std::condition_variable changed_;
    std::int64_t from_ = std::numeric_limits<std::int64_t>::max();
    int passes_ = 0;
    bool open_ = true;
    bool held_ = false;
};

}  // namespace magda::test
