#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>
#include <unordered_set>

#include "../core/AutomationInfo.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Records parameter changes into armed automation lanes during playback
 *
 * When automation write mode is enabled and the transport is playing, parameter
 * changes from user interaction (mouse-driven sliders, faders, knobs) are captured
 * and written as points on armed automation lanes.
 *
 * Owned by AudioBridge. Parameter change callbacks are forwarded from AudioBridge's
 * TrackManagerListener implementation (not registered directly to avoid double-listening).
 *
 * Points are thinned to avoid flooding the curve with redundant data. All points
 * from a single recording pass are grouped into one compound undo operation.
 */
class AutomationRecordingEngine {
  public:
    explicit AutomationRecordingEngine(te::Edit& edit);

    void setWriteEnabled(bool enabled);
    bool isWriteEnabled() const;

    /**
     * @brief Detect transport transitions and manage recording lifecycle
     *
     * Called from AudioBridge::timerCallback() at 30Hz on message thread.
     * Detects play/stop transitions to start/stop compound undo operations.
     */
    void process();

    // Forwarded from AudioBridge's TrackManagerListener callbacks
    void onDeviceParameterChanged(DeviceId deviceId, int paramIndex, float rawValue);
    void onTrackPropertyChanged(int trackId);
    void onMacroValueChanged(TrackId trackId, bool isRack, int id, int macroIndex, float value);

  private:
    bool shouldRecord() const;
    double getCurrentBeatTime() const;
    double normalizeDeviceParam(const AutomationTarget& target, float rawValue);
    bool shouldThinPoint(AutomationLaneId laneId, double beatTime, double value);
    void recordPoint(AutomationLaneId laneId, double beatTime, double normalizedValue);
    void flushFinalPoints();
    // Populate lastTrackMixState_ from current TrackManager state so the first
    // trackPropertyChanged callback during recording has an accurate baseline.
    void seedBaselines();

    te::Edit& edit_;
    bool writeEnabled_ = false;
    bool wasPlaying_ = false;
    bool isRecording_ = false;

    // Thinning: last recorded time+value per lane
    struct LastRecorded {
        double beatTime = -1.0;
        double value = -1.0;
    };
    std::unordered_map<int, LastRecorded> lastRecorded_;

    // Track last known volume/pan per track to detect actual changes
    // (trackPropertyChanged fires for mute/solo/arm/etc. too)
    struct TrackMixState {
        float volume = -1.0f;
        float pan = -2.0f;  // Sentinel: impossible value means "not yet captured"
    };
    std::unordered_map<int, TrackMixState> lastTrackMixState_;

    // Per-lane beat time at which recording first wrote a point this session.
    std::unordered_map<int, double> laneRecordingStart_;

    // Snapshot of point IDs that existed before recording started for each lane.
    // The sweep only deletes from this set so newly recorded points are never erased.
    std::unordered_map<int, std::unordered_set<AutomationPointId>> lanePreRecordingPoints_;

    static constexpr double kMinTimeDeltaSeconds = 0.05;  // 50ms thinning threshold
    static constexpr double kMinValueDelta = 0.005;       // 0.5% normalized range
};

}  // namespace magda
