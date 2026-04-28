#include "selection_api_live.hpp"

#include "../core/SelectionManager.hpp"

namespace magda {

TrackId SelectionApiLive::getSelectedTrack() const {
    return SelectionManager::getInstance().getSelectedTrack();
}

AutomationLaneId SelectionApiLive::getSelectedAutomationLaneId() const {
    auto& sel = SelectionManager::getInstance();
    if (!sel.hasAutomationLaneSelection())
        return INVALID_AUTOMATION_LANE_ID;
    return sel.getAutomationLaneSelection().laneId;
}

}  // namespace magda
