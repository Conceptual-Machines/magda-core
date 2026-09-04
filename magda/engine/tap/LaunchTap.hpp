#pragma once

#include <atomic>
#include <bit>
#include <cstdint>

#include "launch/LaunchHandle.hpp"

/**
 * @file LaunchTap.hpp
 * @brief What a block left a slot doing, for whoever draws it (#2303).
 *
 * The read-back side of the launcher. A slot button knows it is queued, a
 * session playhead knows how far into its clip it is, and neither asks: the
 * block that decided it publishes it, the way a value tap publishes what a
 * parameter resolved to (ValueTap.hpp).
 *
 * The incumbent polls instead -- `SessionClipScheduler::processStateEvents` off
 * a 33 ms timer -- which puts the UI a frame behind the audio on every launch
 * and needs a second entry point (`relaunchActiveClips`) for the case where
 * that mattered. Both go with the poll.
 *
 * ## One word
 *
 * A reading pairs a position with the state it was in, so a slot cannot draw a
 * playhead from before the launch that started it, or a queued badge beside a
 * position that has already begun moving. A float and a 32-bit status fit in
 * one 64-bit word, which is one store on the audio thread and one load off it,
 * with nothing to retry.
 *
 * ## Why there is no write count
 *
 * A ValueTap counts blocks because a modifier that holds while the engine is
 * stopped is indistinguishable from one that is not moving, and because a value
 * nothing publishes has to fall back to the model's. Neither applies here. A
 * slot that is not playing is not playing whether or not a block has ever said
 * so, and that is exactly what a button should draw, so the default reading is
 * already the true one and there is nothing for a count to disambiguate.
 *
 * One writer, the audio thread. Any number of readers.
 */

namespace magda::engine {

class LaunchTap {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "a launch tap is written on the audio thread and must not take a lock");

  public:
    /// What a slot has been asked for and has not reached yet.
    enum class Queued : std::uint8_t { nothing, play, stop };

    /** @brief What one block left the slot doing. */
    struct Reading {
        /// Timeline beats this run has covered, unlooped, or zero while
        /// stopped. The UI wraps it against the clip's own length: a playhead
        /// is a position in the material, and the length that turns one into
        /// the other belongs to the model.
        float elapsedBeats = 0.0f;

        bool playing = false;

        /// What is queued, which is what a slot button blinks on.
        Queued queued = Queued::nothing;

        /// Whether this slot holds its track's playback, so the arrangement is
        /// silent behind it even when the slot itself is not sounding (#2302).
        bool holdsSection = false;
    };

    /**
     * @brief What the last block to render left the slot doing.
     *
     * Off the audio thread, from as many readers as like. Nothing is consumed.
     *
     * One load, so the position and the state are one block's own.
     */
    Reading read() const {
        return unpack(state_.load(std::memory_order_acquire));
    }

    /**
     * @brief Publish what this block left @p handle doing. Audio thread.
     *
     * Once per block per slot, from the pass that advanced it, so the reading
     * and the audio it describes are the same block.
     */
    void write(const LaunchHandle& handle) {
        Reading reading;
        reading.playing = handle.playState() == LaunchHandle::PlayState::playing;
        reading.holdsSection = handle.holdsSection();

        if (const auto queued = handle.queuedState())
            reading.queued =
                *queued == LaunchHandle::QueueState::playQueued ? Queued::play : Queued::stop;

        if (const auto played = handle.playedRange())
            reading.elapsedBeats = static_cast<float>(played->length());

        state_.store(pack(reading), std::memory_order_release);
    }

  private:
    /// The three flags in the status half: playing, then the two queued bits,
    /// then the section hold.
    static std::uint32_t packStatus(const Reading& reading) {
        return static_cast<std::uint32_t>(reading.playing ? 1U : 0U) |
               (static_cast<std::uint32_t>(reading.queued) << 1U) |
               (static_cast<std::uint32_t>(reading.holdsSection ? 1U : 0U) << 3U);
    }

    static std::uint64_t pack(const Reading& reading) {
        return static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(reading.elapsedBeats)) |
               (static_cast<std::uint64_t>(packStatus(reading)) << 32U);
    }

    static Reading unpack(std::uint64_t word) {
        const auto status = static_cast<std::uint32_t>(word >> 32U);

        Reading reading;
        reading.elapsedBeats = std::bit_cast<float>(static_cast<std::uint32_t>(word));
        reading.playing = (status & 1U) != 0U;
        reading.queued = static_cast<Queued>((status >> 1U) & 0b11U);
        reading.holdsSection = ((status >> 3U) & 1U) != 0U;
        return reading;
    }

    std::atomic<std::uint64_t> state_{0};
};

}  // namespace magda::engine
