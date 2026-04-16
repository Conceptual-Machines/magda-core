#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <optional>
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

    // Called whenever a lane's effective value (at the current playhead or
    // during a drag preview) changes. UI controls use this to follow
    // automation without polling: drag previews, stopped commits, and
    // playback evaluation all route through here. The value is the MAGDA
    // 0..1 normalized form; listeners convert to their display domain.
    virtual void automationValueChanged(AutomationLaneId laneId, double normalizedValue) {
        juce::ignoreUnused(laneId, normalizedValue);
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
    void setLaneBypass(AutomationLaneId laneId, bool bypass);

    // Global session-only show/hide: when false, all automation lane viewports
    // are hidden across every track regardless of per-lane `visible` flags.
    bool isGlobalLaneVisibilityEnabled() const {
        return globalLaneVisibilityEnabled_;
    }
    void setGlobalLaneVisibility(bool enabled);

    /**
     * @brief Transient touch suppression for a target.
     *
     * Called by UI controls on mouseDown / mouseUp during playback so the
     * playback engine stops writing into the parameter while the user is
     * dragging it. Looks up the lane by target; no-op if no lane exists.
     * The flag is not serialized and not surfaced via
     * automationLanePropertyChanged — listeners that need to react use
     * setTouchSuppressionListener().
     */
    void setTargetTouchSuppressed(const AutomationTarget& target, bool suppressed);

    // Tracks which automation targets the user is actively manipulating via
    // a mouse/touch gesture. Unlike touchSuppressed, this is set even when no
    // lane exists yet, so the recording engine can tell "user is touching" from
    // "playback engine echoed a curve value back into TrackManager" during
    // active playback — preventing feedback loops that overwrite baked curves.
    void setTargetUserTouched(const AutomationTarget& target, bool touched);
    bool isTargetUserTouched(const AutomationTarget& target) const;

    /**
     * @brief Compute the visual state for a target's bound control.
     *
     * Single source of truth so widgets don't re-derive (lane exists,
     * bypass, touchSuppressed) logic in their own paint paths.
     */
    AutomationVisualState getVisualState(const AutomationTarget& target) const;

    /**
     * @brief Latch a target's lane into the "overridden" state.
     *
     * Called when the user takes over an automated control — bypasses the
     * lane so playback stops reading the curve. The user re-enables by
     * toggling the lane's power button (setLaneBypass(laneId, false)).
     * No-op if no lane exists for the target.
     */
    void setTargetOverridden(const AutomationTarget& target, bool overridden);

    // Query the real automation-write mode from AudioBridge so controls don't
    // depend on a duplicated UI-side cache that can drift out of sync.
    bool isWriteModeEnabled() const;

    // Current live normalized value for a target, independent of whether its
    // automation lane is active. Used by overridden lanes to show where the
    // user-held value sits against the stored curve.
    std::optional<double> getCurrentTargetValue(const AutomationTarget& target) const;

    using TouchSuppressionListener = std::function<void(AutomationLaneId, bool)>;
    void setTouchSuppressionListener(TouchSuppressionListener listener) {
        touchSuppressionListener_ = std::move(listener);
    }

    void setLaneSnapTime(AutomationLaneId laneId, bool snap);
    void setLaneSnapValue(AutomationLaneId laneId, bool snap);
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
     * @brief Delete all points from an absolute lane
     */
    void clearLanePoints(AutomationLaneId laneId);

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
     * @brief Set curve type for a point in a clip
     */
    void setPointCurveTypeInClip(AutomationClipId clipId, AutomationPointId pointId,
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

    // Coalesce automationPointsChanged notifications across a batch. While
    // suppression is active, notifyPointsChanged() records the affected lane
    // IDs and returns without firing. The outermost endBatch() fires one
    // notification per distinct lane. Nestable via a counter — safe to call
    // from compound undo ops that themselves wrap other batched work.
    void beginNotificationBatch();
    void endNotificationBatch();

    /**
     * @brief Broadcast point drag preview event
     */
    void notifyPointDragPreview(AutomationLaneId laneId, AutomationPointId pointId,
                                double previewTime, double previewValue);

    /**
     * @brief Broadcast a lane's effective value change to UI listeners.
     *
     * Fired by AutomationPlaybackEngine on drag preview, stopped commits, and
     * live playback-driven parameter changes. UI controls subscribe to this
     * to track automation without polling.
     *
     * @param laneId          The lane whose value is being reported.
     * @param normalizedValue The MAGDA 0..1 normalized value to display.
     */
    void notifyValueChanged(AutomationLaneId laneId, double normalizedValue);

    // ========================================================================
    // Project Management
    // ========================================================================

    void clearAll();

    /**
     * @brief Restore a lane from deserialized data (project load)
     */
    void restoreLane(AutomationLaneInfo& lane);

    /**
     * @brief Insert a lane at a specific index (used by undo after delete)
     *
     * Out-of-range indices clamp to the end, matching restoreLane's push_back
     * semantics. Fires lanesChanged so listeners rebuild.
     */
    void insertLaneAt(AutomationLaneInfo& lane, size_t index);

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

    /**
     * @brief Set by AutomationPlaybackEngine during value application
     *
     * When true, trackPropertyChanged() is suppressed to prevent feedback loops
     * where automation-driven volume/pan changes would corrupt automation curves.
     */
    void setPlaybackActive(bool active) {
        playbackActive_ = active;
    }
    bool isPlaybackActive() const {
        return playbackActive_;
    }

    /**
     * @brief Re-entrancy flag set only while AutomationPlaybackEngine is echoing
     *        an automated value back into TrackManager via setTrackVolume /
     *        setTrackPan. Listeners that would otherwise push the value back
     *        into the audio engine (AudioBridge) check this to avoid fighting
     *        TE's own automation curve, while still letting user-initiated
     *        fader/pan edits on any track reach the engine during playback.
     *
     *        Use AutomationWriteScope RAII to toggle — always pair set/unset.
     *        Single-threaded (message thread) so a plain bool is sufficient.
     */
    bool isApplyingAutomationWrite() const {
        return applyingAutomationWrite_;
    }

    struct AutomationWriteScope {
        AutomationWriteScope() {
            AutomationManager::getInstance().applyingAutomationWrite_ = true;
        }
        ~AutomationWriteScope() {
            AutomationManager::getInstance().applyingAutomationWrite_ = false;
        }
        AutomationWriteScope(const AutomationWriteScope&) = delete;
        AutomationWriteScope& operator=(const AutomationWriteScope&) = delete;
    };

  private:
    AutomationManager();
    ~AutomationManager();

    std::vector<AutomationLaneInfo> lanes_;
    std::vector<AutomationClipInfo> clips_;
    juce::ListenerList<AutomationManagerListener> listeners_;

    bool playbackActive_ = false;
    bool applyingAutomationWrite_ = false;

    // Targets under an active user touch gesture (mouseDown..mouseUp on a
    // DraggableValueLabel / TextSlider bound to this target). Separate from
    // lane-level touchSuppressed because touches start before a lane exists
    // — see setTargetUserTouched().
    std::vector<AutomationTarget> userTouchedTargets_;

    TouchSuppressionListener touchSuppressionListener_;

    int nextLaneId_ = 1;
    int nextClipId_ = 1;
    int nextPointId_ = 1;

    int notificationBatchDepth_ = 0;
    std::vector<AutomationLaneId> pendingPointsChangedLanes_;

    bool globalLaneVisibilityEnabled_ = true;

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
