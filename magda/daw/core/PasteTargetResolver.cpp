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
    // slot) targets that track directly. Fall through to selection-based
    // resolution only if the context track has since disappeared.
    if (invocation.kind == PasteInvocation::Kind::ContextClip &&
        invocation.contextTrackId != INVALID_TRACK_ID &&
        trackManager.getTrack(invocation.contextTrackId) != nullptr) {
        return {.trackId = invocation.contextTrackId, .ok = true};
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
