#pragma once

#include <algorithm>
#include <cmath>

#include "transport/TempoMap.hpp"

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

    /// Channels on every audio slot. Stereo throughout: the model has no mono
    /// tracks. A narrow port is a device's declared width, not a smaller
    /// buffer; the executor gives that device the channels it declared and
    /// copies what it wrote back across the slot, so every reader sees this
    /// width (#2138).
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
     * @brief The same stretch of timeline, in seconds.
     *
     * Not a second opinion about where the block is: the clock derives the
     * beats from this, so they are two faces of one instant and the conversion
     * between them has happened exactly once, in the one place that owns a
     * tempo map.
     *
     * Both are here because both are asked for, and by different things.
     * Beats say where on the timeline something is, which is how the model
     * positions everything. Seconds say how much audio has gone by, which is
     * what recorded material is measured in and what a file's samples are
     * counted in. A source reading a file needs the second question answered
     * and would otherwise need a tempo map to answer it.
     */
    double startSeconds = 0.0;
    double endSeconds = 0.0;

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

    /**
     * @brief Sample offset within this block of a moment inside it.
     *
     * As sampleForBeat, for the seconds face of the same instant. Exact rather
     * than nearly so: the timeline runs at one second per sample rate through a
     * block whatever the tempo is doing, so this is a straight line and not an
     * approximation of a curve.
     *
     * One sample past the end of the block is a legal answer here, where it is
     * not for a beat, and the difference is what the two are for. A beat places
     * an event, which has to land on a sample this block has. Seconds place the
     * edges of a region, and a region that runs to the end of the block ends at
     * the sample after the last one, the way every half-open range does.
     */
    int sampleForTime(double seconds) const {
        if (numSamples <= 0)
            return 0;
        if (endSeconds <= startSeconds)
            return 0;

        const auto position = (seconds - startSeconds) / (endSeconds - startSeconds);
        const auto sample = static_cast<int>(std::lround(position * numSamples));
        return std::clamp(sample, 0, numSamples);
    }

    /**
     * @brief The beat face of a moment inside this block.
     *
     * The two faces are two views of one stretch of timeline, so a moment given
     * in one has an answer in the other, and this is the conversion that does
     * not need a tempo map: the ends are known in both, and a block is short
     * enough that the curve between them is a straight line to well under a
     * sample.
     *
     * What asks is a clip whose material is consumed against beats rather than
     * against seconds, which is what auto tempo is (clip/EventPlacement.hpp).
     * The window such a clip plays over is worked out in seconds, because that
     * is what spans and fades are in, and then has to be asked about in beats.
     */
    double beatAtTime(double seconds) const {
        // The map itself where there is one, which matters for the moments that
        // are not inside this block. A block's own two ends make a straight
        // line, and that line is exact only while the tempo does not change
        // along it: anything reading past the block's end, or across a step
        // inside it, gets the slope this block happened to have rather than the
        // one the map has there. A clip whose rate follows the tempo is read at
        // instants beyond the block that produced them (ClipVoice's cells), so
        // it is exactly the caller that cannot afford the straight line.
        if (tempo != nullptr)
            return tempo->timeToBeat(seconds);

        if (endSeconds <= startSeconds)
            return startBeat;

        const auto position = (seconds - startSeconds) / (endSeconds - startSeconds);
        return startBeat + position * (endBeat - startBeat);
    }

    /// The map this block was cut from, or null for a caller that assembles a
    /// block by hand. Not owned, and it outlives the block: what publishes a
    /// transport keeps it alive for as long as the callback reading it runs.
    const TempoMap* tempo = nullptr;
};

}  // namespace magda::engine
