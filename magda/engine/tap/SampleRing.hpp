#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
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
 *
 * The slots are atomic so that the lap is that and nothing more. Plain floats
 * would make it a data race, which is undefined rather than merely ragged: the
 * bound above would be a claim about what compilers happen to do, and the first
 * analysis device wired to a ring would hand `make tsan` a real positive, whose
 * only answers are a suppression that blunts the tool or a rewrite under time
 * pressure. Relaxed on both sides, because the ordering that matters is the
 * position counter's and the slots order nothing: what this compiles to is the
 * plain loads and stores it would have been anyway.
 */
class SampleRing {
  public:
    /// Rounded up to a power of two: the index is a mask rather than a modulo,
    /// which is what keeps the write loop free of division.
    explicit SampleRing(int capacity = 16384)
        : capacity_(juce::nextPowerOfTwo(std::max(1024, capacity))),
          mask_(static_cast<std::size_t>(capacity_) - 1),
          samples_(static_cast<std::size_t>(capacity_)) {}

    int capacity() const {
        return capacity_;
    }

    /// Audio thread. Append mono samples.
    void write(const float* samples, int numSamples) {
        if (numSamples <= 0)
            return;

        const auto position = writePosition_.load(std::memory_order_relaxed);
        const auto total = static_cast<std::size_t>(numSamples);
        const auto capacity = static_cast<std::size_t>(capacity_);

        // A block longer than the ring overwrites its own head, so only its tail
        // is ever visible; writing it once is the same ring and one lap less.
        const auto kept = std::min(total, capacity);
        const float* source = samples + (total - kept);

        const auto start = (position + (total - kept)) & mask_;
        const auto firstRun = std::min(kept, capacity - start);
        storeRun(start, source, firstRun);
        storeRun(0, source + firstRun, kept - firstRun);

        writePosition_.store(position + total, std::memory_order_release);
    }

    /**
     * @brief Audio thread. Append the mean of @p block's channels.
     *
     * Through a fixed stack chunk, so this still has no maximum block size. A
     * tap that had to be prepared for a block size would have to do something
     * when handed a longer one, and the only quiet options are to allocate or to
     * drop the block: one is forbidden here and the other is a display that goes
     * blank exactly when a host changes its buffer size.
     */
    void writeDownmix(juce::dsp::AudioBlock<const float> block, int numSamples) {
        const auto channels = static_cast<int>(block.getNumChannels());
        if (numSamples <= 0 || channels <= 0)
            return;

        const auto scale = 1.0f / static_cast<float>(channels);

        // The mean is built in a fixed stack chunk and written through write(),
        // which is what keeps the sum vectorised without giving the tap a
        // maximum block size: a longer block simply takes more passes.
        constexpr int kChunkSamples = 256;
        std::array<float, kChunkSamples> mono{};

        for (auto done = 0; done < numSamples; done += kChunkSamples) {
            const auto count = std::min(kChunkSamples, numSamples - done);
            juce::FloatVectorOperations::copyWithMultiply(
                mono.data(), block.getChannelPointer(0) + done, scale, count);
            for (auto channel = 1; channel < channels; ++channel)
                juce::FloatVectorOperations::addWithMultiply(
                    mono.data(), block.getChannelPointer(static_cast<std::size_t>(channel)) + done,
                    scale, count);
            write(mono.data(), count);
        }
    }

    /**
     * @brief Copy the most recent @p numSamples into @p destination.
     *
     * Off the audio thread. Zero-padded at the front for anything the ring
     * cannot answer for, which is two cases and one rule: history from before
     * it had been written to, and history older than it is deep. A caller
     * asking for a longer window than the ring holds gets silence in front of
     * what there is, rather than the ring's contents repeated: the masked index
     * would happily fold two laps onto the same slots and hand back a
     * duplicated waveform, which draws as a signal that was never played and
     * transforms as harmonics that were never there.
     *
     * Returns the running count of samples ever written, which is how a caller
     * tells that nothing new has arrived since it last asked without comparing
     * the audio itself.
     */
    std::size_t readLatest(float* destination, int numSamples) const {
        const auto position = writePosition_.load(std::memory_order_acquire);
        if (destination == nullptr || numSamples <= 0)
            return position;

        // What the ring can answer for: never more than it is deep, and never
        // more than has been written. The rest is the zero pad at the front.
        const auto wanted = static_cast<std::size_t>(numSamples);
        const auto capacity = static_cast<std::size_t>(capacity_);
        const auto available = std::min({position, wanted, capacity});
        const auto pad = wanted - available;
        std::fill(destination, destination + pad, 0.0f);

        const auto start = (position - available) & mask_;
        const auto firstRun = std::min(available, capacity - start);
        loadRun(start, destination + pad, firstRun);
        loadRun(0, destination + pad + firstRun, available - firstRun);

        return position;
    }

    /// Samples written since construction. Off the audio thread.
    std::size_t writePosition() const {
        return writePosition_.load(std::memory_order_acquire);
    }

  private:
    /// One run of slots, which by construction does not wrap the ring.
    void storeRun(std::size_t start, const float* source, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            samples_[start + i].store(source[i], std::memory_order_relaxed);
    }

    void loadRun(std::size_t start, float* destination, std::size_t count) const {
        for (std::size_t i = 0; i < count; ++i)
            destination[i] = samples_[start + i].load(std::memory_order_relaxed);
    }

    const int capacity_;
    const std::size_t mask_;
    /// Value-initialised, which for an atomic is zero: a ring that has not been
    /// written to yet reads as silence rather than as whatever was allocated.
    std::vector<std::atomic<float>> samples_;
    std::atomic<std::size_t> writePosition_{0};
};

}  // namespace magda::engine
