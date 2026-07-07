#include "PasteTargetResolver.hpp"

#include "SelectionManager.hpp"
#include "TrackManager.hpp"

namespace magda {

PasteTarget resolvePasteTarget(ViewMode mode, PasteTrackMode trackMode,
                               const PasteInvocation& invocation) {
    // Ripple paste keeps every clip on its own source track: there is no single
    // target, and INVALID_TRACK_ID is the correct, expected value.
    if (trackMode == PasteTrackMode::PreserveOriginalTracks)
        return {.trackId = INVALID_TRACK_ID, .ok = true};

    auto& trackManager = TrackManager::getInstance();

    // A context invocation (right-click on a clip, a track area, or a session
    // slot) has an explicit target: land on that track, or abort. Never fall
    // through to the selection. If the context track disappeared between
    // opening the async menu and running the action, migrating the paste to
    // whatever is selected is the wrong-track failure this resolver exists to
    // prevent; a no-op is the safe answer.
    if (invocation.kind == PasteInvocation::Kind::ContextClip) {
        if (invocation.contextTrackId != INVALID_TRACK_ID &&
            trackManager.getTrack(invocation.contextTrackId) != nullptr)
            return {.trackId = invocation.contextTrackId, .ok = true};
        return {.trackId = INVALID_TRACK_ID, .ok = false};
    }

    // Selection-based: the selected track, else the first visible track in this
    // view, else the first visible track in the arrangement.
    const auto selectedTrack = SelectionManager::getInstance().getSelectedTrack();
    if (selectedTrack != INVALID_TRACK_ID && trackManager.getTrack(selectedTrack) != nullptr)
        return {.trackId = selectedTrack, .ok = true};

    auto visibleTracks = trackManager.getVisibleTracks(mode);
    if (!visibleTracks.empty())
        return {.trackId = visibleTracks.front(), .ok = true};

    if (mode != ViewMode::Arrange) {
        visibleTracks = trackManager.getVisibleTracks(ViewMode::Arrange);
        if (!visibleTracks.empty())
            return {.trackId = visibleTracks.front(), .ok = true};
    }

    return {.trackId = INVALID_TRACK_ID, .ok = false};
}

}  // namespace magda
