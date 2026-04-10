#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/AutomationInfo.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

class AudioBridge;

/**
 * @brief Samples automation curves during playback and applies values to parameters
 *
 * Owned by AudioBridge. Called from the 30Hz timer callback (message thread).
 * For each active automation lane, reads the interpolated value at the current
 * playhead position and routes it to the appropriate parameter via AudioBridge.
 */
class AutomationPlaybackEngine {
  public:
    AutomationPlaybackEngine(AudioBridge& bridge, te::Edit& edit);

    /**
     * @brief Sample all automation lanes and apply values
     *
     * Called every timer tick (~30Hz) from AudioBridge::timerCallback().
     * Only applies values when transport is playing.
     */
    void process();

  private:
    void applyLane(const AutomationLaneInfo& lane, double timeInSeconds);
    void applyTrackVolume(TrackId trackId, double normalizedValue);
    void applyTrackPan(TrackId trackId, double normalizedValue);
    void applyDeviceParameter(const AutomationTarget& target, double normalizedValue);
    void applyMacro(const AutomationTarget& target, double normalizedValue);

    AudioBridge& bridge_;
    te::Edit& edit_;
    bool wasPlaying_ = false;
};

}  // namespace magda
