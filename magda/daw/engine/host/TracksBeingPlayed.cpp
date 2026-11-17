// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TracksBeingPlayed.hpp"

#include "../../core/ClipManager.hpp"
#include "../../core/SelectionManager.hpp"

namespace magda::daw::engine_host {

std::set<TrackId> tracksBeingPlayed(const std::vector<TrackInfo>& tracks,
                                    const SelectionManager& selection, const ClipManager& clips) {
    std::set<TrackId> played;
    for (const auto& track : tracks)
        if (track.recordArmed || track.monitorsInput())
            played.insert(track.id);

    const auto clipTrack = [&](ClipId id) {
        if (const auto* clip = clips.getClip(id))
            played.insert(clip->trackId);
    };
    played.insert(selection.getSelectedTrack());
    played.insert(selection.getSelectedTracks().begin(), selection.getSelectedTracks().end());
    clipTrack(selection.getSelectedClip());
    for (const auto id : selection.getSelectedClips())
        clipTrack(id);
    clipTrack(selection.getNoteSelection().clipId);
    played.insert(selection.getDeviceSelection().trackId);
    played.insert(selection.getSelectedChainNode().trackId);
    played.erase(INVALID_TRACK_ID);
    return played;
}

}  // namespace magda::daw::engine_host
