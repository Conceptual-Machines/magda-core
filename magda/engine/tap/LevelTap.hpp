#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

/**
 * @file LevelTap.hpp
 * @brief What a Meter op writes and a meter reads.
 *
 * The plan is topology, so a Meter op names a location and owns nothing. This
 * is what stands behind it, and the host owns it for the same reason it owns a
 * device: it outlives the plans that reference it, so a structural edit
 * somewhere else in the project does not reset a meter that was reading.
 *
 * One writer (the audio thread) and one reader (whoever draws the meter). Two
 * readers would each take part of what the other was owed, because a read is
 * destructive; that is a property of metering rather than an oversight, see
 * read().
 */

namespace magda::engine {

/**
 * @brief Peak level per channel, published without a lock.
 *
 * Stereo, because the plan carries no channel layouts and every audio port in
 * it is the context's channel count, which is two. A tap fed a wider block
 * reports the first two channels rather than folding them, because nothing in
 * the model says what the rest of them mean.
 */
class LevelTap {
  public:
    static constexpr int kNumChannels = 2;

    struct Levels {
        std::array<float, kNumChannels> peak{};

        float loudest() const {
            return std::max(peak[0], peak[1]);
        }
    };

    /**
     * @brief Take the levels and start again. Off the audio thread.
     *
     * Destructive, and that is what makes a meter a meter: what it reports is
     * the loudest thing since it was last looked at, not the loudest thing in
     * whichever block the reader happened to land on. A non-destructive read
     * would let the frame rate decide how much of the signal the meter ever
     * sees, so a drum hit would show or not show depending on how busy the UI
     * was, and a meter that misses transients is worse than no meter.
     *
     * A tap nobody reads accumulates rather than growing: peak is a maximum, so
     * a meter that goes unwatched for a minute reports that minute's loudest
     * sample the next time it is read.
     */
    Levels read() {
        Levels levels;
        for (auto channel = 0; channel < kNumChannels; ++channel)
            levels.peak[static_cast<std::size_t>(channel)] =
                peaks_[static_cast<std::size_t>(channel)].exchange(0.0f, std::memory_order_acq_rel);
        return levels;
    }

    /**
     * @brief Fold one block into what the next read will report. Audio thread.
     *
     * A maximum rather than an assignment, so no block is lost between two
     * reads however short the blocks are and however slowly the reader polls.
     */
    void write(juce::dsp::AudioBlock<const float> block, int numSamples) {
        if (numSamples <= 0)
            return;

        const auto channels =
            std::min(static_cast<int>(block.getNumChannels()), static_cast<int>(kNumChannels));

        for (auto channel = 0; channel < channels; ++channel) {
            const auto range = juce::FloatVectorOperations::findMinAndMax(
                block.getChannelPointer(static_cast<std::size_t>(channel)), numSamples);
            const auto peak = std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
            accumulate(channel, peak);
        }

        // A mono block on a stereo tap reads as the same level on both, which is
        // what it sounds like. Nothing in the engine produces one today; a tap
        // that reported silence on the right if something did would be a meter
        // lying about audible signal.
        if (channels == 1)
            accumulate(1, peaks_[0].load(std::memory_order_relaxed));
    }

    /// Drop what has not been read. For a meter whose signal has gone away:
    /// a device that was removed should fall to silence rather than hold its
    /// last peak until something reads it.
    void clear() {
        for (auto& peak : peaks_)
            peak.store(0.0f, std::memory_order_release);
    }

  private:
    /// Compare-and-swap rather than a plain store, because the reader may take
    /// the value between the load and the store and a store would then put back
    /// a peak that had already been reported.
    void accumulate(int channel, float peak) {
        auto& slot = peaks_[static_cast<std::size_t>(channel)];
        auto current = slot.load(std::memory_order_relaxed);
        while (peak > current &&
               !slot.compare_exchange_weak(current, peak, std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
        }
    }

    std::array<std::atomic<float>, kNumChannels> peaks_{};
};

}  // namespace magda::engine
