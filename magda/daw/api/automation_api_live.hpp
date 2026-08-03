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

    AutomationPointId addPoint(AutomationLaneId laneId, double beatPosition, double value,
                               AutomationCurveType curveType) override;
    void clearLanePoints(AutomationLaneId laneId) override;

    const std::vector<AutomationLaneInfo>& getLanes() const override;
    std::vector<AutomationLaneId> getLanesForTrack(TrackId trackId) const override;
    std::vector<AutomationLaneId> getEditScopedLanes() const override;
    const std::vector<AutomationClipInfo>& getClips() const override;
    bool setLanePoints(AutomationLaneId laneId, std::vector<AutomationPoint> points) override;
    bool deleteLane(AutomationLaneId laneId) override;
    bool retypeEmptyLane(AutomationLaneId laneId, AutomationLaneType type) override;
    AutomationClipId createClip(AutomationLaneId laneId, double startBeats,
                                double lengthBeats) override;
    AutomationClipInfo* getClip(AutomationClipId clipId) override;
    const AutomationClipInfo* getClip(AutomationClipId clipId) const override;
    void deleteClip(AutomationClipId clipId) override;
    void moveClip(AutomationClipId clipId, double startBeats) override;
    void resizeClip(AutomationClipId clipId, double lengthBeats, bool fromStart) override;
    AutomationClipId duplicateClip(AutomationClipId clipId) override;
    void setClipName(AutomationClipId clipId, const juce::String& name) override;
    void setClipColour(AutomationClipId clipId, juce::Colour colour) override;
    void setClipLooping(AutomationClipId clipId, bool looping) override;
    void setClipLoopLength(AutomationClipId clipId, double lengthBeats) override;
    void setClipPoints(AutomationClipId clipId, std::vector<AutomationPoint> points) override;

    void beginNotificationBatch() override;
    void endNotificationBatch() override;
};

}  // namespace magda
