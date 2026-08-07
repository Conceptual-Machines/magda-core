#pragma once

#include "clip/ClipSnapshot.hpp"
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

}  // namespace magda::engine
