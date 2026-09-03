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
 * @brief Which section a track sounds over one block, and where that changed.
 *
 * A track plays its arrangement or its session, never both (#2302). The answer
 * is the fold over the track's slots: the session holds the track if any slot
 * holds it, and it took it over at the earliest sample any of them began on.
 *
 * Derived rather than stored, from the table both of a track's sources already
 * read, after the launcher has advanced every handle and before anything
 * renders. Two sources folding the same table over the same block reach the
 * same answer, which is what keeps the audio and the MIDI of one track on the
 * same side of the switch without a third thing to publish between them.
 */
struct SectionHold {
    /// Whether the session holds the track when this block ends.
    bool session = false;

    /// Where it took the track over, for the block in which that happened.
    /// Zero when it already held it, which is also what a caller that was
    /// already handed over does with it: mute the whole block.
    EdgeSample takenAt{0};
};

/// @copydoc SectionHold
inline SectionHold sectionHold(const LaunchHandleTable& handles, TrackId trackId) {
    const auto [first, last] = handles.rangeFor(trackId);

    SectionHold hold;
    auto earliest = std::numeric_limits<int>::max();

    for (const auto* entry = first; entry != last; ++entry) {
        if (entry->handle == nullptr || !entry->handle->holdsSection())
            continue;

        hold.session = true;

        // Where this slot began sounding within the block, if it did. A slot
        // already playing when the block opened took the track at its start; a
        // slot that holds the track and sounded nothing here is one that
        // stopped in an earlier block and is still holding it, which is the
        // same answer.
        const auto& status = entry->handle->blockStatus();

        if (status.beforeEvent.playing())
            earliest = 0;
        else if (status.afterEvent && status.afterEvent->playing())
            earliest = std::min(earliest, status.event.sample);
    }

    if (hold.session && earliest != std::numeric_limits<int>::max())
        hold.takenAt = EdgeSample{earliest};

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
