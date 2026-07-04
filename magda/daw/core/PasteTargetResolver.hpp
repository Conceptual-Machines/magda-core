#pragma once

#include <cstdint>

#include "TypeIds.hpp"
#include "ViewModeState.hpp"

namespace magda {

/**
 * @brief The single authority for computing a clipboard-paste target track.
 *
 * Historically every paste entry point (keyboard command, context menu,
 * track-area menu, session slot) computed its own target track, drawing from
 * two different selection models. That is how the paste-on-wrong-track bug
 * (#1670) happened and how the class recurs. This resolver is the ONLY code
 * allowed to compute a paste target: new entry points describe where they were
 * invoked from and what track semantic they want, and get back one answer.
 *
 * Scope: user-facing "Paste" of the clip clipboard. Duplicate-in-place ops
 * (Duplicate Time Range / Loop / Selection) are a different operation whose
 * paste step is definitionally PreserveOriginalTracks and which derive their
 * multi-track set from the time selection, not from this resolver.
 */

/// Whether a paste pins every clip onto one resolved track, or lets each clip
/// keep its own source track.
enum class PasteTrackMode : std::uint8_t {
    PinToResolvedTrack,     ///< Normal paste: all clips land on one resolved track.
    PreserveOriginalTracks  ///< Ripple paste: each clip keeps its source track.
};

/// Where the paste was invoked from. Determines how PinToResolvedTrack computes
/// its target; PreserveOriginalTracks ignores it.
struct PasteInvocation {
    enum class Kind : std::uint8_t {
        Selection,   ///< Keyboard/menu command: use the selected track, else first visible.
        ContextClip  ///< Right-click on a clip/track/slot: use that context track.
    };

    Kind kind = Kind::Selection;
    TrackId contextTrackId = INVALID_TRACK_ID;  ///< Only used for ContextClip.

    static PasteInvocation fromSelection() {
        return {.kind = Kind::Selection, .contextTrackId = INVALID_TRACK_ID};
    }
    static PasteInvocation fromContextTrack(TrackId trackId) {
        return {.kind = Kind::ContextClip, .contextTrackId = trackId};
    }
};

/// Outcome of resolution.
struct PasteTarget {
    /// Track to pass to pasteFromClipboardBeats. INVALID_TRACK_ID paired with
    /// PreserveOriginalTracks is intentional (keep each clip's source track).
    TrackId trackId = INVALID_TRACK_ID;
    /// False only for PinToResolvedTrack when no track could be resolved (the
    /// edit has no visible tracks). Callers must abort the paste in that case.
    bool ok = true;
};

/// Compute the paste target track. See the header doc for the contract.
PasteTarget resolvePasteTarget(ViewMode mode, PasteTrackMode trackMode,
                               const PasteInvocation& invocation);

}  // namespace magda
