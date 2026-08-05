#pragma once

#include <algorithm>
#include <cmath>

/**
 * @file RenderContext.hpp
 * @brief What the executor renders into, and what it is asked to render now.
 */

namespace magda::engine {

/**
 * @brief Fixed properties of the audio device a plan is prepared for.
 *
 * Settled before any block runs and constant until the next prepare, so
 * everything sized from it (scratch buffers, device state) is allocated once
 * off the audio thread.
 */
struct RenderContext {
    double sampleRate = 44100.0;

    /// Largest block the executor will be asked for. Blocks may be shorter;
    /// output is identical either way, because nothing here quantises to the
    /// block (block size is an I/O batching concept, never a precision one).
    int maxBlockSize = 512;

    /// Channels on every audio port. Stereo throughout: the model has no mono
    /// tracks and the plan carries no channel layouts.
    int numChannels = 2;

    bool operator==(const RenderContext&) const = default;
};

/**
 * @brief The stretch of timeline one block covers.
 *
 * Beats, because beats are what the model positions things in. The transport's
 * sample clock is what turns the audio device's sample count into this, once
 * per block, and it is the only thing that reads a tempo map: an op is handed
 * the two ends of its block and needs nothing else to know where it is.
 *
 * A block never spans a jump. The transport cuts a callback at every loop wrap,
 * so what arrives here always runs from @ref startBeat to @ref endBeat without
 * interruption, and @ref continuous says whether the previous block ended where
 * this one begins.
 */
struct BlockInfo {
    int numSamples = 0;

    /// Whether the transport is rolling. A stopped block still renders: the
    /// graph keeps running so tails ring out and live input passes through.
    /// What a stopped block does not do is advance the timeline, so its start
    /// and end beats are the same.
    bool playing = false;

    double startBeat = 0.0;
    double endBeat = 0.0;

    /**
     * @brief Whether the timeline ran into this block out of the last one.
     *
     * False on the first block after the transport starts, after a locate, and
     * after a loop wrap. What that licenses is narrow: anything an op derived
     * from where the cursor *was* is stale and has to be derived again, which
     * for a source means seeking.
     *
     * What it does not license is a reset. Audio did not stop flowing through
     * the graph because the cursor moved, so delay lines, reverb tails and
     * envelopes carry on across a jump exactly as they carry on across any
     * other block boundary. An op that clears its state here is inventing a gap
     * the transport never asked for.
     */
    bool continuous = false;

    /**
     * @brief Sample offset within this block of a musical position inside it.
     *
     * The conversion every op needs and none of them should be doing twice: a
     * MIDI clip placing a note, a metronome placing a tick. Linear across the
     * block, which is exact at a constant tempo and, across a ramp, wrong by
     * far less than a sample over the few milliseconds a block lasts.
     *
     * Positions outside the block clamp to its ends: an event has to land on a
     * sample this block actually has.
     */
    int sampleForBeat(double beat) const {
        if (numSamples <= 0)
            return 0;
        if (endBeat <= startBeat)
            return 0;

        // Nearest rather than truncated: a position that lands exactly on a
        // sample has to resolve to that sample, and the arithmetic getting
        // there is not exact enough for truncation to promise it.
        const auto position = (beat - startBeat) / (endBeat - startBeat);
        const auto sample = static_cast<int>(std::lround(position * numSamples));
        return std::clamp(sample, 0, numSamples - 1);
    }
};

}  // namespace magda::engine
