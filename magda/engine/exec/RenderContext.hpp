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
 * Beats, since beats are what the model positions things in. The transport's
 * sample clock turns the audio device's sample count into this, once per
 * block, and is the only thing that reads a tempo map: an op gets the two
 * ends of its block and needs nothing else to know where it is.
 *
 * A block never spans a jump: the transport cuts a callback at every loop
 * wrap, so this always runs from one end of @ref beats to the other without
 * interruption, and @ref continuous says whether the previous block ended
 * where this one begins.
 */
struct BlockInfo {
    int numSamples = 0;

    /**
     * @brief What one sample of it is worth, in seconds.
     *
     * Here as well as on RenderContext because most callers of the
     * conversions below hold only a block (a MIDI clip placing a note, a
     * metronome placing a tick).
     *
     * Zero means a hand-assembled block that didn't state one; the
     * conversions then derive it from the block's two faces, which is exact
     * whenever the block is well formed (a block runs at one rate whatever
     * the tempo does) except for a stopped block, which covers no time and
     * places nothing.
     */
    double sampleRate = 0.0;

    /// Whether the transport is rolling. A stopped block still renders (the
    /// graph keeps running so tails ring out and live input passes through);
    /// it just doesn't advance the timeline, so its start and end beats match.
    bool playing = false;

    /// Where on the timeline it is. Goes backwards at a loop wrap and a locate.
    BeatRange beats;

    /**
     * @brief The same stretch of timeline, in seconds.
     *
     * Not a second opinion about where the block is -- the clock derives
     * @ref beats from this, so the two are one instant, converted once in
     * the one place that owns a tempo map.
     *
     * Both faces exist because both are asked for by different things:
     * beats say where on the timeline something is (how the model positions
     * everything); seconds say how much audio has gone by (what recorded
     * material and file samples are counted in). A file-reading source
     * needs the seconds answer and would otherwise need a tempo map to get it.
     */
    SecondsRange seconds;

    /**
     * @brief The same stretch, counted in beats that only ever go forwards.
     *
     * The timeline beat goes backwards on every wrap and locate, which is
     * right for placing material and wrong for scheduling against a future
     * beat -- a launch quantized to the next bar, resolved against a beat
     * the loop is about to take back, would fire at the wrong moment or
     * twice.
     *
     * A third face of the same instant, derived alongside beats and
     * seconds. It counts musical time actually rolled through since the
     * clock began, so it survives a wrap, a locate and a tempo edit, and is
     * the domain a queued launch names its position in (#2300).
     *
     * A stopped block does not advance it, for the same reason it does not
     * advance the timeline.
     */
    BeatRange monotonicBeats;

    /**
     * @brief The same stretch, in seconds that only ever go forwards.
     *
     * Accumulated from the samples each block rendered -- elapsed time, not
     * a position. Never derived from the beat faces: after a wrap, two
     * monotonic beats are not in the same cycle, so a map can't convert
     * between them (#2324).
     *
     * This is the one thing @ref monotonicSamples can't answer: how long
     * the transport has actually rolled, across a rate change as well as
     * within one (a sample count divided by the current rate isn't that,
     * since the samples weren't all worth the same time). A run's own
     * elapsed time is instead measured from its samples (LaunchHandle.hpp),
     * re-counting its origin on a rate change rather than reading this.
     *
     * A stopped block does not advance it.
     */
    SecondsRange monotonicSeconds;

    /**
     * @brief Whether the timeline ran into this block out of the last one.
     *
     * False on the first block after the transport starts, after a locate,
     * and after a loop wrap -- meaning anything an op derived from where the
     * cursor *was* is stale and must be re-derived (for a source, a seek).
     *
     * It does not mean a reset: audio didn't stop flowing because the
     * cursor moved, so delay lines, reverb tails and envelopes carry on
     * across a jump exactly as across any other block boundary. An op
     * clearing its state here invents a gap the transport never asked for.
     */
    bool continuous = false;

    /**
     * @brief What one sample of this block is worth, in seconds.
     *
     * What the block says, or what its own two faces say when it says
     * nothing. Exact whenever the block is well formed; zero for a block
     * that covers no time and states no rate, which converts nothing.
     */
    double rate() const {
        if (sampleRate > 0.0)
            return sampleRate;

        return seconds.length() > 0.0 ? static_cast<double>(numSamples) / seconds.length() : 0.0;
    }

    /**
     * @brief The moment @p beat falls on, on this block's own seconds axis.
     *
     * The inverse of @ref beatAtTime, through the map for the same reason
     * and with the same detour through the origin on a shifted block -- the
     * one conversion under everything below: a beat becomes a moment here,
     * and a moment becomes a sample by counting, the only step needing no
     * tempo (#2336).
     */
    double timeForBeat(double beat) const {
        if (tempo != nullptr)
            return tempo->beatToTime(beat + materialOrigin.beat) - materialOrigin.seconds;

        if (beats.empty())
            return seconds.start;

        return seconds.start + (((beat - beats.start) / beats.length()) * seconds.length());
    }

    /**
     * @brief The beat sample zero sounds on, to the tolerance the clock uses.
     *
     * Not quite the block's start. A musical position within a hundredth of
     * a sample of the cursor still counts as the cursor
     * (TransportClock::samplesUntil), so a block can open that close before
     * a boundary while every sample it renders is past it -- the beat at
     * its literal start would then be in the section it already left, and a
     * consumer taking one signature or bar for the whole block would take
     * the wrong one.
     *
     * So this nudges the start forward by that same hundredth of a sample
     * of seconds, converted to a beat at the tempo the block opens on
     * rather than at the block's average slope (through the map, the way
     * @ref beatAtTime reads any other moment) -- the average slope would
     * over- or under-nudge across a tempo step, landing on the wrong side
     * of the very boundary this exists to cross (#2340). A block with no
     * seconds face (assembled by hand) has only the average slope to nudge
     * along.
     *
     * Where the block *is* stays @ref beats and @ref seconds; this only
     * says which side of a boundary its first sample is on, and a position
     * derived under that section is projected back by the caller (#2336).
     */
    double openingBeat() const {
        if (numSamples <= 0 || beats.empty())
            return beats.start;

        if (seconds.empty() || rate() <= 0.0)
            return beats.start + (kSampleEpsilon * (beats.length() / numSamples));

        return beatAtTime(seconds.start + (kSampleEpsilon / rate()));
    }

    /**
     * @brief How many samples into this block @p moment falls, unrounded.
     *
     * By counting samples, the one step that needs no tempo: a block runs
     * at one rate whatever the map is doing.
     */
    double offsetForTime(double moment) const {
        return (moment - seconds.start) * rate();
    }

    /**
     * @brief How many samples into this block @p beat falls, unrounded.
     *
     * Through the seconds face (where samples are counted) and so through
     * the map wherever there is one -- a beat's distance into the block is
     * a question about when it happens, a straight line only while the
     * tempo is. A block with no seconds face (assembled by hand) has only
     * its own two beat ends as that line.
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
     * A note-off is where a note's stretch ends, so it's an edge and lands
     * on N whenever a clip or loop pass ends on the block boundary. It's
     * also a message, and a message at N is written into nobody's buffer:
     * the block owning sample N is the next one, and by the time it
     * renders, the stretch that owed the off has gone -- leaving a note
     * that hangs.
     *
     * So an emitted edge at N sounds on N-1 instead: one sample early,
     * against a note ringing until the session ends.
     */
    EventSample soundsAt(EdgeSample edge) const {
        return EventSample{std::clamp(edge.value, 0, std::max(0, numSamples - 1))};
    }

    /**
     * @brief The beat face of a moment inside this block.
     *
     * The conversion that needs no tempo map: both ends are known in both
     * faces, and a block is short enough that the curve between them is a
     * straight line to well under a sample.
     *
     * What asks is a clip whose material is consumed against beats rather
     * than seconds (auto tempo, clip/EventPlacement.hpp): the window such a
     * clip plays over is worked out in seconds (what spans and fades use)
     * and then has to be asked about in beats.
     */
    double beatAtTime(double moment) const {
        // The map itself where there is one, which matters for moments
        // outside this block: a block's own two ends make a straight line,
        // exact only while the tempo doesn't change along it. A clip whose
        // rate follows the tempo is read at instants beyond the block that
        // produced them (ClipVoice's cells), so it can't afford that line.
        //
        // Through the origin in both directions on a shifted block: the
        // moment goes back onto the timeline to be asked about, and the
        // answer comes back onto the run's own axis. Zero on a timeline
        // block, where this is the map itself.
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
     * The durable coordinate every other face here interprets
     * (transport/TimeDomains.hpp): a locate doesn't move it, a wrap doesn't
     * take it back, a tempo edit doesn't rescale it -- the only face two
     * blocks can be compared on without knowing what happened between.
     *
     * A stopped block does not advance it, for the same reason it does not
     * advance the timeline: nothing played.
     *
     * Empty on a hand-assembled block, which is why nothing may assume it
     * is set (#2332 is what makes it load-bearing).
     */
    SampleRange monotonicSamples;

    /// The map this block was cut from, or null for a hand-assembled block.
    /// Not owned, and outlives the block: whatever publishes a transport
    /// keeps it alive for as long as the callback reading it runs.
    ///
    /// Always the timeline's, even where the axes above are not: a shifted
    /// block records the shift below rather than pretending the map is its
    /// own. Anything reaching for this directly is asking a timeline
    /// question and must put @ref materialOrigin back first.
    const TempoMap* tempo = nullptr;

    /**
     * @brief What the two axes above were shifted by, or zero on the timeline.
     *
     * A session slot plays over a block moved onto the run's own origin
     * (clip/SessionPlayback.hpp), and the two faces move by different
     * amounts: a beat is a position and a second is elapsed time, and under
     * a tempo curve those aren't one number (#2324).
     *
     * Carried so the shift stays undoable -- dropping the map would leave
     * the block's own two ends as the only answer, and that straight line
     * is exactly what the cell path above cannot afford.
     */
    MaterialOrigin materialOrigin;
};

}  // namespace magda::engine
