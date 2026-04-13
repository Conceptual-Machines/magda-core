#include "AutomationRecordingEngine.hpp"

#include <cmath>

#include "../core/AutomationCommands.hpp"
#include "../core/AutomationManager.hpp"
#include "../core/ParameterInfo.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/TrackManager.hpp"
#include "../core/UndoManager.hpp"

namespace magda {

// Convert linear gain to dB (same formula as AutomationManager.cpp)
static float gainToDb(float gain) {
    constexpr float MIN_DB = -60.0f;
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

AutomationRecordingEngine::AutomationRecordingEngine(te::Edit& edit) : edit_(edit) {}

void AutomationRecordingEngine::setWriteEnabled(bool enabled) {
    DBG("[AutoRec] setWriteEnabled: " << (enabled ? "ON" : "OFF"));
    writeEnabled_ = enabled;
}

bool AutomationRecordingEngine::isWriteEnabled() const {
    return writeEnabled_;
}

void AutomationRecordingEngine::process() {
    bool playing = edit_.getTransport().isPlaying();

    if (!wasPlaying_ && playing && writeEnabled_) {
        DBG("[AutoRec] Transport started with write ON — begin recording");
        UndoManager::getInstance().beginCompoundOperation("Record Automation");
        isRecording_ = true;
        lastRecorded_.clear();
        lastTrackMixState_.clear();
    } else if (wasPlaying_ && !playing && isRecording_) {
        DBG("[AutoRec] Transport stopped — end recording");
        flushFinalPoints();
        UndoManager::getInstance().endCompoundOperation();
        isRecording_ = false;
        lastRecorded_.clear();
    } else if (playing && writeEnabled_ && !isRecording_) {
        DBG("[AutoRec] Write toggled ON mid-playback — begin recording");
        UndoManager::getInstance().beginCompoundOperation("Record Automation");
        isRecording_ = true;
        lastRecorded_.clear();
    } else if (isRecording_ && !writeEnabled_) {
        DBG("[AutoRec] Write toggled OFF — end recording");
        flushFinalPoints();
        UndoManager::getInstance().endCompoundOperation();
        isRecording_ = false;
        lastRecorded_.clear();
    }

    wasPlaying_ = playing;
}

bool AutomationRecordingEngine::shouldRecord() const {
    return isRecording_ && edit_.getTransport().isPlaying();
}

double AutomationRecordingEngine::getCurrentBeatTime() const {
    auto position = edit_.getTransport().getPosition();
    return edit_.tempoSequence.toBeats(position).inBeats();
}

double AutomationRecordingEngine::normalizeDeviceParam(const AutomationTarget& target,
                                                       float rawValue) {
    ParameterInfo paramInfo = target.getParameterInfo();
    return static_cast<double>(ParameterUtils::realToNormalized(rawValue, paramInfo));
}

bool AutomationRecordingEngine::shouldThinPoint(AutomationLaneId laneId, double beatTime,
                                                double value) {
    auto it = lastRecorded_.find(laneId);
    if (it == lastRecorded_.end())
        return false;  // First point for this lane — always record

    const auto& last = it->second;

    // Always record if value change is significant
    double valueDelta = std::abs(value - last.value);
    if (valueDelta >= kMinValueDelta)
        return false;  // Don't thin — significant value change

    // Thin if time delta is too small AND value didn't change much
    double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
    double timeDeltaSeconds = (beatTime - last.beatTime) * 60.0 / bpm;
    if (timeDeltaSeconds < kMinTimeDeltaSeconds)
        return true;  // Thin — too close in time with negligible value change

    return false;  // Enough time has passed
}

void AutomationRecordingEngine::recordPoint(AutomationLaneId laneId, double beatTime,
                                            double normalizedValue) {
    auto cmd = std::make_unique<AddAutomationPointCommand>(
        laneId, INVALID_AUTOMATION_CLIP_ID, beatTime, normalizedValue, AutomationCurveType::Linear);
    UndoManager::getInstance().executeCommand(std::move(cmd));

    lastRecorded_[laneId] = {beatTime, normalizedValue};
}

void AutomationRecordingEngine::flushFinalPoints() {
    double beatTime = getCurrentBeatTime();
    for (const auto& [laneId, last] : lastRecorded_) {
        // Write a final point at the current position with the last known value
        auto cmd = std::make_unique<AddAutomationPointCommand>(
            laneId, INVALID_AUTOMATION_CLIP_ID, beatTime, last.value, AutomationCurveType::Linear);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    }
}

// ============================================================================
// Parameter Change Handlers
// ============================================================================

void AutomationRecordingEngine::onDeviceParameterChanged(DeviceId deviceId, int paramIndex,
                                                         float rawValue) {
    if (!shouldRecord()) {
        return;
    }

    auto& autoMgr = AutomationManager::getInstance();

    for (const auto& lane : autoMgr.getLanes()) {
        if (!lane.armed)
            continue;
        if (lane.target.type != AutomationTargetType::DeviceParameter)
            continue;
        if (lane.target.devicePath.getDeviceId() != deviceId)
            continue;
        if (lane.target.paramIndex != paramIndex)
            continue;

        double beatTime = getCurrentBeatTime();
        double normalizedValue = normalizeDeviceParam(lane.target, rawValue);

        DBG("[AutoRec] Device param hit: deviceId=" << deviceId << " param=" << paramIndex
                                                    << " raw=" << rawValue << " norm="
                                                    << normalizedValue << " beat=" << beatTime);

        if (shouldThinPoint(lane.id, beatTime, normalizedValue))
            return;

        recordPoint(lane.id, beatTime, normalizedValue);
        return;  // One lane per device+param
    }
}

void AutomationRecordingEngine::onTrackPropertyChanged(int trackId) {
    if (!shouldRecord())
        return;

    // Note: we do NOT check AutomationManager::isPlaybackActive() here.
    // That flag is true for the entire playback session (set by bakeAllLanes),
    // which would block all recording. Instead, we rely on the volume/pan
    // change detection below to filter out non-user-initiated changes.
    // Automation playback drives parameters through TE's native curve system,
    // which does NOT fire trackPropertyChanged, so user fader moves are the
    // only source of this callback during playback.

    auto& autoMgr = AutomationManager::getInstance();
    auto tid = static_cast<TrackId>(trackId);
    const auto* track = TrackManager::getInstance().getTrack(tid);
    if (!track)
        return;

    // trackPropertyChanged fires for mute, solo, arm, colour, etc. — not just
    // volume/pan. Only proceed if volume or pan actually changed since last call.
    auto& mix = lastTrackMixState_[tid];
    bool volumeChanged = (track->volume != mix.volume);
    bool panChanged = (track->pan != mix.pan);
    mix.volume = track->volume;
    mix.pan = track->pan;

    if (!volumeChanged && !panChanged)
        return;

    double beatTime = getCurrentBeatTime();

    for (const auto& lane : autoMgr.getLanes()) {
        if (!lane.armed)
            continue;
        if (lane.target.trackId != tid)
            continue;

        if (lane.target.type == AutomationTargetType::TrackVolume && volumeChanged) {
            ParameterInfo paramInfo = lane.target.getParameterInfo();
            float db = gainToDb(track->volume);
            double normalizedValue =
                static_cast<double>(ParameterUtils::realToNormalized(db, paramInfo));

            DBG("[AutoRec] Volume hit: track=" << (int)tid << " vol=" << track->volume << " norm="
                                               << normalizedValue << " beat=" << beatTime);
            if (!shouldThinPoint(lane.id, beatTime, normalizedValue))
                recordPoint(lane.id, beatTime, normalizedValue);
        } else if (lane.target.type == AutomationTargetType::TrackPan && panChanged) {
            ParameterInfo paramInfo = lane.target.getParameterInfo();
            double normalizedValue =
                static_cast<double>(ParameterUtils::realToNormalized(track->pan, paramInfo));

            DBG("[AutoRec] Pan hit: track=" << (int)tid << " pan=" << track->pan
                                            << " norm=" << normalizedValue << " beat=" << beatTime);
            if (!shouldThinPoint(lane.id, beatTime, normalizedValue))
                recordPoint(lane.id, beatTime, normalizedValue);
        }
    }
}

bool AutomationRecordingEngine::macroScopeMatches(const AutomationTarget& target, bool isRack,
                                                  int id) {
    // The lane's devicePath encodes the rack/device that owns the macro.
    // For rack macros (isRack=true, id=RackId): the path's first rack step must match.
    // For device macros (isRack=false, id=DeviceId): the path's device must match.
    if (isRack) {
        return target.devicePath.getRackId() == id;
    }
    return target.devicePath.getDeviceId() == id;
}

void AutomationRecordingEngine::onMacroValueChanged(TrackId trackId, bool isRack, int id,
                                                    int macroIndex, float value) {
    if (!shouldRecord())
        return;

    auto& autoMgr = AutomationManager::getInstance();
    double beatTime = getCurrentBeatTime();

    for (const auto& lane : autoMgr.getLanes()) {
        if (!lane.armed)
            continue;
        if (lane.target.type != AutomationTargetType::Macro)
            continue;
        if (lane.target.trackId != trackId)
            continue;
        if (lane.target.macroIndex != macroIndex)
            continue;
        if (!macroScopeMatches(lane.target, isRack, id))
            continue;

        // Macro values are already 0-1 normalized
        double normalizedValue = static_cast<double>(value);

        if (shouldThinPoint(lane.id, beatTime, normalizedValue))
            return;

        recordPoint(lane.id, beatTime, normalizedValue);
        return;  // One lane per macro scope+index
    }
}

}  // namespace magda
