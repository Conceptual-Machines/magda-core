#pragma once

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

/**
 * @brief Where a beat inside @p block is on the block's own seconds axis.
 *
 * The block's own line, not a tempo map: it is the line sampleForBeat and
 * sampleForTime use, so a sub-range's seconds and its sample count agree.
 */
inline double secondsWithin(const BlockInfo& block, double beat) {
    const auto span = block.beats.end - block.beats.start;
    if (!(span > 0.0))
        return block.seconds.start;

    return block.seconds.start +
           (((beat - block.beats.start) / span) * (block.seconds.end - block.seconds.start));
}

/// Whether the material runs on from the last block. Material beat zero is a
/// run beginning here (LaunchHandle::virtualStart), which is a discontinuity.
inline bool runsOn(const BlockInfo& block, const BeatRange& range, const RunOrigin& origin) {
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
                               const RunOrigin& origin) {
    auto material = block;
    material.beats = BeatRange{block.beats.start - origin.beat, block.beats.end - origin.beat};
    material.seconds =
        SecondsRange{block.seconds.start - origin.seconds, block.seconds.end - origin.seconds};
    material.continuous = runsOn(block, range, origin);

    // Both axes moved, by different amounts, so the project's map would answer
    // about the timeline. Dropped, so beatAtTime falls back to this block's own
    // two ends, which are the material's.
    material.tempo = nullptr;
    return material;
}

/// The same, cropped to @p range and the @p count samples it fills, for a
/// reader handed a sub-block of the output.
inline BlockInfo materialSubBlock(const BlockInfo& block, const BeatRange& range,
                                  const RunOrigin& origin, int count) {
    auto material = block;
    material.numSamples = count;
    material.beats = BeatRange{range.start - origin.beat, range.end - origin.beat};
    material.seconds = SecondsRange{secondsWithin(block, range.start) - origin.seconds,
                                    secondsWithin(block, range.end) - origin.seconds};
    material.continuous = runsOn(block, range, origin);
    material.tempo = nullptr;  // see materialBlock
    return material;
}

/// Where in @p block the event landed, or the whole block when there was none.
/// What starts a clip on its beat rather than on a callback boundary.
inline int splitSample(const BlockInfo& block, const SplitStatus& status) {
    return status.afterEvent ? block.sampleForBeat(status.beforeEvent.range.end) : block.numSamples;
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
