#pragma once

#include "clip/ClipSnapshot.hpp"
#include "exec/RenderContext.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * @file SessionPlayback.hpp
 * @brief Where a launched slot's material is, in the block that plays it.
 *
 * A slot has no position: it is compiled at the origin and a launch handle says
 * where its run began (ClipSnapshot.hpp, LaunchHandle.hpp). So playing one is
 * the same reading of the same material as the arrangement's, over a block
 * whose beat axis has been moved onto that origin.
 *
 * Shared by the audio and MIDI sources because it is the same move for both,
 * and they differ only in what they do with a block once they have it: audio
 * fills a sub-block of the output and needs the sub-range's own length, MIDI
 * writes into the whole buffer at sample offsets and needs the whole block with
 * the emission bounded instead.
 */

namespace magda::engine {

/**
 * @brief Where a beat inside @p block is on the block's own seconds axis.
 *
 * The block's own line rather than a tempo map, deliberately, and it is the
 * line `sampleForBeat` and `sampleForTime` are already drawn on: a sub-range's
 * seconds and its sample count have to be two views of one stretch, or a reader
 * consumes a different amount of material than the block renders.
 */
inline double secondsWithin(const BlockInfo& block, double beat) {
    const auto span = block.endBeat - block.startBeat;
    if (!(span > 0.0))
        return block.startSeconds;

    return block.startSeconds +
           (((beat - block.startBeat) / span) * (block.endSeconds - block.startSeconds));
}

/**
 * @brief Whether the material runs on from the last block.
 *
 * A run's own beginning is where the material is at beat zero, because the
 * handle's virtual origin is the sub-range's own start exactly when the run
 * began there (LaunchHandle::virtualStart). Calling that continuous would carry
 * a voice straight across the jump, which is a step in the waveform with
 * nothing to take it out, and would leave a MIDI clip unchased at its own first
 * beat.
 */
inline bool runsOn(const BlockInfo& block, const BeatRange& range, double playStart) {
    return block.continuous && (range.start - playStart) > 0.0;
}

/**
 * @brief @p block on the axis of a run that began at @p playStart.
 *
 * The same samples of the same callback, the same seconds of buffer, a
 * different stretch of material underneath. Everything derived from the block's
 * own two ends therefore keeps working, which is what lets a caller ask this
 * block where a material beat lands in the output and get the sample the
 * timeline beat would have landed on.
 *
 * ## Two faces, each from its own domain
 *
 * The beat face is the material's own beat: the timeline beat less the run's
 * origin in beats, which is what an auto tempo or warped event is read against.
 *
 * The seconds face is how long the run has been going: the block's seconds less
 * the run's origin in seconds, which is what an event played at its file's own
 * speed is read against. Two subtractions rather than one subtraction and a
 * conversion, and the handle projects each origin from its own monotonic domain
 * (LaunchHandle.hpp). Deriving the seconds from the beats would give where that
 * beat sits rather than how long the run has lasted: the two differ by every
 * tempo change between the origin and now, and after a loop wrap the projected
 * origin is a beat in a cycle the run never played (#2324).
 *
 * Both faces come out identical at one constant tempo with no wraps, which is
 * where the distinction is invisible and where it would have been discovered
 * late.
 *
 * The remaining approximation is the compile's rather than this: a slot's spans
 * and fades were resolved at the origin's tempo (ClipSnapshot.hpp), so under a
 * tempo curve a slot's audible length in seconds is the origin's answer. What
 * it takes to make a slot follow the tempo it is launched into is a compile
 * question and belongs with the rest of the launcher's behaviour (#2306).
 *
 * @p range is the sub-range of the block the run is playing, and all it decides
 * here is @ref runsOn: the block is the whole block, and bounding the emission
 * to the sub-range is the caller's.
 */
inline BlockInfo materialBlock(const BlockInfo& block, const BeatRange& range, double playStart,
                               double playStartSeconds) {
    auto material = block;
    material.startBeat = block.startBeat - playStart;
    material.endBeat = block.endBeat - playStart;
    material.startSeconds = block.startSeconds - playStartSeconds;
    material.endSeconds = block.endSeconds - playStartSeconds;
    material.continuous = runsOn(block, range, playStart);

    // The project's map is not this block's map any more. Both of its axes have
    // moved, and by different amounts, so asking the map where a material
    // second falls would answer about the timeline: for an auto tempo event,
    // whose position is a question about beats, that is a beat the material
    // never had. Dropped, so `BlockInfo::beatAtTime` falls back to the block's
    // own two ends, which are the material's own two ends and are exact there
    // (#2324).
    material.tempo = nullptr;
    return material;
}

/**
 * @brief The same, cropped to @p range and to the @p count samples it fills.
 *
 * For a reader that is handed a sub-block of the output rather than the whole
 * of it, which is the audio side: the sample arithmetic then lands inside the
 * piece it was given instead of inside the callback. The ends go through the
 * block's own line for the reason @ref secondsWithin gives, which is the same
 * line @p count was measured on.
 */
inline BlockInfo materialSubBlock(const BlockInfo& block, const BeatRange& range, double playStart,
                                  double playStartSeconds, int count) {
    auto material = block;
    material.numSamples = count;
    material.startBeat = range.start - playStart;
    material.endBeat = range.end - playStart;
    material.startSeconds = secondsWithin(block, range.start) - playStartSeconds;
    material.endSeconds = secondsWithin(block, range.end) - playStartSeconds;
    material.continuous = runsOn(block, range, playStart);
    material.tempo = nullptr;  // see materialBlock
    return material;
}

/**
 * @brief Where in @p block the launch or the stop landed.
 *
 * The whole block when nothing happened in it, which makes the first sub-range
 * the whole of it and the second one empty. This is the sample that keeps a
 * clip starting on its beat rather than on a callback boundary.
 */
inline int splitSample(const BlockInfo& block, const SplitStatus& status) {
    return status.isSplit ? block.sampleForBeat(status.range1.end) : block.numSamples;
}

/**
 * @brief Every slot of @p track that has a handle, and what the block was for it.
 *
 * @p fn is called with `(const SessionSlotPlayback&, const SplitStatus&)`, in
 * scene order. The status is what the launcher already worked out before
 * anything rendered, so a slot's two sources see one answer rather than each
 * advancing the handle for itself (SessionLauncher.hpp).
 *
 * One walk rather than a lookup per slot: the snapshot holds a track's slots in
 * scene order and the table holds its handles in the same order, so the two are
 * merged. They can disagree about which scenes exist, since each is published
 * on its own schedule; a slot with no handle is one the table has not caught up
 * with, and it is silent for that block rather than an error.
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
