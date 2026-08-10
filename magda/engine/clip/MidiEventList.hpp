#pragma once

#include <cstdint>
#include <vector>

/**
 * @file MidiEventList.hpp
 * @brief One MIDI clip's messages, resolved, and how a block finds them.
 *
 * The compiled form of everything the model says about a MIDI clip. Between a
 * `MidiNote` and a note-on stand the curve densification, the MPE channel
 * assignment, the same-pitch overlap rule and the two offsets, and every one of
 * those is a question about the model with one answer that does not change until
 * the model does. So they are answered once, off the audio thread, and what
 * reaches the callback is one sorted array of short messages.
 *
 * The same move `AudioEventPlayback` makes with `WarpMap`, and the same move the
 * fork makes by a longer road: it builds a playback sequence per clip
 * (`MidiList::createDefaultPlaybackMidiSequence`) and its node walks that. What
 * differs is where the sequence lives and what rebuilding it costs. The fork's
 * lives inside `te::MidiClip`, so editing a curve clears and rebuilds it, TE's
 * TreeWatcher sees the tree change and restarts playback, and the graph is
 * rebuilt under a rolling transport. This one is a value in an immutable
 * snapshot, so an edit compiles a new one and swaps it in.
 *
 * Groove is the one thing NOT resolved here, because it cannot be: it is
 * anchored to the project grid, so a looped clip grooves each pass differently
 * whenever its loop length is not a whole multiple of the template's period
 * (GrooveTemplate.hpp).
 *
 * Beats throughout, in the clip's own content domain. Where those land on the
 * timeline is the fold's answer, below.
 */

namespace magda::engine {

/**
 * @brief One short message, at one content beat.
 *
 * Three data bytes and no more, because a MIDI clip has no SysEx: the model
 * holds notes, CC and pitch bend and nothing else. That is what keeps the
 * per-block cost bounded by events rather than by bytes, which matters against
 * a port budget counted in bytes (EngineDevice.hpp).
 */
struct MidiClipEvent {
    double beat = 0.0;

    /// Channel included, as it goes out.
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    /// Note-ons only: the beat this note stops sounding at.
    ///
    /// The only field with no counterpart in the model, and what makes "which
    /// notes are sounding at this instant" answerable without a scan of
    /// unbounded length. That question is asked on every locate.
    ///
    /// Where it STOPS sounding, which is not always where its own note-off is,
    /// and the difference matters to exactly one caller. A note whose pitch is
    /// struck again before it ends has its note-off dropped at compile time,
    /// because emitting it would cut the second note short (the fork's
    /// `useNoteUp = false`). Such a note still sounds from its onset until the
    /// retrigger replaces it, so this is the retrigger's beat: a locate landing
    /// in that stretch has to hear it, and reading a dropped note-off as "never
    /// sounding" would give silence instead.
    double endBeat = 0.0;

    unsigned kind() const {
        return status & 0xf0u;
    }
    bool isNoteOn() const {
        return kind() == 0x90u && data2 > 0;
    }
    bool isNoteOff() const {
        return kind() == 0x80u || (kind() == 0x90u && data2 == 0);
    }
    bool isNoteEdge() const {
        return isNoteOn() || isNoteOff();
    }
    int channel() const {
        return static_cast<int>(status & 0x0fu) + 1;
    }
};

/**
 * @brief Every controller of one kind on one channel, in order.
 *
 * What the chase reads. Locating into the middle of a clip has to leave every
 * controller at the value the curve is at, and the answer is the last event of
 * each stream before the instant, which is one binary search per stream rather
 * than the scan of the whole sequence the fork does
 * (`chocMidiHelpers::createControllerUpdatesForTime`).
 *
 * Emitting on value change makes that lookup exactly right rather than nearly
 * right: if nothing was emitted since, nothing changed since, so the last event
 * IS the current value. Under a fixed grid it would be up to a grid step stale,
 * and the synth would sit on the stale value until the next point arrived.
 */
struct MidiControllerStream {
    /// 1 to 16.
    int channel = 1;

    /// The controller number, or @ref kPitchBend.
    int controller = 0;

    /// Indices into MidiEventList::events, ascending.
    std::vector<std::int32_t> events;

    static constexpr int kPitchBend = 256;
};

/**
 * @brief One stretch of a block, in one pass of a clip's loop.
 *
 * A loop is a coordinate change rather than a copy. The fork unrolls, writing
 * one copy of the sequence per repetition, so a two-bar loop under a sixty-four
 * bar clip is thirty-two copies rebuilt whenever a note moves. Nothing here
 * needs that: a block is a beat range, and folding a range through a loop gives
 * a handful of sub-ranges over the one list.
 */
struct MidiFoldPass {
    /// The half-open content range this pass covers of the block.
    double contentStart = 0.0;
    double contentEnd = 0.0;

    /// The timeline beat that content beat zero sits at, for this pass. An
    /// event at content beat c therefore sounds at `timelineOfContentZero + c`.
    double timelineOfContentZero = 0.0;

    /// Whether this pass begins at the loop's own start inside this block,
    /// which is where notes hanging over from before the loop start are struck
    /// again, and where the previous pass's notes are ended.
    bool startsPass = false;

    /// Whether this pass runs out inside this block, which is where everything
    /// still sounding from it is ended.
    bool endsPass = false;

    /// The content beat a pass runs from and to, which is the loop region for a
    /// looped clip and the whole list for one that does not loop.
    double windowStart = 0.0;
    double windowEnd = 0.0;
};

/**
 * @brief One MIDI clip's messages and the indexes over them.
 */
struct MidiEventList {
    /// Sorted by beat, and at equal beats by kind: controllers, then pitch
    /// bend, then note-offs, then note-ons. Controllers first because a bank or
    /// program change has to land before the note it configures, which is the
    /// fork's rule too; offs before ons because two notes of one pitch meeting
    /// exactly is otherwise a coin toss between a retrigger and a hung note.
    std::vector<MidiClipEvent> events;

    std::vector<MidiControllerStream> controllers;

    /// The longest note in the list. What bounds the backwards scan the chase
    /// makes: a note-on further back than this cannot still be sounding, so
    /// "what is on at this instant" is a bounded window rather than a walk from
    /// the top.
    double longestNoteBeats = 0.0;

    /// Whether any note carries pitch expression, which is what puts the clip
    /// on MPE channels.
    bool mpe = false;

    bool empty() const {
        return events.empty();
    }

    /// The first event at or after @p beat. Binary search; the list is sorted.
    std::size_t lowerBound(double beat) const;

    /// The value of every controller as of @p beat, as event indices, appended
    /// to @p out. Empty entries are skipped: a stream with nothing before the
    /// instant has no value to chase to.
    void controllerStateAt(double beat, std::vector<std::int32_t>& out) const;

    /**
     * @brief Note-ons before @p beat whose note ends after it, into @p out.
     *
     * What a locate needs, and what a loop pass needs: a note hanging over the
     * point being jumped to has to be struck, and the fork does the same thing
     * for the same reason (`getNotesOnAtTime`, and the clip-in half of its
     * unrolling). A note with no note-off at all is never sounding here, because
     * what ended it was a boundary rather than itself.
     *
     * Bounded by @ref longestNoteBeats rather than walked from the top.
     *
     * @p widen is what a groove can move an edge by. With one in force these are
     * candidates rather than an answer, because where a note sounds is not where
     * it was written, and the caller settles it on the grooved edges.
     */
    void notesSoundingAt(double beat, double widen, std::vector<std::int32_t>& out) const;
};

/**
 * @brief How a clip's content maps onto the timeline, and back.
 *
 * The fold's inputs, kept together because they are asked about together and
 * because their interaction is the whole of what a MIDI clip's placement means.
 */
struct MidiFold {
    /// Timeline beat the clip starts at.
    double clipStartBeat = 0.0;

    /// The content origin a left-resize left behind, which only exists when the
    /// clip does not loop: a looped clip's window is the loop region and the
    /// trim has nothing to say about it, which is what the model's own
    /// getMidiVisibleRange already decides.
    double trimOffsetBeats = 0.0;

    /// The phase, which applies whether or not the clip loops. The fork's
    /// arranger path drops it for a clip that does not loop while its session
    /// path applies it; that is a gap in the sync layer rather than a semantic,
    /// and it is recorded as a divergence.
    double offsetBeats = 0.0;

    bool loopEnabled = false;
    double loopStartBeats = 0.0;
    double loopLengthBeats = 0.0;
};

/// How many passes of one clip's loop a single block may cover. A loop shorter
/// than a block is pathological and reachable: loopLengthBeats has no floor in
/// the model. Past this the source counts rather than looping unbounded on the
/// audio thread.
constexpr int kMaxFoldPassesPerBlock = 8;

/**
 * @brief Fold the timeline range [@p startBeat, @p endBeat) into passes.
 *
 * Returns how many were written into @p out, which is never more than
 * kMaxFoldPassesPerBlock. A clip that does not loop is always exactly one pass.
 */
int foldBlock(const MidiFold& fold, double startBeat, double endBeat, double clipEndBeat,
              MidiFoldPass (&out)[kMaxFoldPassesPerBlock]);

}  // namespace magda::engine
