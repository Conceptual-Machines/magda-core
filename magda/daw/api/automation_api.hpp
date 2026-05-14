#pragma once

#include "../core/AutomationInfo.hpp"
#include "../core/AutomationTypes.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

/**
 * Abstract view onto AutomationManager — the subset the agent layer uses.
 *
 * PR1 exposes what AutomationExecutor needs (lane lookup, lane creation,
 * point writes, batch coalescing). Subsequent PRs grow the surface.
 */
class AutomationApi {
  public:
    virtual ~AutomationApi() = default;

    virtual AutomationLaneId createLane(const AutomationTarget& target,
                                        AutomationLaneType type) = 0;
    virtual AutomationLaneId getLaneForTarget(const AutomationTarget& target) const = 0;

    virtual AutomationLaneInfo* getLane(AutomationLaneId laneId) = 0;
    virtual const AutomationLaneInfo* getLane(AutomationLaneId laneId) const = 0;

    virtual AutomationPointId addPoint(AutomationLaneId laneId, double beatPosition, double value,
                                       AutomationCurveType curveType) = 0;
    virtual void clearLanePoints(AutomationLaneId laneId) = 0;

    virtual void beginNotificationBatch() = 0;
    virtual void endNotificationBatch() = 0;

    /// RAII helper that brackets a write batch on this AutomationApi.
    /// Replaces direct use of AutomationManager::BatchScope so callers
    /// don't reach back to the singleton.
    class BatchScope {
      public:
        explicit BatchScope(AutomationApi& api) : api_(api) {
            api_.beginNotificationBatch();
        }
        ~BatchScope() {
            api_.endNotificationBatch();
        }
        BatchScope(const BatchScope&) = delete;
        BatchScope& operator=(const BatchScope&) = delete;

      private:
        AutomationApi& api_;
    };
};

}  // namespace magda
