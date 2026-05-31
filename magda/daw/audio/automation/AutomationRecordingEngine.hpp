#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>
#include <unordered_set>

#include "../../core/AutomationInfo.hpp"
#include "../../core/AutomationManager.hpp"
#include "../../core/ChainNode.hpp"
#include "../../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Automation recording mode.
 *
 * Off    — not armed; nothing is recorded.
 * Write  — record any user-driven parameter change while transport rolls
 *          (current default behavior).
 * Touch  — record only while the user is physically holding/dragging the
 *          parameter; on release, automation playback resumes.
 * Latch  — like Touch, but on release the last held value continues to be
 *          written into the lane until the transport stops.
 */
enum class AutomationMode {
    Off,
    Write,
    Touch,
    Latch,
};

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

    void setMode(AutomationMode mode);
    AutomationMode getMode() const;

    // Backwards-compat shims — Off↔Write only. Touch/Latch require setMode().
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
    void onDeviceParameterChanged(const ChainNodePath& devicePath, int paramIndex, float rawValue);
    void onTrackPropertyChanged(int trackId);
    void onMacroValueChanged(TrackId trackId, ChainScope scope, int ownerId, int macroIndex,
                             float value);
    void onModParameterValueChanged(TrackId trackId, const ChainNodePath& devicePath, ModId modId,
                                    int paramIndex, float value);

#ifdef MAGDA_ENABLE_TEST_HOOKS
    static bool magdaTestShouldIgnoreAutomationWriteback() {
        return AutomationManager::getInstance().isApplyingAutomationWrite();
    }
#endif

  private:
    bool shouldRecord() const;
    static bool shouldIgnoreAutomationWriteback();
    // True when the active mode requires the user to be physically holding the
    // target (Touch / Latch). Write mode records on any change; Off doesn't get
    // here because shouldRecord() already returns false.
    bool requiresUserTouched() const;
    double getCurrentBeatTime() const;
    double normalizeDeviceParam(const AutomationTarget& target, float rawValue);
    bool shouldThinPoint(AutomationLaneId laneId, double beatTime, double value);
    void recordPoint(AutomationLaneId laneId, double beatTime, double normalizedValue);
    void flushFinalPoints();
    // Populate lastTrackMixState_ from current TrackManager state so the first
    // trackPropertyChanged callback during recording has an accurate baseline.
    void seedBaselines();

    te::Edit& edit_;
    AutomationMode mode_ = AutomationMode::Off;
    bool wasPlaying_ = false;
    bool isRecording_ = false;

    // Thinning: last recorded time+value per lane
    struct LastRecorded {
        double beatTime = -1.0;
        double value = -1.0;
    };
    std::unordered_map<int, LastRecorded> lastRecorded_;

    // Track last known volume/pan/send-levels per track to detect actual
    // changes (trackPropertyChanged fires for mute/solo/arm/etc. too)
    struct TrackMixState {
        float volume = -1.0f;
        float pan = -2.0f;  // Sentinel: impossible value means "not yet captured"
        std::unordered_map<int, float> sendLevels;  // busIndex → level
    };
    std::unordered_map<int, TrackMixState> lastTrackMixState_;

    // Per-lane beat time at which recording first wrote a point this session.
    std::unordered_map<int, double> laneRecordingStart_;

    // Per-lane normalized value the parameter held just before the user's
    // first edit of this gesture. Used by Touch mode to write a "bounce-back"
    // point at release time so the lane returns to where it was before the
    // touch (instead of holding the last drag value forever). Only populated
    // for target types that expose a pre-gesture value (TrackVolume/TrackPan
    // via the existing seed-baseline mechanism).
    std::unordered_map<int, double> laneTouchBaseline_;

    // Latch state: targets the user touched and released without re-touching.
    // While the transport rolls in Latch mode, each entry's value is re-written
    // to its lane on every process() tick so the lane stays flat at the held
    // value until the user re-engages or transport stops.
    struct LatchEntry {
        AutomationTarget target;
        double value = 0.0;
    };
    std::unordered_map<int, LatchEntry> latched_;
    // Targets that were under user touch at the previous process() tick. The
    // diff against the current tick's set is how we detect release for Latch.
    std::vector<AutomationTarget> previouslyTouched_;

    // Detect release transitions (targets in previouslyTouched_ but not in
    // the current touched set) and emit per-mode actions: Touch writes a
    // bounce-back point; Latch captures the last value into latched_.
    void processReleaseTransitions();
    // Latch only: re-record the held value at the current beat for every
    // entry in latched_. Called after processReleaseTransitions() each tick.
    void continueLatchedWrites();
    void clearLatchState();

    // Snapshot of point IDs that existed before recording started for each lane.
    // The sweep only deletes from this set so newly recorded points are never erased.
    std::unordered_map<int, std::unordered_set<AutomationPointId>> lanePreRecordingPoints_;

    static constexpr double kMinTimeDeltaSeconds = 0.05;  // 50ms thinning threshold
    static constexpr double kMinValueDelta = 0.005;       // 0.5% normalized range
};

}  // namespace magda
