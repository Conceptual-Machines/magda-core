#pragma once

#include <tracktion_engine/tracktion_engine.h>

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
class AutomationPlaybackEngine : public AutomationManagerListener {
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

  private:
    static constexpr double kBakeIntervalSeconds = 0.01;  // 10ms between baked points

    void bakeAllLanes();
    void clearAllLanes();

    void bakeLane(const AutomationLaneInfo& lane);
    void clearLane(const AutomationLaneInfo& lane);

    te::AutomatableParameter* resolveParameter(const AutomationTarget& target);

    AudioBridge& bridge_;
    te::Edit& edit_;
    bool wasPlaying_ = false;
    bool needsRebake_ = true;  // Start true so first play triggers initial bake
};

}  // namespace magda
