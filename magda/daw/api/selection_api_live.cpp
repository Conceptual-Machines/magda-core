#include "selection_api_live.hpp"

namespace magda {

TrackId SelectionApiLive::getSelectedTrack() const {
    return SelectionManager::getInstance().getSelectedTrack();
}

bool SelectionApiLive::hasAutomationLaneSelection() const {
    return SelectionManager::getInstance().hasAutomationLaneSelection();
}

const AutomationLaneSelection& SelectionApiLive::getAutomationLaneSelection() const {
    return SelectionManager::getInstance().getAutomationLaneSelection();
}

}  // namespace magda
