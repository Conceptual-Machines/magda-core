#pragma once

#include <algorithm>
#include <limits>

#include "clip/ClipSnapshot.hpp"
#include "exec/RenderContext.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * @file SessionPlayback.hpp
 * @brief A block on the axis of the run playing it.
 *
 * A slot is compiled at the origin and a launch handle says where its run began
 * (ClipSnapshot.hpp, LaunchHandle.hpp), so playing one is the arrangement's own
 * playback over a block whose axes have moved onto that origin.
 *
 * Shared by the audio and MIDI sources: audio takes a sub-block, MIDI takes the
 * whole block and bounds its emission instead.
 */

namespace magda::engine {

/// Whether the material runs on from the last block. Material beat zero is a
/// run beginning here (LaunchHandle::virtualStart), which is a discontinuity.
inline bool runsOn(const BlockInfo& block, const BeatRange& range, const MaterialOrigin& origin) {
    return block.continuous && (range.start - origin.beat) > 0.0;
}

/**
 * @brief @p block with both axes moved onto @p origin. Samples are unchanged.
 *
 * Beats become material beats, seconds become elapsed run time. Each is a
 * subtraction of its own face of the origin; deriving one from the other
 * through a tempo map gives where a beat sits, not how long a run has lasted,
 * and after a wrap the projected origin is in a cycle the run never played
 * (#2324). At one constant tempo the two derivations agree.
 *
 * A slot's spans and fades were compiled at the origin's tempo, so under a
 * tempo curve its audible length in seconds is the origin's answer (#2306).
 *
 * @p range only decides @ref runsOn; bounding the emission to it is the
 * caller's.
 */
inline BlockInfo materialBlock(const BlockInfo& block, const BeatRange& range,
                               const MaterialOrigin& origin) {
    auto material = block;
    material.beats = BeatRange{block.beats.start - origin.beat, block.beats.end - origin.beat};
    material.seconds =
        SecondsRange{block.seconds.start - origin.seconds, block.seconds.end - origin.seconds};
    material.continuous = runsOn(block, range, origin);

    // Both axes moved, by different amounts, so the project's map answers about
    // the timeline rather than about this. Recorded rather than dropped:
    // beatAtTime puts the origin back to ask and takes it off the answer, and a
    // reader consuming its material against beats is asked about instants past
    // the end of the block, where the block's own two ends are a straight line
    // through whatever the tempo does next (RenderContext.hpp).
    material.materialOrigin = origin;
    return material;
}

/**
 * @brief The same, cropped to the @p count samples from @p offset.
 *
 * Cropped by samples rather than by beats, which is the only cut that says the
 * same thing to the reader as it does to the buffer. The seconds face comes
 * straight off them: a block runs at one second per sample rate whatever the
 * tempo does, so this is exact rather than a curve read off the block's own two
 * ends (#2330).
 */
inline BlockInfo materialSubBlock(const BlockInfo& block, const BeatRange& range,
                                  const MaterialOrigin& origin, int offset, int count) {
    auto material = materialBlock(block, range, origin);
    material.numSamples = count;
    material.beats = BeatRange{range.start - origin.beat, range.end - origin.beat};

    const auto through = [&](int sample) {
        return block.numSamples > 0
                   ? block.seconds.start +
                         (static_cast<double>(sample) / static_cast<double>(block.numSamples)) *
                             block.seconds.length()
                   : block.seconds.start;
    };

    material.seconds =
        SecondsRange{through(offset) - origin.seconds, through(offset + count) - origin.seconds};
    return material;
}

/// Where in @p block the event landed, or the whole block when there was none.
/// What starts a clip on its beat rather than on a callback boundary.
///
/// A bound rather than a moment: it is where one half of the block ends and the
/// other begins, and with no event it is the boundary past the last sample.
inline EdgeSample splitSample(const BlockInfo& block, const SplitStatus& status) {
    return EdgeSample{status.afterEvent ? status.event.sample : block.numSamples};
}

/**
 * @brief How long a section takes to hand a track over, and to take it back.
 *
 * Short, and much shorter than ClipInfo::launchFadeSamples, because it is the
 * other kind of ramp. A start de-click subtracts an offset from material that
 * goes on sounding underneath it, so its length costs nothing and 256 samples
 * buys a gentler correction. A stop de-click has no material underneath: what
 * it spends its length on is a held constant, and 5 ms of one is a thump in
 * place of a click rather than the absence of both.
 *
 * 32 samples puts what the correction does spend below about 750 Hz at 48 kHz,
 * which is under the step's own broadband energy without lasting long enough to
 * be heard as a level of its own. The incumbent spends between 10 and 40 on the
 * same edge, in two places that disagree with each other; this is one number
 * for one job.
 *
 * A constant rather than a per-clip figure, unlike a slot's own launch ramp: an
 * arrangement handed over is a whole track's worth of clips at once, and no one
 * of them owns the number.
 */
constexpr int kSectionDeClickSamples = 32;

/**
 * @brief The stretch of a block the arrangement sounds, and what happens at its ends.
 *
 * A track plays its arrangement or its session, never both (#2302). This is
 * that as a span rather than as endpoints a caller then reconciles: the samples
 * the arrangement owns, and whether each end of them is an edge that has to be
 * ramped or merely where the block stopped.
 *
 * One span is enough, and it always begins at the block's first sample. The two
 * directions happen at different kinds of instant and that is what bounds it: a
 * session takes a track on the sample its slot launches, and gives one up at a
 * block boundary, because a release is a request the advance applies at sample
 * zero. So the arrangement can only ever gain the track at zero and lose it
 * once, and a list of spans could hold no more than this does.
 *
 * Saying it as a span is what makes a zero-length one harmless. Ownership
 * expressed as endpoints plus flags cannot tell "the arrangement had the track
 * for no samples" from "the arrangement had the track", and the difference is
 * whether there is an edge to ramp: a slot released and another launched on the
 * same sample would otherwise start a stop ramp over a signal that was not
 * playing, decaying whatever the arrangement last pushed, from before the
 * session took the track at all (#2344 review).
 *
 * Derived rather than stored, from the table both of a track's sources read,
 * after the launcher has advanced every handle and before anything renders. Two
 * sources folding the same table over the same block reach the same answer,
 * which is what keeps a track's audio and its MIDI on the same side of every
 * switch with nothing of their own to keep in step.
 */
struct SectionHold {
    /// The edge past which the arrangement is silent. Zero for a block the
    /// session owns entirely.
    EdgeSample until{0};

    /// Whether the arrangement takes the track at the block's first sample
    /// rather than already having it: a resume, which lands mid-material and
    /// owes both a ramp out of silence and a chase of what is sounding there.
    ///
    /// Never set for a span with no samples in it, because nothing resumes.
    bool gained = false;

    /// Whether it loses the track at @ref until rather than simply running to
    /// the end of the block: the edge that has to be carried down, and that
    /// owes note-offs for whatever it had sounding.
    ///
    /// Set for a span with no samples in it only when the arrangement owned the
    /// block before this one, which is a launch landing on a callback boundary:
    /// there is a step there, and it comes off the block before.
    bool lost = false;

    /// Whether the arrangement sounds any of this block.
    bool sounds() const {
        return until.value > 0;
    }

    /// The whole of a block, with no edges: a track nothing can launch on.
    ///
    /// Named rather than left to a default, because the right answer is the
    /// block's length and a default cannot know it. Defaulting to zero says the
    /// session owns everything, which is the opposite of what a track with no
    /// handles is, and silences its arrangement (#2344 review).
    static SectionHold arrangement(int numSamples) {
        return SectionHold{EdgeSample{numSamples}, false, false};
    }
};

/**
 * @copydoc SectionHold
 *
 * @p handles is null until the store has published a table, which is a session
 * whose slots have no handles yet rather than an error (SessionLauncher.hpp).
 * Nothing can hold a track then, so the arrangement has all of it.
 */
inline SectionHold sectionHold(const LaunchHandleTable* handles, TrackId trackId, int numSamples) {
    if (handles == nullptr)
        return SectionHold::arrangement(numSamples);

    const auto [first, last] = handles->rangeFor(trackId);

    // Who owned the track before anything in this block, who owns its first
    // sample once releases have applied, and who owns its last.
    auto sessionBefore = false;
    auto sessionAtZero = false;
    auto sessionAtEnd = false;
    auto takenAt = std::numeric_limits<int>::max();

    for (const auto* entry = first; entry != last; ++entry) {
        if (entry->handle == nullptr)
            continue;

        const auto& status = entry->handle->blockStatus();

        sessionBefore = sessionBefore || status.heldSectionAtStart;

        // A release applies on the first sample, so a slot that was released
        // holds nothing by the time anything sounds.
        const auto heldThrough = status.heldSectionAtStart && !status.releasedSection;
        sessionAtZero = sessionAtZero || heldThrough;

        if (!entry->handle->holdsSection())
            continue;

        sessionAtEnd = true;

        // Where this slot took the track. One that already had it took it
        // before this block; one that launched here took it on its own sample.
        if (heldThrough || status.beforeEvent.playing())
            takenAt = 0;
        else if (status.afterEvent && status.afterEvent->playing())
            takenAt = std::min(takenAt, status.event.sample);
    }

    SectionHold hold;

    if (sessionAtZero)
        hold.until = EdgeSample{0};
    else if (sessionAtEnd && takenAt != std::numeric_limits<int>::max())
        hold.until = EdgeSample{std::clamp(takenAt, 0, numSamples)};
    else
        hold.until = EdgeSample{numSamples};

    // Both edges are about a signal, so neither exists without one. The
    // exception is a loss on a block boundary, where the signal is the block
    // before's and the step is still there.
    hold.gained = sessionBefore && !sessionAtZero && hold.sounds();
    hold.lost = sessionAtEnd && (hold.sounds() || !sessionBefore);

    return hold;
}

/**
 * @brief Every slot of @p track that has a handle, in scene order.
 *
 * @p fn takes `(const SessionSlotPlayback&, const SplitStatus&)`. The status is
 * the launcher's, worked out before anything rendered (SessionLauncher.hpp).
 *
 * A merge of two lists already in scene order. The snapshot and the table
 * publish separately, so a slot with no handle is one the table has not caught
 * up with: silent for that block, not an error.
 */
template <typename Fn>
void forEachSlot(const LaunchHandleTable& handles, const TrackClipPlayback& track, const Fn& fn) {
    const auto [first, last] = handles.rangeFor(track.trackId);

    const auto* entry = first;

    for (const auto& slot : track.session) {
        while (entry != last && entry->key.sceneIndex < slot.sceneIndex)
            ++entry;

        if (entry == last)
            return;

        if (entry->key.sceneIndex == slot.sceneIndex && entry->handle != nullptr)
            fn(slot, entry->handle->blockStatus());
    }
}

}  // namespace magda::engine
