#include "automation_api_live.hpp"

#include "../core/AutomationManager.hpp"

namespace magda {

AutomationLaneId AutomationApiLive::createLane(const AutomationTarget& target,
                                               AutomationLaneType type) {
    return AutomationManager::getInstance().createLane(target, type);
}

AutomationLaneId AutomationApiLive::getLaneForTarget(const AutomationTarget& target) const {
    return AutomationManager::getInstance().getLaneForTarget(target);
}

AutomationLaneInfo* AutomationApiLive::getLane(AutomationLaneId laneId) {
    return AutomationManager::getInstance().getLane(laneId);
}

const AutomationLaneInfo* AutomationApiLive::getLane(AutomationLaneId laneId) const {
    return AutomationManager::getInstance().getLane(laneId);
}

AutomationPointId AutomationApiLive::addPoint(AutomationLaneId laneId, double beatPosition,
                                              double value, AutomationCurveType curveType) {
    return AutomationManager::getInstance().addPoint(laneId, beatPosition, value, curveType);
}

void AutomationApiLive::clearLanePoints(AutomationLaneId laneId) {
    AutomationManager::getInstance().clearLanePoints(laneId);
}

void AutomationApiLive::beginNotificationBatch() {
    AutomationManager::getInstance().beginNotificationBatch();
}

void AutomationApiLive::endNotificationBatch() {
    AutomationManager::getInstance().endNotificationBatch();
}

}  // namespace magda
