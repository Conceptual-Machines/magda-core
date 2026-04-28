#include "selection_api_live.hpp"

#include "../core/SelectionManager.hpp"

namespace magda {

TrackId SelectionApiLive::getSelectedTrack() const {
    return SelectionManager::getInstance().getSelectedTrack();
}

const std::unordered_set<ClipId>& SelectionApiLive::getSelectedClips() const {
    return SelectionManager::getInstance().getSelectedClips();
}

AutomationLaneId SelectionApiLive::getSelectedAutomationLaneId() const {
    auto& sel = SelectionManager::getInstance();
    if (!sel.hasAutomationLaneSelection())
        return INVALID_AUTOMATION_LANE_ID;
    return sel.getAutomationLaneSelection().laneId;
}

void SelectionApiLive::selectTracks(const std::unordered_set<TrackId>& trackIds) {
    SelectionManager::getInstance().selectTracks(trackIds);
}

void SelectionApiLive::selectClips(const std::unordered_set<ClipId>& clipIds) {
    SelectionManager::getInstance().selectClips(clipIds);
}

}  // namespace magda
