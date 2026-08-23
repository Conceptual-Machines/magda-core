#pragma once

#include "clip/ClipSnapshot.hpp"
#include "clip/ClipStretcher.hpp"
#include "exec/RenderContext.hpp"
#include "io/ClipPlacement.hpp"
#include "io/SourceReaders.hpp"

/**
 * @file EventPlacement.hpp
 * @brief One event, turned into a file to read and a position to read it at.
 *
 * The snapshot holds an event the way the model does: source-domain counts at
 * the source's own rate, plus the flags that say what to do with them. Playing
 * it needs two things instead, and they have to agree with each other. What the
 * file is read through is one (io/SourceReaders.hpp), and where in that reading
 * the event's first sample sits is the other (io/ClipPlacement.hpp).
 *
 * Both from here, and both pure functions of the event, because the two sides
 * that need them never meet. The pool opens the file and points the reader; the
 * voice, a block later and on the audio thread, asks where the timeline is in
 * it. If each worked out reverse and looping for itself the two could disagree,
 * and what a listener would hear is a clip playing the wrong part of its file
 * with nothing to say which of them was wrong.
 *
 * Reverse is the whole reason this file exists. Everything else is carried
 * through unchanged and mirroring is a coordinate change: the model holds a
 * reversed event's anchor and loop region in the original file's domain, as the
 * editors and the project file do, and the reading happens in the mirrored one.
 * Turning one into the other is a couple of lines, and they have to be the same
 * couple of lines on both sides.
 */

namespace magda::engine {

/// What the file behind @p event is read through: mirrored where it plays
/// backwards, tiled where it loops, converted where the source's rate is not
/// @p deviceSampleRate.
SourceRead sourceReadFor(const AudioEventPlayback& event, double deviceSampleRate);

/// Where @p event sits on the timeline, and the sample of the reading above
/// that plays at its start. In the device's samples, because that is what the
/// reading delivers and what the callback consumes one of per output sample.
ClipPlacement placementFor(const AudioEventPlayback& event, double deviceSampleRate);

/**
 * @brief Reading samples consumed per output sample, at @p event's usual rate.
 *
 * One at unity, which is every clip in slice 3. What makes it something else is
 * the speed ratio, or auto tempo, and for auto tempo this is an average over the
 * event rather than a rate that holds: the ratio there is the project's tempo
 * over the file's own and moves with the tempo curve. Nothing plays at this. It
 * is what a stretcher is sized and primed against, where being roughly right is
 * the whole requirement.
 *
 * Clamped to what a stretcher will run at, which is what makes a block's worth
 * of reading a bounded quantity and therefore something a scratch buffer can be
 * sized for before the callback.
 */
double readingRateOf(const AudioEventPlayback& event);

/**
 * @brief Where in the reading @p event is at one instant of the timeline.
 *
 * In fractional device samples, and the whole of what this slice adds: speed
 * ratios, auto tempo, analog pitch and speed ramps are all this one function
 * answering differently, and warp (#2038) will be another answer from here.
 *
 * Both faces of the instant are taken because which one is authoritative
 * depends on the event. A constant ratio is a fact about seconds: so many
 * samples of file per second of timeline, whatever the tempo does. Auto tempo
 * is not, and cannot be resolved to seconds ahead of the block, because its
 * ratio is the project's tempo over the file's and moves with the tempo curve.
 * What saves it is that the integral of that ratio is beats: the material
 * consumed by an instant is how many beats have passed since the event began,
 * times the seconds a beat of the file is worth. Exact, and needs only the beat
 * face the clock already publishes rather than a second tempo map down here.
 *
 * @p clip is taken for its fades, because a fade whose behaviour is a speed
 * ramp is not a gain envelope at all: it is a warp of this position near the
 * clip's edges (FadeCurves.hpp).
 */
double readingPositionAt(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                         double seconds, double beat, double deviceSampleRate);

/// What @p event needs stretching with, or a setup no stretcher is made for
/// when it plays at its file's own speed with nothing asked of its pitch.
/// @p clip is taken for its fades, which decide the same question at its edges.
StretchSetup stretchSetupFor(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                             const RenderContext& context);

}  // namespace magda::engine
