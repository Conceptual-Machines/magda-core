#pragma once

#include <juce_core/juce_core.h>

#include <mutex>
#include <vector>

#include "io/RecordStream.hpp"

/**
 * @file RecordThread.hpp
 * @brief The one thread allowed to write a take to disk.
 *
 * PrefetchThread's counterpart, and the same shape for the same reasons: one
 * thread rather than one per take, above the interface and below the callback,
 * polling rather than woken from the audio thread, since waking a thread takes
 * a lock and the callback does not take locks.
 *
 * A round that is behind is not a glitch here, it is a hole in a recording, so
 * it drains every stream that has anything before it sleeps at all.
 */

namespace magda::engine {

class RecordThread final : private juce::Thread {
  public:
    /**
     * @brief A writer, with or without the thread behind it.
     *
     * Off is for tests, which want to say exactly how far behind the disk got
     * before the next block was written.
     */
    explicit RecordThread(bool runInBackground = true);
    ~RecordThread() override;

    RecordThread(const RecordThread&) = delete;
    RecordThread& operator=(const RecordThread&) = delete;

    /// Start draining this stream. Off the audio thread; the stream must
    /// outlive the registration.
    void add(RecordStream& stream);

    /**
     * @brief Stop draining it.
     *
     * Off the audio thread, and blocks until the thread is out of the stream,
     * which is what makes it safe to finish and destroy afterwards. What is
     * still queued stays queued: closing a take is RecordStream::finish, and
     * an unregister that also flushed would decide where a take ends.
     */
    void remove(RecordStream& stream);

    /// Streams registered right now.
    std::size_t streamCount() const;

    /// One round of draining, without the thread.
    bool drainOnce();

    /// Whether anything is draining behind the caller's back.
    bool runsInBackground() const {
        return runsInBackground_;
    }

  private:
    void run() override;

    /// How long to sleep when every stream is empty. Long enough that an idle
    /// project is not a thread spinning, short enough that a take stopped
    /// between rounds is on disk before anyone asks for it.
    static constexpr int kIdleMilliseconds = 5;

    mutable std::mutex lock_;
    std::vector<RecordStream*> streams_;
    bool runsInBackground_ = true;
};

}  // namespace magda::engine
