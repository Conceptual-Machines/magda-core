#pragma once

#include "automation_api.hpp"

namespace magda {

/// Forwards every AutomationApi call to AutomationManager::getInstance().
class AutomationApiLive : public AutomationApi {
  public:
    AutomationLaneId createLane(const AutomationTarget& target, AutomationLaneType type) override;
    AutomationLaneId getLaneForTarget(const AutomationTarget& target) const override;

    AutomationLaneInfo* getLane(AutomationLaneId laneId) override;
    const AutomationLaneInfo* getLane(AutomationLaneId laneId) const override;

    AutomationPointId addPoint(AutomationLaneId laneId, double time, double value,
                               AutomationCurveType curveType) override;
    void clearLanePoints(AutomationLaneId laneId) override;

    void beginNotificationBatch() override;
    void endNotificationBatch() override;
};

}  // namespace magda
