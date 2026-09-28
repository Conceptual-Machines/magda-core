#pragma once

#include "ClipInfo.hpp"
#include "TrackInfo.hpp"

/**
 * @file ClipPlacementPolicy.hpp
 * @brief Whether a clip may be placed on a track, asked with the clip in hand.
 *
 * TrackTypeTraits says what a lane accepts by kind, which is as much as a file
 * drop knows before it has read anything. Moving an existing clip knows more,
 * and for one lane the difference decides the answer: the chord track holds
 * progressions, and whether a MIDI clip is one is a fact about that clip rather
 * than about MIDI.
 *
 * Kept here rather than on TrackInfo so that core's track header does not have
 * to know what a clip is.
 */

namespace magda {

/**
 * @brief Whether @p clip may be placed on @p track.
 *
 * Every path that moves a clip asks this: the drag, the keyboard nudge, and
 * MoveClipToTrackCommand itself, which is what makes it hold for the paths
 * nobody has written yet. The command is the one that must, because a UI that
 * only declines to offer a bad target still leaves the command reachable from
 * the API, the scripting layer and whatever comes next.
 */
inline bool trackAcceptsClip(const TrackInfo& track, const ClipInfo& clip) {
    switch (traitsOf(track.type).userClips) {
        case UserClipAcceptance::None:
            return false;

        case UserClipAcceptance::Any:
            return true;

        case UserClipAcceptance::MidiOnly:
            return clip.isMidi();

        case UserClipAcceptance::Progressions:
            // Moving is not importing. An import reads a file and can work the
            // harmony out on the way in, which is what dropping a .mid on the
            // chord track does. A move can only take the clip as it already is,
            // so what it takes is a clip that is already a progression --
            // silently rewriting the contents of a clip somebody dragged would
            // be a conversion wearing a move's undo step.
            return clip.isMidi() && !clip.chordAnnotations.empty();
    }

    return false;
}

}  // namespace magda
