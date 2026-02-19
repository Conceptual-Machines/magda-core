#pragma once

#include <juce_events/juce_events.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "AutomationInfo.hpp"
#include "AutomationTypes.hpp"
#include "TrackManager.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Listener interface for automation changes
 */
class AutomationManagerListener {
  public:
    virtual ~AutomationManagerListener() = default;

    // Called when lanes are added, removed, or reordered
    virtual void automationLanesChanged() = 0;

    // Called when a specific lane's properties change
    virtual void automationLanePropertyChanged(AutomationLaneId laneId) {
        juce::ignoreUnused(laneId);
    }

    // Called when automation clips change on a lane
    virtual void automationClipsChanged(AutomationLaneId laneId) {
        juce::ignoreUnused(laneId);
    }

    // Called when points change (added, removed, moved)
    virtual void automationPointsChanged(AutomationLaneId laneId) {
        juce::ignoreUnused(laneId);
    }

    // Called when a point is being dragged (for preview)
    virtual void automationPointDragPreview(AutomationLaneId laneId, AutomationPointId pointId,
                                            double previewTime, double previewValue) {
        juce::ignoreUnused(laneId, pointId, previewTime, previewValue);
    }
};

/**
 * @brief Singleton manager for automation data
 *
 * Provides CRUD operations for automation lanes, clips, and points.
 * Handles curve interpolation for real-time value retrieval.
 * Listens to TrackManager for volume/pan changes to update automation lanes.
 */
class AutomationManager : public TrackManagerListener {
  public:
    static AutomationManager& getInstance();

    // Prevent copying
    AutomationManager(const AutomationManager&) = delete;
    AutomationManager& operator=(const AutomationManager&) = delete;

    // ========================================================================
    // Lane Management
    // ========================================================================

    /**
     * @brief Create a new automation lane
     * @param target What the lane automates
     * @param type Absolute or clip-based
     * @return The ID of the new lane
     */
    AutomationLaneId createLane(const AutomationTarget& target, AutomationLaneType type);

    /**
     * @brief Get or create a lane for a target
     * @param target What to automate
     * @param type Lane type if creating
     * @return Lane ID (existing or new)
     */
    AutomationLaneId getOrCreateLane(const AutomationTarget& target, AutomationLaneType type);

    /**
     * @brief Delete an automation lane
     */
    void deleteLane(AutomationLaneId laneId);

    /**
     * @brief Get lane info by ID
     */
    AutomationLaneInfo* getLane(AutomationLaneId laneId);
    const AutomationLaneInfo* getLane(AutomationLaneId laneId) const;

    /**
     * @brief Get all lanes
     */
    const std::vector<AutomationLaneInfo>& getLanes() const {
        return lanes_;
    }

    /**
     * @brief Get all clips
     */
    const std::vector<AutomationClipInfo>& getClips() const {
        return clips_;
    }

    /**
     * @brief Get lanes for a specific track
     */
    std::vector<AutomationLaneId> getLanesForTrack(TrackId trackId) const;

    /**
     * @brief Get lane for a specific target
     * @return INVALID_AUTOMATION_LANE_ID if not found
     */
    AutomationLaneId getLaneForTarget(const AutomationTarget& target) const;

    // ========================================================================
    // Lane Properties
    // ========================================================================

    void setLaneName(AutomationLaneId laneId, const juce::String& name);
    void setLaneVisible(AutomationLaneId laneId, bool visible);
    void setLaneExpanded(AutomationLaneId laneId, bool expanded);
    void setLaneArmed(AutomationLaneId laneId, bool armed);
    void setLaneHeight(AutomationLaneId laneId, int height);

    // ========================================================================
    // Automation Clips (for clip-based lanes)
    // ========================================================================

    /**
     * @brief Create an automation clip on a lane
     */
    AutomationClipId createClip(AutomationLaneId laneId, double startTime, double length);

    /**
     * @brief Delete an automation clip
     */
    void deleteClip(AutomationClipId clipId);

    /**
     * @brief Get clip info by ID
     */
    AutomationClipInfo* getClip(AutomationClipId clipId);
    const AutomationClipInfo* getClip(AutomationClipId clipId) const;

    /**
     * @brief Move a clip to a new position
     */
    void moveClip(AutomationClipId clipId, double newStartTime);

    /**
     * @brief Resize a clip
     */
    void resizeClip(AutomationClipId clipId, double newLength, bool fromStart = false);

    /**
     * @brief Duplicate a clip
     */
    AutomationClipId duplicateClip(AutomationClipId clipId);

    // ========================================================================
    // Clip Properties
    // ========================================================================

    void setClipName(AutomationClipId clipId, const juce::String& name);
    void setClipColour(AutomationClipId clipId, juce::Colour colour);
    void setClipLooping(AutomationClipId clipId, bool looping);
    void setClipLoopLength(AutomationClipId clipId, double length);

    // ========================================================================
    // Point Management (Absolute lanes)
    // ========================================================================

    /**
     * @brief Add a point to an absolute lane
     */
    AutomationPointId addPoint(AutomationLaneId laneId, double time, double value,
                               AutomationCurveType curveType = AutomationCurveType::Linear);

    /**
     * @brief Add a point to a clip
     */
    AutomationPointId addPointToClip(AutomationClipId clipId, double localTime, double value,
                                     AutomationCurveType curveType = AutomationCurveType::Linear);

    /**
     * @brief Delete a point from a lane
     */
    void deletePoint(AutomationLaneId laneId, AutomationPointId pointId);

    /**
     * @brief Delete a point from a clip
     */
    void deletePointFromClip(AutomationClipId clipId, AutomationPointId pointId);

    /**
     * @brief Move a point to a new time/value
     */
    void movePoint(AutomationLaneId laneId, AutomationPointId pointId, double newTime,
                   double newValue);

    /**
     * @brief Move a point within a clip
     */
    void movePointInClip(AutomationClipId clipId, AutomationPointId pointId, double newTime,
                         double newValue);

    /**
     * @brief Set bezier handles for a point
     */
    void setPointHandles(AutomationLaneId laneId, AutomationPointId pointId,
                         const BezierHandle& inHandle, const BezierHandle& outHandle);

    /**
     * @brief Set bezier handles for a point in a clip
     */
    void setPointHandlesInClip(AutomationClipId clipId, AutomationPointId pointId,
                               const BezierHandle& inHandle, const BezierHandle& outHandle);

    /**
     * @brief Set curve type for a point
     */
    void setPointCurveType(AutomationLaneId laneId, AutomationPointId pointId,
                           AutomationCurveType curveType);

    /**
     * @brief Set tension for a curve segment
     * @param tension Range -1.0 (concave) to 0.0 (linear) to +1.0 (convex)
     */
    void setPointTension(AutomationLaneId laneId, AutomationPointId pointId, double tension);

    /**
     * @brief Set tension for a curve segment in a clip
     */
    void setPointTensionInClip(AutomationClipId clipId, AutomationPointId pointId, double tension);

    // ========================================================================
    // Value Interpolation
    // ========================================================================

    /**
     * @brief Get interpolated value at a time on a lane
     * @param laneId Lane to query
     * @param time Time in seconds
     * @return Normalized value 0-1 (0.5 if no points)
     */
    double getValueAtTime(AutomationLaneId laneId, double time) const;

    /**
     * @brief Get interpolated value at a time within a clip
     * @param clipId Clip to query
     * @param localTime Time within clip (0 to length)
     * @return Normalized value 0-1
     */
    double getClipValueAtTime(AutomationClipId clipId, double localTime) const;

    // ========================================================================
    // Listener Management
    // ========================================================================

    void addListener(AutomationManagerListener* listener);
    void removeListener(AutomationManagerListener* listener);

    /**
     * @brief Broadcast point drag preview event
     */
    void notifyPointDragPreview(AutomationLaneId laneId, AutomationPointId pointId,
                                double previewTime, double previewValue);

    // ========================================================================
    // Project Management
    // ========================================================================

    void clearAll();

    /**
     * @brief Restore a lane from deserialized data (project load)
     */
    void restoreLane(AutomationLaneInfo& lane);

    /**
     * @brief Restore a clip from deserialized data (project load)
     */
    void restoreClip(AutomationClipInfo& clip);

    /**
     * @brief Update ID counters to avoid collisions after restoring lanes/clips
     */
    void refreshIdCountersFromLanes();

    // ========================================================================
    // TrackManagerListener - Updates automation when faders move
    // ========================================================================

    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;

  private:
    AutomationManager();
    ~AutomationManager();

    std::vector<AutomationLaneInfo> lanes_;
    std::vector<AutomationClipInfo> clips_;
    juce::ListenerList<AutomationManagerListener> listeners_;

    int nextLaneId_ = 1;
    int nextClipId_ = 1;
    int nextPointId_ = 1;

    // Notification helpers
    void notifyLanesChanged();
    void notifyLanePropertyChanged(AutomationLaneId laneId);
    void notifyClipsChanged(AutomationLaneId laneId);
    void notifyPointsChanged(AutomationLaneId laneId);

    // Interpolation helpers
    double interpolateLinear(double t, double v1, double v2) const;
    double interpolateBezier(double t, const AutomationPoint& p1, const AutomationPoint& p2) const;
    double interpolatePoints(const std::vector<AutomationPoint>& points, double time) const;

    // Point management helpers
    AutomationPoint* findPoint(std::vector<AutomationPoint>& points, AutomationPointId pointId);
    const AutomationPoint* findPoint(const std::vector<AutomationPoint>& points,
                                     AutomationPointId pointId) const;
    void sortPoints(std::vector<AutomationPoint>& points);
};

}  // namespace magda
