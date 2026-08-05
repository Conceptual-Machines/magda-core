#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

/**
 * @file SampleRing.hpp
 * @brief The recent past of a signal, for something that draws it.
 *
 * An oscilloscope and a spectrum want the same thing and neither wants a queue:
 * they want the last N samples whenever they happen to ask, and they would
 * rather miss what went by while they were not looking than fall behind it. So
 * the writer always overwrites the oldest sample and never blocks, and the
 * reader always sees the present.
 *
 * The same seam as LevelTap, for the other half of the tap question: a level is
 * one number per block, this is the samples themselves. An analysis device owns
 * one of these and the UI reads it; the engine writes it from process().
 */

namespace magda::engine {

/**
 * @brief A single-producer, single-consumer history of the most recent samples.
 *
 * Mono. Every analysis MAGDA draws (a trace, an FFT) is of the summed signal,
 * and a per-channel history would double the memory to be summed on the way out
 * anyway.
 *
 * Tearing is possible and bounded: if the reader stalls for as long as the ring
 * is deep, the writer laps it and the oldest few samples of what it copies are
 * from the new lap. At the default capacity that is a third of a second of
 * audio, which a UI would have to have stopped entirely to hit, and the cost
 * when it happens is one ragged frame of a drawing.
 */
class SampleRing {
  public:
    /// Rounded up to a power of two: the index is a mask rather than a modulo,
    /// which is what keeps the write loop free of division.
    explicit SampleRing(int capacity = 16384)
        : capacity_(juce::nextPowerOfTwo(std::max(1024, capacity))),
          mask_(static_cast<std::size_t>(capacity_) - 1),
          samples_(static_cast<std::size_t>(capacity_), 0.0f) {}

    int capacity() const {
        return capacity_;
    }

    /// Audio thread. Append mono samples.
    void write(const float* samples, int numSamples) {
        if (numSamples <= 0)
            return;

        auto position = writePosition_.load(std::memory_order_relaxed);
        for (auto i = 0; i < numSamples; ++i)
            samples_[(position + static_cast<std::size_t>(i)) & mask_] = samples[i];

        writePosition_.store(position + static_cast<std::size_t>(numSamples),
                             std::memory_order_release);
    }

    /**
     * @brief Audio thread. Append the mean of @p block's channels.
     *
     * Summed straight into the ring rather than through scratch, which is what
     * lets this have no maximum block size. A tap that had to be prepared for a
     * block size would have to do something when handed a longer one, and the
     * only quiet options are to allocate or to drop the block: one is forbidden
     * here and the other is a display that goes blank exactly when a host
     * changes its buffer size.
     */
    void writeDownmix(juce::dsp::AudioBlock<const float> block, int numSamples) {
        const auto channels = static_cast<int>(block.getNumChannels());
        if (numSamples <= 0 || channels <= 0)
            return;

        const auto scale = 1.0f / static_cast<float>(channels);
        auto position = writePosition_.load(std::memory_order_relaxed);

        for (auto i = 0; i < numSamples; ++i) {
            auto sum = 0.0f;
            for (auto channel = 0; channel < channels; ++channel)
                sum += block.getSample(channel, i);
            samples_[(position + static_cast<std::size_t>(i)) & mask_] = sum * scale;
        }

        writePosition_.store(position + static_cast<std::size_t>(numSamples),
                             std::memory_order_release);
    }

    /**
     * @brief Copy the most recent @p numSamples into @p destination.
     *
     * Off the audio thread. Zero-padded at the front while the ring has not
     * filled once, so a display opened a moment ago draws silence rather than
     * whatever the allocation held. Returns the running count of samples ever
     * written, which is how a caller tells that nothing new has arrived since
     * it last asked without comparing the audio itself.
     */
    std::size_t readLatest(float* destination, int numSamples) const {
        const auto position = writePosition_.load(std::memory_order_acquire);

        for (auto i = 0; i < numSamples; ++i) {
            const auto index = static_cast<long long>(position) - numSamples + i;
            destination[i] = index < 0 ? 0.0f : samples_[static_cast<std::size_t>(index) & mask_];
        }

        return position;
    }

    /// Samples written since construction. Off the audio thread.
    std::size_t writePosition() const {
        return writePosition_.load(std::memory_order_acquire);
    }

  private:
    const int capacity_;
    const std::size_t mask_;
    std::vector<float> samples_;
    std::atomic<std::size_t> writePosition_{0};
};

}  // namespace magda::engine
