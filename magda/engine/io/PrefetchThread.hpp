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
    /**
     * @brief A reader, with or without the thread behind it.
     *
     * Playback wants the thread: nothing else can wait for a disk on its
     * behalf. An offline render wants it off, and not as an optimisation. A
     * render goes as fast as the machine will let it, so a second of timeline
     * can pass in ten milliseconds of wall clock, and whether a clip's chunks
     * arrived before the block that wanted them would come down to how the
     * scheduler felt. Every underrun that produced would look exactly like a
     * clip playing silence.
     *
     * With it off, @ref fillOnce is the caller's to drive and the reading
     * happens in step with the render. Only one thing may drive it either way:
     * two threads inside fill() is a race over one stream's cursor.
     */
    explicit PrefetchThread(bool runInBackground = true);
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
    /// exactly how far ahead the reader got before the callback ran, and for an
    /// offline render, which drives its own reading.
    bool fillOnce();

    /// Whether anything is filling behind the caller's back. What an offline
    /// render checks before driving fillOnce itself.
    bool runsInBackground() const {
        return runsInBackground_;
    }

  private:
    void run() override;

    /// How long to sleep when every pool is full. Short enough that a seek is
    /// answered inside a block or two at any sane device size, long enough that
    /// an idle project is not a thread spinning.
    static constexpr int kIdleMilliseconds = 5;

    mutable std::mutex lock_;
    std::vector<PrefetchStream*> streams_;
    bool runsInBackground_ = true;
};

}  // namespace magda::engine
