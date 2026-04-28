#pragma once

#include "selection_api.hpp"

namespace magda {

/// Forwards every SelectionApi call to SelectionManager::getInstance().
class SelectionApiLive : public SelectionApi {
  public:
    TrackId getSelectedTrack() const override;
    const std::unordered_set<ClipId>& getSelectedClips() const override;

    AutomationLaneId getSelectedAutomationLaneId() const override;

    void selectTracks(const std::unordered_set<TrackId>& trackIds) override;
    void selectClips(const std::unordered_set<ClipId>& clipIds) override;
};

}  // namespace magda
