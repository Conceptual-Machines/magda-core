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

    // ------------------------------------------------------------------
    // Enumeration
    // ------------------------------------------------------------------
    // What automation exists, and what it targets. Every lane carries an
    // AutomationTarget (a ControlTarget), so a caller can discover both the
    // lanes and the parameters they drive without reaching into the registry.

    /** Every lane in the project, in stable order. */
    virtual const std::vector<AutomationLaneInfo>& getLanes() const = 0;

    /** Lanes whose target resolves to `trackId`. */
    virtual std::vector<AutomationLaneId> getLanesForTrack(TrackId trackId) const = 0;

    /**
     * @brief Lanes with no owning track or device — currently tempo.
     *
     * These are edit-scoped: their target has no `devicePath`, so they are not
     * reachable through `getLanesForTrack` and would otherwise be invisible.
     */
    virtual std::vector<AutomationLaneId> getEditScopedLanes() const = 0;

    /** Every automation clip in the project. */
    virtual const std::vector<AutomationClipInfo>& getClips() const = 0;

    virtual AutomationPointId addPoint(AutomationLaneId laneId, double beatPosition, double value,
                                       AutomationCurveType curveType) = 0;
    virtual void clearLanePoints(AutomationLaneId laneId) = 0;

    /**
     * @brief Replace every point on an absolute lane as one undoable step.
     *
     * Writing a curve through repeated `addPoint` calls produces one undo entry
     * per point, so undoing a generated curve means one Undo per sample. This
     * is the bulk form. Returns false for an unknown or clip-based lane —
     * clip-based lanes keep their points on clips, which `setClipPoints`
     * covers.
     */
    virtual bool setLanePoints(AutomationLaneId laneId, std::vector<AutomationPoint> points) = 0;

    /** Remove a lane and its clips. Undoable as one step. */
    virtual bool deleteLane(AutomationLaneId laneId) = 0;

    // Clip-based automation lanes. Retyping is intentionally restricted to
    // empty lanes so an agent cannot silently discard an existing curve.
    virtual bool retypeEmptyLane(AutomationLaneId laneId, AutomationLaneType type) = 0;
    virtual AutomationClipId createClip(AutomationLaneId laneId, double startBeats,
                                        double lengthBeats) = 0;
    virtual AutomationClipInfo* getClip(AutomationClipId clipId) = 0;
    virtual const AutomationClipInfo* getClip(AutomationClipId clipId) const = 0;
    virtual void deleteClip(AutomationClipId clipId) = 0;
    virtual void moveClip(AutomationClipId clipId, double startBeats) = 0;
    virtual void resizeClip(AutomationClipId clipId, double lengthBeats, bool fromStart) = 0;
    virtual AutomationClipId duplicateClip(AutomationClipId clipId) = 0;
    virtual void setClipName(AutomationClipId clipId, const juce::String& name) = 0;
    virtual void setClipColour(AutomationClipId clipId, juce::Colour colour) = 0;
    virtual void setClipLooping(AutomationClipId clipId, bool looping) = 0;
    virtual void setClipLoopLength(AutomationClipId clipId, double lengthBeats) = 0;
    virtual void setClipPoints(AutomationClipId clipId, std::vector<AutomationPoint> points) = 0;

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
