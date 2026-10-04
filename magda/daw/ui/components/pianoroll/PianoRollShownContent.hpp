#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "core/ClipTypes.hpp"
#include "core/TypeIds.hpp"

namespace magda {

/**
 * @brief What a piano roll's grid shows: a track's timeline in one view, or in relative mode its
 *        clips.
 *
 * The editor fits its view (clip start, notes centred) only when this changes, so re-selecting a
 * clip, coming back to the editor or clicking between clips already on screen keeps the view.
 */
struct PianoRollShownContent {
    TrackId track = INVALID_TRACK_ID;
    ClipView view = ClipView::Arrangement;
    bool relative = false;
    std::vector<ClipId> clips;

    bool operator==(const PianoRollShownContent&) const = default;

    static PianoRollShownContent of(TrackId track, ClipView view, bool relative,
                                    std::vector<ClipId> clips) {
        PianoRollShownContent content{track, view, relative, {}};
        if (relative) {
            std::ranges::sort(clips);
            content.clips = std::move(clips);
        }
        return content;
    }
};

/// Records @p next as shown; true when it differs from what @p shown held.
inline bool takeNewContent(std::optional<PianoRollShownContent>& shown,
                           PianoRollShownContent next) {
    if (shown == next)
        return false;
    shown = std::move(next);
    return true;
}

}  // namespace magda
