#pragma once

#include <algorithm>
#include <cmath>

#include "transport/TempoMap.hpp"
#include "transport/TimeDomains.hpp"

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
 * so what arrives here always runs from one end of @ref beats to the other without
 * interruption, and @ref continuous says whether the previous block ended where
 * this one begins.
 */
struct BlockInfo {
    int numSamples = 0;

    /**
     * @brief What one sample of it is worth, in seconds.
     *
     * Here as well as on the RenderContext because the conversions below are
     * the block's own and most of what calls them holds a block and nothing
     * else: a MIDI clip placing a note, a metronome placing a tick.
     *
     * Zero says a block did not state one, which is a block assembled by hand,
     * and the conversions then take it from the two faces the block does have.
     * That derivation is the rate exactly whenever the block is well formed,
     * because a block runs at one rate whatever the tempo does. What it cannot
     * do is answer for a block that covers no time at all, which is a stopped
     * one, and a stopped block places nothing.
     */
    double sampleRate = 0.0;

    /// Whether the transport is rolling. A stopped block still renders: the
    /// graph keeps running so tails ring out and live input passes through.
    /// What a stopped block does not do is advance the timeline, so its start
    /// and end beats are the same.
    bool playing = false;

    /// Where on the timeline it is. Goes backwards at a loop wrap and a locate.
    BeatRange beats;

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
    SecondsRange seconds;

    /**
     * @brief The same stretch, counted in beats that only ever go forwards.
     *
     * The timeline beat is where the cursor is, and it goes backwards every
     * time a loop wraps and every time somebody locates. That is right for
     * placing material and wrong for scheduling anything against a future
     * beat: a launch quantized to the next bar, resolved against a beat the
     * loop is about to take back, fires at the wrong moment or twice.
     *
     * This is the third face of the same instant, alongside beats and seconds,
     * and it is derived in the same one place they are. It counts musical time
     * the transport has actually rolled through since the clock began, so it
     * survives a wrap, a locate and a tempo edit, and it is the domain a
     * queued launch names its position in (#2300).
     *
     * A stopped block does not advance it, for the same reason it does not
     * advance the timeline.
     */
    BeatRange monotonicBeats;

    /**
     * @brief The same stretch, in seconds that only ever go forwards.
     *
     * Accumulated from the samples each block rendered, so it is elapsed time
     * rather than a position. Nothing derives it from the beat faces and
     * nothing may: a map converts a position, and after a wrap two monotonic
     * beats are not in the same cycle (#2324).
     *
     * Kept because it is the one thing @ref monotonicSamples cannot answer: how
     * long the transport has actually rolled, across a rate change as well as
     * within one. A count of samples divided by the rate they are being counted
     * at now is not that, because they were not all worth the same amount of
     * time. A run's own elapsed time is measured from the samples instead
     * (LaunchHandle.hpp), and it handles a rate change by re-counting its
     * origin rather than by reading this.
     *
     * A stopped block does not advance it.
     */
    SecondsRange monotonicSeconds;

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
     * @brief What one sample of this block is worth, in seconds.
     *
     * What the block says, or what its own two faces say when it says nothing.
     * The derivation is the rate exactly whenever a block is well formed, since
     * a block runs at one rate whatever the tempo does; zero for a block that
     * covers no time and states no rate, which converts nothing.
     */
    double rate() const {
        if (sampleRate > 0.0)
            return sampleRate;

        return seconds.length() > 0.0 ? static_cast<double>(numSamples) / seconds.length() : 0.0;
    }

    /**
     * @brief The moment @p beat falls on, on this block's own seconds axis.
     *
     * The inverse of @ref beatAtTime, through the map for the same reason and
     * with the same detour through the origin on a shifted block. The one
     * conversion under everything below: a beat becomes a moment here, and a
     * moment becomes a sample by counting them, which is the only step that
     * does not need a tempo (#2336).
     */
    double timeForBeat(double beat) const {
        if (tempo != nullptr)
            return tempo->beatToTime(beat + materialOrigin.beat) - materialOrigin.seconds;

        if (beats.empty())
            return seconds.start;

        return seconds.start + (((beat - beats.start) / beats.length()) * seconds.length());
    }

    /**
     * @brief How many samples into this block @p moment falls, unrounded.
     *
     * By counting samples, which is the one step that needs no tempo: a block
     * runs at one rate whatever the map is doing.
     */
    double offsetForTime(double moment) const {
        return (moment - seconds.start) * rate();
    }

    /**
     * @brief How many samples into this block @p beat falls, unrounded.
     *
     * Through the seconds face, because that is where samples are counted, and
     * therefore through the map wherever there is one: a beat's distance into
     * the block is a question about when it happens, and the answer is only a
     * straight line while the tempo is.
     *
     * A block with no seconds face at all is one assembled by hand from beats,
     * and its own two beat ends are then the only line there is.
     */
    double offsetForBeat(double beat) const {
        if (!seconds.empty() && rate() > 0.0)
            return offsetForTime(timeForBeat(beat));

        return beats.empty() ? 0.0 : ((beat - beats.start) / beats.length()) * numSamples;
    }

    /// The sample of this block that @p moment happens on
    /// (TimeDomains::eventAt).
    EventSample eventForTime(double moment) const {
        return seconds.empty() ? EventSample{0} : eventAt(offsetForTime(moment), numSamples);
    }

    /// The sample of this block that @p beat happens on.
    EventSample eventForBeat(double beat) const {
        return beats.empty() ? EventSample{0} : eventAt(offsetForBeat(beat), numSamples);
    }

    /// Where a stretch bounded by @p moment has its edge (TimeDomains::edgeAt).
    EdgeSample edgeForTime(double moment) const {
        return seconds.empty() ? EdgeSample{0} : edgeAt(offsetForTime(moment), numSamples);
    }

    /// Where a stretch bounded by @p beat has its edge.
    EdgeSample edgeForBeat(double beat) const {
        return beats.empty() ? EdgeSample{0} : edgeAt(offsetForBeat(beat), numSamples);
    }

    /**
     * @brief The sample an edge that has to be heard is emitted on.
     *
     * A note-off is where a note's stretch ends, so it is an edge and lands on
     * N whenever a clip or a loop pass ends on the block boundary. It is also a
     * message, and a message at N is written into nobody's buffer: the block
     * that owns that sample is the next one, and by the time it renders, the
     * stretch that owed the off has gone. What is left is a note that hangs.
     *
     * So an emitted edge at N sounds on N-1 instead. One sample early, against
     * a note that rings until the session ends.
     */
    EventSample soundsAt(EdgeSample edge) const {
        return EventSample{std::clamp(edge.value, 0, std::max(0, numSamples - 1))};
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
    double beatAtTime(double moment) const {
        // The map itself where there is one, which matters for the moments that
        // are not inside this block. A block's own two ends make a straight
        // line, and that line is exact only while the tempo does not change
        // along it: anything reading past the block's end, or across a step
        // inside it, gets the slope this block happened to have rather than the
        // one the map has there. A clip whose rate follows the tempo is read at
        // instants beyond the block that produced them (ClipVoice's cells), so
        // it is exactly the caller that cannot afford the straight line.
        //
        // Through the origin in both directions on a shifted block, which is
        // what keeps the map usable there: the moment goes back onto the
        // timeline to be asked about, and the answer comes back onto the run's
        // own axis. Zero on a timeline block, where this is the map itself.
        if (tempo != nullptr)
            return tempo->timeToBeat(moment + materialOrigin.seconds) - materialOrigin.beat;

        if (seconds.empty())
            return beats.start;

        const auto position = (moment - seconds.start) / seconds.length();
        return beats.start + (position * beats.length());
    }

    /**
     * @brief Where the block sits on the transport's own sample count.
     *
     * The durable coordinate, and the one every other face here is ultimately
     * an interpretation of (transport/TimeDomains.hpp). A locate does not move
     * it, a wrap does not take it back and a tempo edit does not rescale it, so
     * it is the only one of them two blocks can be compared on without knowing
     * what happened between.
     *
     * A stopped block does not advance it, for the same reason it does not
     * advance the timeline: nothing played.
     *
     * Empty on a block assembled by hand, which is why nothing may assume it is
     * set (#2332 is what makes it load-bearing).
     */
    SampleRange monotonicSamples;

    /// The map this block was cut from, or null for a caller that assembles a
    /// block by hand. Not owned, and it outlives the block: what publishes a
    /// transport keeps it alive for as long as the callback reading it runs.
    ///
    /// Always the timeline's, even where the axes above are not: a shifted
    /// block records the shift below rather than pretending the map is its own.
    /// Anything reaching for this directly is asking a timeline question and
    /// has to put @ref materialOrigin back first.
    const TempoMap* tempo = nullptr;

    /**
     * @brief What the two axes above were shifted by, or zero on the timeline.
     *
     * A session slot plays over a block moved onto the run's own origin
     * (clip/SessionPlayback.hpp), and the two faces move by different amounts:
     * a beat is where something sits and a second is how long a run has
     * lasted, and under a tempo curve those are not one number (#2324).
     *
     * Carried so the shift stays undoable. Dropping the map instead would
     * leave the block's own two ends as the only answer, and that straight
     * line is exactly what the cell path above cannot afford.
     */
    MaterialOrigin materialOrigin;
};

}  // namespace magda::engine
