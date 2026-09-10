#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cstring>
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
        if (samples == nullptr || numSamples <= 0)
            return;

        const size_t w = writePos_.load(std::memory_order_relaxed);

        const auto total = static_cast<size_t>(numSamples);
        const auto capacity = static_cast<size_t>(capacity_);
        const size_t kept = std::min(total, capacity);  // a longer block overwrites its own head
        const float* src = samples + (total - kept);

        const size_t start = (w + (total - kept)) & mask_;
        const size_t firstRun = std::min(kept, capacity - start);
        std::memcpy(buffer_.data() + start, src, firstRun * sizeof(float));
        if (kept > firstRun)
            std::memcpy(buffer_.data(), src + firstRun, (kept - firstRun) * sizeof(float));

        writePos_.store(w + total, std::memory_order_release);
    }

    /**
     * Message thread. Copy the most recent numSamples into dest, zero-padded at
     * the front for whatever the ring cannot supply: it is still filling, or the
     * request is longer than the capacity. Returns the running sample count at
     * the moment of the read so callers can detect whether new audio arrived
     * since last poll.
     */
    size_t readLatest(float* dest, int numSamples) const noexcept {
        const size_t w = writePos_.load(std::memory_order_acquire);
        if (dest == nullptr || numSamples <= 0)
            return w;

        const auto wanted = static_cast<size_t>(numSamples);
        const auto capacity = static_cast<size_t>(capacity_);
        const size_t available = std::min({w, wanted, capacity});
        const size_t pad = wanted - available;
        std::fill(dest, dest + pad, 0.0f);

        const size_t start = (w - available) & mask_;
        const size_t firstRun = std::min(available, capacity - start);
        std::memcpy(dest + pad, buffer_.data() + start, firstRun * sizeof(float));
        if (available > firstRun)
            std::memcpy(dest + pad + firstRun, buffer_.data(),
                        (available - firstRun) * sizeof(float));

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
