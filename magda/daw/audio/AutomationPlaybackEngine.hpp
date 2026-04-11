#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>
#include <vector>

#include "../core/AutomationInfo.hpp"
#include "../core/AutomationManager.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

class AudioBridge;

/**
 * @brief Bakes MAGDA automation curves into TE's native AutomatableParameter curves
 *
 * Instead of polling automation values on the message thread, this engine
 * flattens MAGDA's bezier/tension curves into dense TE AutomationCurve points
 * so that TE's audio thread evaluates them per-block at sample-accurate timing.
 *
 * Lifecycle:
 * - On transport start: bake all lanes into TE curves
 * - On automation data change during playback: rebake affected lanes
 * - On transport stop: clear TE curves so manual control works
 *
 * Owned by AudioBridge. Called from timerCallback() (message thread) to detect
 * transport transitions and rebake when automation data changes.
 */
class AutomationPlaybackEngine : public AutomationManagerListener,
                                 public te::AutomatableParameter::Listener {
  public:
    AutomationPlaybackEngine(AudioBridge& bridge, te::Edit& edit);
    ~AutomationPlaybackEngine() override;

    /**
     * @brief Check for transport transitions and rebake if needed
     *
     * Called from AudioBridge::timerCallback() at 30Hz on message thread.
     * Detects play/stop transitions and triggers bake/clear operations.
     */
    void process();

    // AutomationManagerListener — rebake on data changes during playback
    void automationLanesChanged() override;
    void automationPointsChanged(AutomationLaneId laneId) override;
    // Property changes include bypass, snap flags, arm, name, etc. Only
    // bypass affects what gets baked, but any property-change listener miss
    // means a bypass toggle has no audible effect until another event forces
    // a rebake — so mark dirty unconditionally here.
    void automationLanePropertyChanged(AutomationLaneId laneId) override;
    // Fluid preview while the user is actively dragging a point — republish
    // the value through AutomationManager::notifyValueChanged so UI listeners
    // (faders, knobs, custom UIs) can follow the drag in real time without a
    // round-trip through the TE parameter (which would fight the baked curve).
    void automationPointDragPreview(AutomationLaneId laneId, AutomationPointId pointId,
                                    double previewTime, double previewValue) override;

    // te::AutomatableParameter::Listener — TE's audio thread writes the baked
    // curve into the parameter on each block during playback, and
    // updateToFollowCurve() (called from bakeLane when stopped) does the same
    // while stopped. Both paths coalesce through this async callback, which
    // we translate back to MAGDA 0..1 and broadcast via notifyValueChanged so
    // UI controls track the curve without polling.
    void curveHasChanged(te::AutomatableParameter&) override {}
    void currentValueChanged(te::AutomatableParameter&) override;

  private:
    static constexpr double kBakeIntervalSeconds = 0.01;  // 10ms between baked points

    void bakeAllLanes();
    void clearAllLanes();

    void bakeLane(const AutomationLaneInfo& lane);
    void clearLane(const AutomationLaneInfo& lane);

    te::AutomatableParameter* resolveParameter(const AutomationTarget& target);

    // Maps a MAGDA 0..1 normalized value to the TE parameter's real range,
    // accounting for the non-linear dB scale on track volume and TE's native
    // fader position encoding. Shared by the bake loop and the drag-preview
    // fast path so both paths agree on the conversion.
    float convertToTEValue(const AutomationTarget& target, te::AutomatableParameter* param,
                           double magdaNormalized) const;

    // Inverse of convertToTEValue: TE parameter's real value → MAGDA 0..1.
    // Used by currentValueChanged to translate TE-driven parameter writes back
    // into the normalized form UI listeners expect.
    double convertFromTEValue(const AutomationTarget& target, te::AutomatableParameter* param,
                              float teValue) const;

    // Register this engine as a TE listener on every parameter we just baked,
    // and unregister from any parameter we used to bake but no longer do. This
    // keeps the listenedParams_ map in sync with bakedTargets_.
    void syncParameterListeners();

    AudioBridge& bridge_;
    te::Edit& edit_;
    bool wasPlaying_ = false;
    bool needsRebake_ = true;  // Start true so first play triggers initial bake

    // Targets we baked on the previous pass. On the next bake we clear the
    // curve for any target that is no longer present — otherwise a deleted
    // lane's baked values continue driving the parameter on the audio thread.
    std::vector<AutomationTarget> bakedTargets_;

    // Reverse lookup for TE parameter listener callbacks: when TE fires
    // currentValueChanged(param), we look up which lane owns it so we can
    // broadcast the normalized value. Rebuilt alongside bakedTargets_ in
    // syncParameterListeners().
    struct ListenedParamInfo {
        AutomationLaneId laneId;
        AutomationTarget target;
    };
    std::unordered_map<te::AutomatableParameter*, ListenedParamInfo> listenedParams_;
};

}  // namespace magda
