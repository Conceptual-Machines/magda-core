#pragma once

#include <juce_core/juce_core.h>

#include <mutex>
#include <vector>

#include "io/PrefetchStream.hpp"

/**
 * @file PrefetchThread.hpp
 * @brief The one thread allowed to wait for a disk.
 *
 * Every stream in the engine is read from here, on a thread above the ones
 * drawing the interface and below the one rendering audio. One thread rather
 * than one per stream: a hundred tracks would otherwise be a hundred threads
 * competing for the same head, and the order they read in would be decided by
 * the scheduler rather than by what is about to play.
 *
 * It polls. Nothing wakes it from the audio thread, because waking a thread
 * takes a lock and the callback does not take locks; a stream that has just
 * been seeked is found on the next round instead, which is what the underrun
 * count is measuring when it counts a seek.
 */

namespace magda::engine {

class PrefetchThread final : private juce::Thread {
  public:
    PrefetchThread();
    ~PrefetchThread() override;

    PrefetchThread(const PrefetchThread&) = delete;
    PrefetchThread& operator=(const PrefetchThread&) = delete;

    /// Start reading for this stream. Off the audio thread; the stream must
    /// outlive the registration.
    void add(PrefetchStream& stream);

    /**
     * @brief Stop reading for it.
     *
     * Off the audio thread, and blocks until the thread is out of the stream,
     * which is what makes it safe to destroy afterwards. The caller is a thread
     * that is allowed to wait; that is the whole reason removal happens here
     * rather than wherever the audio thread notices a stream is gone.
     */
    void remove(PrefetchStream& stream);

    /// Streams registered right now.
    std::size_t streamCount() const;

    /// One round of filling, without the thread. For tests, which want to say
    /// exactly how far ahead the reader got before the callback ran.
    bool fillOnce();

  private:
    void run() override;

    /// How long to sleep when every pool is full. Short enough that a seek is
    /// answered inside a block or two at any sane device size, long enough that
    /// an idle project is not a thread spinning.
    static constexpr int kIdleMilliseconds = 5;

    mutable std::mutex lock_;
    std::vector<PrefetchStream*> streams_;
};

}  // namespace magda::engine
