#pragma once

#include "selection_api.hpp"

namespace magda {

/// Forwards every SelectionApi call to SelectionManager::getInstance().
class SelectionApiLive : public SelectionApi {
  public:
    TrackId getSelectedTrack() const override;
    AutomationLaneId getSelectedAutomationLaneId() const override;
};

}  // namespace magda
