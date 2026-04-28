#pragma once

#include "selection_api.hpp"

namespace magda {

/// Forwards every SelectionApi call to SelectionManager::getInstance().
class SelectionApiLive : public SelectionApi {
  public:
    TrackId getSelectedTrack() const override;
    bool hasAutomationLaneSelection() const override;
    const AutomationLaneSelection& getAutomationLaneSelection() const override;
};

}  // namespace magda
