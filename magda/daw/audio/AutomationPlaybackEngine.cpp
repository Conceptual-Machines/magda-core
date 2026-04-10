#include "AutomationPlaybackEngine.hpp"

#include "../core/AutomationManager.hpp"
#include "../core/ParameterInfo.hpp"
#include "../core/ParameterUtils.hpp"
#include "AudioBridge.hpp"

namespace magda {

AutomationPlaybackEngine::AutomationPlaybackEngine(AudioBridge& bridge, te::Edit& edit)
    : bridge_(bridge), edit_(edit) {
    AutomationManager::getInstance().addListener(this);
}

AutomationPlaybackEngine::~AutomationPlaybackEngine() {
    AutomationManager::getInstance().removeListener(this);
}

void AutomationPlaybackEngine::process() {
    bool playing = edit_.getTransport().isPlaying();

    if (!wasPlaying_ && playing) {
        // Transport just started — bake all lanes into TE curves
        bakeAllLanes();
    } else if (wasPlaying_ && !playing) {
        // Transport just stopped — clear TE curves so manual control works
        clearAllLanes();
    } else if (playing && needsRebake_) {
        // Automation data changed during playback — rebake
        bakeAllLanes();
    }

    wasPlaying_ = playing;
    needsRebake_ = false;
}

// ============================================================================
// AutomationManagerListener
// ============================================================================

void AutomationPlaybackEngine::automationLanesChanged() {
    if (wasPlaying_)
        needsRebake_ = true;
}

void AutomationPlaybackEngine::automationPointsChanged(AutomationLaneId /*laneId*/) {
    if (wasPlaying_)
        needsRebake_ = true;
}

// ============================================================================
// Bake / Clear
// ============================================================================

void AutomationPlaybackEngine::bakeAllLanes() {
    auto& autoMgr = AutomationManager::getInstance();

    // Set feedback guard to prevent trackPropertyChanged from corrupting curves
    // when TE reads baked values during playback
    autoMgr.setPlaybackActive(true);

    for (const auto& lane : autoMgr.getLanes()) {
        if (lane.hasData())
            bakeLane(lane);
    }
}

void AutomationPlaybackEngine::clearAllLanes() {
    auto& autoMgr = AutomationManager::getInstance();

    for (const auto& lane : autoMgr.getLanes()) {
        clearLane(lane);
    }

    autoMgr.setPlaybackActive(false);
}

void AutomationPlaybackEngine::bakeLane(const AutomationLaneInfo& lane) {
    auto* param = resolveParameter(lane.target);
    if (!param)
        return;

    auto& autoMgr = AutomationManager::getInstance();
    auto& curve = param->getCurve();

    // Clear existing TE automation points
    curve.clear(nullptr);

    // Determine the time range from the automation data
    double startTime = 0.0;
    double endTime = 0.0;

    if (lane.isAbsolute() && !lane.absolutePoints.empty()) {
        startTime = lane.absolutePoints.front().time;
        endTime = lane.absolutePoints.back().time;
    } else if (lane.isClipBased()) {
        // Find the overall range from all clips
        bool first = true;
        for (auto clipId : lane.clipIds) {
            const auto* clip = autoMgr.getClip(clipId);
            if (!clip)
                continue;
            if (first || clip->startTime < startTime)
                startTime = clip->startTime;
            if (first || clip->getEndTime() > endTime)
                endTime = clip->getEndTime();
            first = false;
        }
    }

    if (endTime <= startTime)
        return;

    // Value conversion lambda: maps MAGDA's 0-1 normalized to TE's parameter range.
    // MAGDA and TE use different fader curves, so we must convert through dB for volume.
    auto convertValue = [&](double magdaNormalized) -> float {
        switch (lane.target.type) {
            case AutomationTargetType::TrackVolume: {
                // MAGDA 0-1 (FaderDB scale) → dB → TE fader position
                auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
                float dB = ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized),
                                                            paramInfo);
                return te::decibelsToVolumeFaderPosition(dB);
            }
            case AutomationTargetType::TrackPan: {
                // MAGDA 0-1 → linear -1..+1 (same as TE's pan range)
                auto paramInfo = ParameterPresets::pan(-1, "Pan");
                return ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized),
                                                        paramInfo);
            }
            default:
                // Device parameters: both MAGDA and TE use 0-1 normalized
                return static_cast<float>(magdaNormalized);
        }
    };

    // Bake: sample our curve at regular intervals and write dense linear points to TE
    for (double t = startTime; t <= endTime; t += kBakeIntervalSeconds) {
        double normalizedValue = autoMgr.getValueAtTime(lane.id, t);
        float teValue = convertValue(normalizedValue);
        curve.addPoint(te::TimePosition::fromSeconds(t), teValue, 0.0f, nullptr);
    }

    // Ensure the final point is exact
    double finalValue = autoMgr.getValueAtTime(lane.id, endTime);
    float teFinalValue = convertValue(finalValue);
    curve.addPoint(te::TimePosition::fromSeconds(endTime), teFinalValue, 0.0f, nullptr);
}

void AutomationPlaybackEngine::clearLane(const AutomationLaneInfo& lane) {
    auto* param = resolveParameter(lane.target);
    if (!param)
        return;

    param->getCurve().clear(nullptr);
}

// ============================================================================
// Parameter Resolution
// ============================================================================

te::AutomatableParameter* AutomationPlaybackEngine::resolveParameter(
    const AutomationTarget& target) {
    switch (target.type) {
        case AutomationTargetType::TrackVolume: {
            auto* track = bridge_.getAudioTrack(target.trackId);
            if (!track)
                return nullptr;
            if (auto* vp = track->getVolumePlugin()) {
                return vp->volParam.get();
            }
            return nullptr;
        }

        case AutomationTargetType::TrackPan: {
            auto* track = bridge_.getAudioTrack(target.trackId);
            if (!track)
                return nullptr;
            if (auto* vp = track->getVolumePlugin()) {
                return vp->panParam.get();
            }
            return nullptr;
        }

        case AutomationTargetType::DeviceParameter: {
            DeviceId deviceId = target.devicePath.getDeviceId();
            if (deviceId == INVALID_DEVICE_ID)
                return nullptr;
            auto plugin = bridge_.getPlugin(deviceId);
            if (!plugin)
                return nullptr;
            auto params = plugin->getAutomatableParameters();
            if (target.paramIndex >= 0 && target.paramIndex < static_cast<int>(params.size())) {
                return params[static_cast<size_t>(target.paramIndex)];
            }
            return nullptr;
        }

        case AutomationTargetType::Macro:
        case AutomationTargetType::ModParameter:
            // TODO: resolve macro/mod parameters to TE AutomatableParameters
            return nullptr;
    }

    return nullptr;
}

}  // namespace magda
