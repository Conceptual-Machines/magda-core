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

static const char* modeName(AutomationMode m) {
    switch (m) {
        case AutomationMode::Off:
            return "OFF";
        case AutomationMode::Write:
            return "WRITE";
        case AutomationMode::Touch:
            return "TOUCH";
        case AutomationMode::Latch:
            return "LATCH";
    }
    return "?";
}

void AutomationRecordingEngine::setMode(AutomationMode mode) {
    if (mode == mode_)
        return;
    DBG("[AutoRec] setMode: " << modeName(mode_) << " -> " << modeName(mode));
    mode_ = mode;
    // Drop per-gesture state from the previous mode so a Touch ↔ Latch
    // transition (or any switch out of Touch / Latch) doesn't resume from
    // stale latched entries or baselines on the next gesture.
    clearLatchState();
    if (mode_ != AutomationMode::Off) {
        // Capture track state now — before playback starts and before TE
        // feeds back automation echoes that would corrupt track->volume.
        // This snapshot is used as the initial anchor value for any lane
        // created during the subsequent recording session.
        seedBaselines();
    }
}

AutomationMode AutomationRecordingEngine::getMode() const {
    return mode_;
}

void AutomationRecordingEngine::setWriteEnabled(bool enabled) {
    setMode(enabled ? AutomationMode::Write : AutomationMode::Off);
}

bool AutomationRecordingEngine::isWriteEnabled() const {
    return mode_ != AutomationMode::Off;
}

void AutomationRecordingEngine::process() {
    bool playing = edit_.getTransport().isPlaying();

    if (!wasPlaying_ && playing && isWriteEnabled()) {
        DBG("[AutoRec] Transport started with write ON — begin recording");
        UndoManager::getInstance().beginCompoundOperation("Record Automation");
        isRecording_ = true;
        lastRecorded_.clear();
        seedBaselines();
    } else if (wasPlaying_ && !playing && isRecording_) {
        DBG("[AutoRec] Transport stopped — end recording");
        flushFinalPoints();
        UndoManager::getInstance().endCompoundOperation();
        isRecording_ = false;
        lastRecorded_.clear();
        laneRecordingStart_.clear();
        lanePreRecordingPoints_.clear();
        clearLatchState();
    } else if (playing && isWriteEnabled() && !isRecording_) {
        DBG("[AutoRec] Write toggled ON mid-playback — begin recording");
        UndoManager::getInstance().beginCompoundOperation("Record Automation");
        isRecording_ = true;
        lastRecorded_.clear();
        seedBaselines();
    } else if (isRecording_ && !isWriteEnabled()) {
        DBG("[AutoRec] Write toggled OFF — end recording");
        flushFinalPoints();
        UndoManager::getInstance().endCompoundOperation();
        isRecording_ = false;
        lastRecorded_.clear();
        laneRecordingStart_.clear();
        lanePreRecordingPoints_.clear();
        clearLatchState();
    }

    if (isRecording_ && (mode_ == AutomationMode::Touch || mode_ == AutomationMode::Latch)) {
        processReleaseTransitions();
        if (mode_ == AutomationMode::Latch)
            continueLatchedWrites();
    } else if (!latched_.empty() || !previouslyTouched_.empty() || !laneTouchBaseline_.empty()) {
        clearLatchState();
    }

    wasPlaying_ = playing;
}

void AutomationRecordingEngine::processReleaseTransitions() {
    auto& autoMgr = AutomationManager::getInstance();
    auto current = autoMgr.getUserTouchedTargets();

    for (const auto& prev : previouslyTouched_) {
        bool stillTouched = std::find(current.begin(), current.end(), prev) != current.end();
        if (stillTouched)
            continue;

        auto laneId = autoMgr.getLaneForTarget(prev);
        if (laneId == INVALID_AUTOMATION_LANE_ID)
            continue;

        if (mode_ == AutomationMode::Touch) {
            // Bounce back: write a point at the current beat using the
            // pre-touch baseline so the lane stops holding the last drag
            // value. Without this, a freshly-created lane would pin the
            // parameter at the released value forever.
            auto baselineIt = laneTouchBaseline_.find(laneId);
            if (baselineIt != laneTouchBaseline_.end()) {
                double beatTime = getCurrentBeatTime();
                double baseline = baselineIt->second;
                DBG("[AutoRec] Touch bounce-back lane=" << laneId << " baseline=" << baseline);
                recordPoint(laneId, beatTime, baseline);
                laneTouchBaseline_.erase(baselineIt);
            }
        } else {
            // Latch: capture the last recorded value so continueLatchedWrites
            // can keep re-recording it until the user re-touches or stops.
            auto it = lastRecorded_.find(laneId);
            if (it != lastRecorded_.end()) {
                latched_[laneId] = LatchEntry{prev, it->second.value};
                DBG("[AutoRec] Latch captured lane=" << laneId << " value=" << it->second.value);
            }
        }
    }

    // Re-touch: drop any latch entries the user has taken back over so the
    // continuous writes stop and recording proceeds normally during the new
    // gesture. (Touch doesn't accumulate state to drop here.)
    if (mode_ == AutomationMode::Latch) {
        for (const auto& cur : current) {
            auto laneId = autoMgr.getLaneForTarget(cur);
            if (laneId != INVALID_AUTOMATION_LANE_ID && latched_.erase(laneId) > 0) {
                DBG("[AutoRec] Latch released (user re-touched) lane=" << laneId);
            }
        }
    }

    previouslyTouched_ = std::move(current);
}

void AutomationRecordingEngine::continueLatchedWrites() {
    double beatTime = getCurrentBeatTime();
    for (const auto& [laneId, entry] : latched_) {
        if (!shouldThinPoint(laneId, beatTime, entry.value))
            recordPoint(laneId, beatTime, entry.value);
    }
}

void AutomationRecordingEngine::clearLatchState() {
    // Clears all per-gesture Touch / Latch state in one shot:
    // - latched_ (Latch held values)
    // - previouslyTouched_ (release-detection diff buffer)
    // - laneTouchBaseline_ (Touch bounce-back baselines)
    latched_.clear();
    previouslyTouched_.clear();
    laneTouchBaseline_.clear();
}

bool AutomationRecordingEngine::shouldRecord() const {
    return isRecording_ && edit_.getTransport().isPlaying();
}

bool AutomationRecordingEngine::requiresUserTouched() const {
    return mode_ == AutomationMode::Touch || mode_ == AutomationMode::Latch;
}

double AutomationRecordingEngine::getCurrentBeatTime() const {
    auto position = edit_.getTransport().getPosition();
    return edit_.tempoSequence.toBeats(position).inBeats();
}

double AutomationRecordingEngine::normalizeDeviceParam(const AutomationTarget& target,
                                                       float rawValue) {
    ParameterInfo paramInfo = target.getParameterInfo();
    const float teSpan = paramInfo.teMaxValue - paramInfo.teMinValue;
    const bool infoMatchesTeRange = std::abs(paramInfo.minValue - paramInfo.teMinValue) < 1e-6f &&
                                    std::abs(paramInfo.maxValue - paramInfo.teMaxValue) < 1e-6f;

    if (teSpan > 0.0f && !infoMatchesTeRange) {
        return juce::jlimit(0.0, 1.0,
                            static_cast<double>((rawValue - paramInfo.teMinValue) / teSpan));
    }

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
    // Sweep pre-existing automation points in the recorded time range so old
    // curve data doesn't interleave with the new recording. Only points that
    // existed before this recording session started are candidates — points
    // written by the current session are never deleted.
    auto& autoMgr = AutomationManager::getInstance();
    const auto* lane = autoMgr.getLane(laneId);
    if (lane && lane->isAbsolute()) {
        // On first recorded point for this lane this session, snapshot existing
        // point IDs and mark the sweep start beat.
        if (!laneRecordingStart_.count(laneId)) {
            laneRecordingStart_[laneId] = beatTime;
            auto& snapshot = lanePreRecordingPoints_[laneId];
            for (const auto& pt : lane->absolutePoints)
                snapshot.insert(pt.id);
        }

        double sweepFrom = laneRecordingStart_.at(laneId);
        const auto& snapshot = lanePreRecordingPoints_[laneId];

        // Delete only pre-recording points that fall within [sweepFrom, beatTime].
        std::vector<AutomationPointId> toDelete;
        for (const auto& pt : lane->absolutePoints) {
            if (pt.time >= sweepFrom && pt.time <= beatTime && snapshot.count(pt.id))
                toDelete.push_back(pt.id);
        }
        for (auto pid : toDelete) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<DeleteAutomationPointCommand>(laneId, INVALID_AUTOMATION_CLIP_ID,
                                                               pid));
        }
    }

    auto cmd = std::make_unique<AddAutomationPointCommand>(
        laneId, INVALID_AUTOMATION_CLIP_ID, beatTime, normalizedValue, AutomationCurveType::Linear);
    UndoManager::getInstance().executeCommand(std::move(cmd));

    lastRecorded_[laneId] = {beatTime, normalizedValue};
}

void AutomationRecordingEngine::seedBaselines() {
    lastTrackMixState_.clear();
    const auto& tracks = TrackManager::getInstance().getTracks();
    for (const auto& track : tracks) {
        auto& mix = lastTrackMixState_[static_cast<int>(track.id)];
        // Use the manual (user-set) values so that automation echoes baked into
        // track->volume during a prior playback session don't corrupt the anchor.
        mix.volume = track.manualVolume;
        mix.pan = track.manualPan;
        mix.sendLevels.clear();
        for (const auto& send : track.sends)
            mix.sendLevels[send.busIndex] = send.level;
        DBG("[AutoRec] seedBaselines track="
            << (int)track.id << " vol=" << track.volume << " (" << gainToDb(track.volume) << " dB)"
            << " manualVol=" << track.manualVolume << " (" << gainToDb(track.manualVolume) << " dB)"
            << " pan=" << track.pan << " manualPan=" << track.manualPan);
    }
}

void AutomationRecordingEngine::flushFinalPoints() {
    double stopBeat = getCurrentBeatTime();
    for (const auto& [laneId, last] : lastRecorded_) {
        // Skip if the transport has already rewound past the last recorded point
        // (e.g. TE returns to loop start on stop). Writing at the rewind position
        // would create a spurious anchor at the beginning of the timeline.
        if (stopBeat <= last.beatTime)
            continue;
        auto cmd = std::make_unique<AddAutomationPointCommand>(
            laneId, INVALID_AUTOMATION_CLIP_ID, stopBeat, last.value, AutomationCurveType::Linear);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    }
}

// ============================================================================
// Parameter Change Handlers
// ============================================================================

void AutomationRecordingEngine::onDeviceParameterChanged(DeviceId deviceId, int paramIndex,
                                                         float rawValue) {
    if (!shouldRecord())
        return;

    auto& trackMgr = TrackManager::getInstance();
    auto devicePath = trackMgr.findDevicePath(deviceId);
    if (!devicePath.isValid())
        return;

    // Build target for this device parameter
    AutomationTarget target;
    target.type = AutomationTargetType::DeviceParameter;
    target.trackId = devicePath.trackId;
    target.devicePath = devicePath;
    target.paramIndex = paramIndex;

    // Get param name for lane display
    auto resolved = trackMgr.resolvePath(devicePath);
    if (resolved.valid && resolved.device) {
        if (paramIndex >= 0 && paramIndex < (int)resolved.device->parameters.size())
            target.paramName =
                resolved.device->name + ": " + resolved.device->parameters[paramIndex].name;
    }

    auto& autoMgr = AutomationManager::getInstance();

    // Touch / Latch only record while the user is physically holding the
    // control. Filter before lane creation so a quick fly-by parameter change
    // doesn't even materialise a lane.
    if (requiresUserTouched() && !autoMgr.isTargetUserTouched(target))
        return;

    auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);

    // Touch bounce-back baseline. For device params there's no engine-side
    // pre-touch value (unlike volume/pan which use mix state), so we read
    // the slider-captured baseline that TextSlider deposited on mouseDown.
    if (mode_ == AutomationMode::Touch && !laneTouchBaseline_.count(laneId)) {
        if (auto baseline = autoMgr.getTouchBaseline(target))
            laneTouchBaseline_[laneId] = *baseline;
    }

    double beatTime = getCurrentBeatTime();
    double normalizedValue = normalizeDeviceParam(target, rawValue);

    DBG("[AutoRec] Device param hit: deviceId=" << deviceId << " param=" << paramIndex << " raw="
                                                << rawValue << " norm=" << normalizedValue
                                                << " beat=" << beatTime);

    if (shouldThinPoint(laneId, beatTime, normalizedValue))
        return;

    recordPoint(laneId, beatTime, normalizedValue);
}

void AutomationRecordingEngine::onTrackPropertyChanged(int trackId) {
    if (!shouldRecord())
        return;

    auto tid = static_cast<TrackId>(trackId);
    const auto* track = TrackManager::getInstance().getTrack(tid);
    if (!track)
        return;

    // Never record values being written by the playback engine — these are
    // automation echoes round-tripping through TrackManager, not user gestures.
    auto& autoMgr = AutomationManager::getInstance();
    if (autoMgr.isApplyingAutomationWrite()) {
        DBG("[AutoRec] trackPropertyChanged SKIPPED (isApplyingAutomationWrite) track="
            << trackId << " vol=" << track->volume << " (" << gainToDb(track->volume) << " dB)");
        return;
    }

    // trackPropertyChanged fires for mute, solo, arm, colour, etc. — not just
    // volume/pan. Only proceed if volume or pan actually changed since last call.
    // Baselines are seeded at record-start in seedBaselines() so the first
    // real callback already has accurate prior values to compare against.
    auto& mix = lastTrackMixState_[tid];
    bool volumeChanged = (track->volume != mix.volume);
    bool panChanged = (track->pan != mix.pan);

    // Detect aux-send level deltas. sendChanges holds (busIndex, newLevel,
    // preSeedLevel) so the per-send recording block below can correct the
    // anchor of a freshly-created lane the same way volume/pan do.
    struct SendDelta {
        int busIndex;
        float newLevel;
        float preSeedLevel;
    };
    std::vector<SendDelta> sendChanges;
    for (const auto& send : track->sends) {
        auto it = mix.sendLevels.find(send.busIndex);
        float preSeed = (it == mix.sendLevels.end()) ? send.level : it->second;
        if (it == mix.sendLevels.end() || it->second != send.level)
            sendChanges.push_back({send.busIndex, send.level, preSeed});
    }

    if (!volumeChanged && !panChanged && sendChanges.empty())
        return;

    // During playback, ignore property changes that weren't initiated by a
    // user gesture — otherwise AutomationPlaybackEngine's writes to the TE
    // parameter round-trip back through trackPropertyChanged and we'd re-record
    // the baked curve on every block. Touch / Latch require this gate
    // unconditionally (even when no automation is being baked back), since
    // their semantics demand a physical hold to record.
    if (autoMgr.isPlaybackActive() || requiresUserTouched()) {
        AutomationTarget volTarget;
        volTarget.type = AutomationTargetType::TrackVolume;
        volTarget.trackId = tid;
        AutomationTarget panTarget;
        panTarget.type = AutomationTargetType::TrackPan;
        panTarget.trackId = tid;

        if (volumeChanged && !autoMgr.isTargetUserTouched(volTarget))
            volumeChanged = false;
        if (panChanged && !autoMgr.isTargetUserTouched(panTarget))
            panChanged = false;

        // Filter send changes that came from automation playback, not the user.
        sendChanges.erase(std::remove_if(sendChanges.begin(), sendChanges.end(),
                                         [&](const SendDelta& s) {
                                             AutomationTarget sendTarget;
                                             sendTarget.type = AutomationTargetType::SendLevel;
                                             sendTarget.trackId = tid;
                                             sendTarget.sendBusIndex = s.busIndex;
                                             return !autoMgr.isTargetUserTouched(sendTarget);
                                         }),
                          sendChanges.end());

        if (!volumeChanged && !panChanged && sendChanges.empty())
            return;
    }

    // Snapshot the seed values BEFORE overwriting mix state. The anchor
    // correction below needs the pre-gesture baseline (set by seedBaselines()),
    // not the automation-driven value we're about to store.
    float preSeedVolume = mix.volume;
    float preSeedPan = mix.pan;

    // Update the tracked mix state only for real events we will actually record.
    // Updating early (before the user-touch gate) would corrupt the seed value
    // captured in seedBaselines() — automation-driven callbacks that bypass the
    // isApplyingAutomationWrite guard (e.g. fired by non-volume property changes)
    // would silently overwrite mix.volume with the automation-driven track->volume.
    mix.volume = track->volume;
    mix.pan = track->pan;
    for (const auto& s : sendChanges)
        mix.sendLevels[s.busIndex] = s.newLevel;

    double beatTime = getCurrentBeatTime();

    if (volumeChanged) {
        AutomationTarget target;
        target.type = AutomationTargetType::TrackVolume;
        target.trackId = tid;

        bool isNewLane = (autoMgr.getLaneForTarget(target) == INVALID_AUTOMATION_LANE_ID);
        auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);

        // Capture pre-touch baseline once per gesture for Touch's bounce-back
        // on release. preSeedVolume is the value before this callback fired,
        // which on the first event of a gesture is the genuine pre-touch value.
        if (mode_ == AutomationMode::Touch && !laneTouchBaseline_.count(laneId)) {
            ParameterInfo paramInfo = target.getParameterInfo();
            float seedDb = gainToDb(preSeedVolume);
            laneTouchBaseline_[laneId] =
                static_cast<double>(ParameterUtils::realToNormalized(seedDb, paramInfo));
        }

        // createLane reads track->volume for the anchor, but by the time the
        // first user gesture fires, the playback engine may have already written
        // a different value. Correct the anchor using the value seeded at
        // recording start, which is captured before TE's async callbacks run.
        if (isNewLane) {
            ParameterInfo paramInfo = target.getParameterInfo();
            float seedDb = gainToDb(preSeedVolume);
            double seedNorm =
                static_cast<double>(ParameterUtils::realToNormalized(seedDb, paramInfo));
            const auto* lane = autoMgr.getLane(laneId);
            if (lane && !lane->absolutePoints.empty()) {
                DBG("[AutoRec] New vol lane — anchor was "
                    << gainToDb(track->volume) << " dB, correcting to seed=" << seedDb << " dB"
                    << " (track->vol=" << track->volume << " preSeed=" << preSeedVolume << ")");
                UndoManager::getInstance().executeCommand(
                    std::make_unique<MoveAutomationPointCommand>(laneId, INVALID_AUTOMATION_CLIP_ID,
                                                                 lane->absolutePoints[0].id, 0.0,
                                                                 seedNorm));
            }
        }

        ParameterInfo paramInfo = target.getParameterInfo();
        float db = gainToDb(track->volume);
        double normalizedValue =
            static_cast<double>(ParameterUtils::realToNormalized(db, paramInfo));

        DBG("[AutoRec] Volume hit: track=" << (int)tid << " vol=" << track->volume << " (" << db
                                           << " dB)"
                                           << " norm=" << normalizedValue << " beat=" << beatTime);
        if (!shouldThinPoint(laneId, beatTime, normalizedValue))
            recordPoint(laneId, beatTime, normalizedValue);
    }

    if (panChanged) {
        AutomationTarget target;
        target.type = AutomationTargetType::TrackPan;
        target.trackId = tid;

        bool isNewLane = (autoMgr.getLaneForTarget(target) == INVALID_AUTOMATION_LANE_ID);
        auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);

        if (mode_ == AutomationMode::Touch && !laneTouchBaseline_.count(laneId)) {
            ParameterInfo paramInfo = target.getParameterInfo();
            laneTouchBaseline_[laneId] =
                static_cast<double>(ParameterUtils::realToNormalized(preSeedPan, paramInfo));
        }

        if (isNewLane) {
            ParameterInfo paramInfo = target.getParameterInfo();
            double seedNorm =
                static_cast<double>(ParameterUtils::realToNormalized(preSeedPan, paramInfo));
            const auto* lane = autoMgr.getLane(laneId);
            if (lane && !lane->absolutePoints.empty()) {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<MoveAutomationPointCommand>(laneId, INVALID_AUTOMATION_CLIP_ID,
                                                                 lane->absolutePoints[0].id, 0.0,
                                                                 seedNorm));
            }
        }

        ParameterInfo paramInfo = target.getParameterInfo();
        double normalizedValue =
            static_cast<double>(ParameterUtils::realToNormalized(track->pan, paramInfo));

        DBG("[AutoRec] Pan hit: track=" << (int)tid << " pan=" << track->pan
                                        << " norm=" << normalizedValue << " beat=" << beatTime);
        if (!shouldThinPoint(laneId, beatTime, normalizedValue))
            recordPoint(laneId, beatTime, normalizedValue);
    }

    for (const auto& s : sendChanges) {
        AutomationTarget target;
        target.type = AutomationTargetType::SendLevel;
        target.trackId = tid;
        target.sendBusIndex = s.busIndex;

        bool isNewLane = (autoMgr.getLaneForTarget(target) == INVALID_AUTOMATION_LANE_ID);
        auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);

        if (isNewLane) {
            ParameterInfo paramInfo = target.getParameterInfo();
            float seedDb = gainToDb(s.preSeedLevel);
            double seedNorm =
                static_cast<double>(ParameterUtils::realToNormalized(seedDb, paramInfo));
            const auto* lane = autoMgr.getLane(laneId);
            if (lane && !lane->absolutePoints.empty()) {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<MoveAutomationPointCommand>(laneId, INVALID_AUTOMATION_CLIP_ID,
                                                                 lane->absolutePoints[0].id, 0.0,
                                                                 seedNorm));
            }
        }

        ParameterInfo paramInfo = target.getParameterInfo();
        float db = gainToDb(s.newLevel);
        double normalizedValue =
            static_cast<double>(ParameterUtils::realToNormalized(db, paramInfo));

        if (!shouldThinPoint(laneId, beatTime, normalizedValue))
            recordPoint(laneId, beatTime, normalizedValue);
    }
}

void AutomationRecordingEngine::onModParameterValueChanged(TrackId trackId,
                                                           const ChainNodePath& devicePath,
                                                           ModId modId, int paramIndex,
                                                           float value) {
    if (!shouldRecord())
        return;

    AutomationTarget target;
    target.type = AutomationTargetType::ModParameter;
    target.trackId = trackId;
    target.devicePath = devicePath;
    target.modId = modId;
    target.modParamIndex = paramIndex;

    auto& autoMgr = AutomationManager::getInstance();

    // Touch / Latch: only record while the user is physically holding the
    // mod-rate slider. Same gate as device params and macros.
    if (requiresUserTouched() && !autoMgr.isTargetUserTouched(target))
        return;

    auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);

    // Touch baseline for bounce-back on release. The slider deposits the
    // pre-drag value into AutomationManager on mouseDown.
    if (mode_ == AutomationMode::Touch && !laneTouchBaseline_.count(laneId)) {
        if (auto baseline = autoMgr.getTouchBaseline(target))
            laneTouchBaseline_[laneId] = *baseline;
    }

    // Convert raw rate (Hz, etc.) to normalized 0..1 using the lane's stored
    // ParameterInfo so curve points are stored in the same space as draws.
    ParameterInfo info = target.getParameterInfo();
    double normalizedValue = static_cast<double>(ParameterUtils::realToNormalized(value, info));

    double beatTime = getCurrentBeatTime();
    if (shouldThinPoint(laneId, beatTime, normalizedValue))
        return;

    recordPoint(laneId, beatTime, normalizedValue);
}

void AutomationRecordingEngine::onMacroValueChanged(TrackId trackId, bool isRack, int id,
                                                    int macroIndex, float value) {
    if (!shouldRecord())
        return;

    // Build target: for rack macros the path points to the rack,
    // for device macros it points to the device.
    AutomationTarget target;
    target.type = AutomationTargetType::Macro;
    target.trackId = trackId;
    target.macroIndex = macroIndex;

    if (isRack) {
        target.devicePath = ChainNodePath::rack(trackId, static_cast<RackId>(id));
    } else {
        target.devicePath = TrackManager::getInstance().findDevicePath(static_cast<DeviceId>(id));
        if (!target.devicePath.isValid())
            return;
    }

    auto& autoMgr = AutomationManager::getInstance();

    if (requiresUserTouched() && !autoMgr.isTargetUserTouched(target))
        return;

    auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);

    if (mode_ == AutomationMode::Touch && !laneTouchBaseline_.count(laneId)) {
        if (auto baseline = autoMgr.getTouchBaseline(target))
            laneTouchBaseline_[laneId] = *baseline;
    }

    double beatTime = getCurrentBeatTime();
    double normalizedValue = static_cast<double>(value);  // Macros are already 0-1

    if (shouldThinPoint(laneId, beatTime, normalizedValue))
        return;

    recordPoint(laneId, beatTime, normalizedValue);
}

}  // namespace magda
