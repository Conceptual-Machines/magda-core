#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace magda::daw::audio {

/**
 * @brief Lock-free single-producer / single-consumer history ring for feeding
 *        audio samples from the audio thread to a UI analyzer (oscilloscope,
 *        spectrum). Shared primitive for all analysis devices.
 *
 * The audio thread calls write() from applyToBuffer(); the message thread calls
 * readLatest() on a timer to grab the most recent N samples (the visible
 * window / FFT frame). This is a "latest history" ring, not a drain-FIFO: the
 * producer always overwrites the oldest samples, so the consumer never backs
 * up and always sees current signal regardless of frame rate.
 *
 * Correctness: a single atomic write counter is published with release on the
 * producer and read with acquire on the consumer. If the producer laps the
 * consumer mid-read (only possible if the UI stalls for ~0.3s at 44.1k with the
 * default capacity) a few samples may tear - acceptable for visualisation, and
 * the large capacity makes it effectively impossible at 30-60fps. No locks, no
 * allocation on the audio thread.
 */
class AudioTapBuffer {
  public:
    explicit AudioTapBuffer(int capacity = 16384)
        : capacity_(juce::nextPowerOfTwo(juce::jmax(1024, capacity))),
          mask_(static_cast<size_t>(capacity_) - 1),
          buffer_(static_cast<size_t>(capacity_), 0.0f) {}

    int capacity() const noexcept {
        return capacity_;
    }

    /** Audio thread. Append mono samples to the ring. */
    void write(const float* samples, int numSamples) noexcept {
        const size_t w = writePos_.load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
            buffer_[(w + static_cast<size_t>(i)) & mask_] = samples[i];
        writePos_.store(w + static_cast<size_t>(numSamples), std::memory_order_release);
    }

    /**
     * Message thread. Copy the most recent numSamples into dest (zero-padded at
     * the front while the ring is still filling). Returns the running sample
     * count at the moment of the read so callers can detect whether new audio
     * arrived since last poll.
     */
    size_t readLatest(float* dest, int numSamples) const noexcept {
        const size_t w = writePos_.load(std::memory_order_acquire);
        for (int i = 0; i < numSamples; ++i) {
            const long long idx = static_cast<long long>(w) - numSamples + i;
            dest[i] = (idx < 0) ? 0.0f : buffer_[static_cast<size_t>(idx) & mask_];
        }
        return w;
    }

    /** Message thread. Running total of samples written since construction. */
    size_t writePosition() const noexcept {
        return writePos_.load(std::memory_order_acquire);
    }

  private:
    const int capacity_;
    const size_t mask_;
    std::vector<float> buffer_;
    std::atomic<size_t> writePos_{0};
};

}  // namespace magda::daw::audio
