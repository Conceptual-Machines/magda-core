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
        // Transport just started. Curves were pre-baked on last stop (or on data
        // change while stopped), so only rebake if data changed since then.
        // Skipping redundant bake avoids destroying the already-built
        // AutomationIterator, which would cause TE to ignore the curve for
        // ~10ms (its async rebuild timer) — audible as a late automation onset.
        if (needsRebake_) {
            bakeAllLanes();
        } else {
            AutomationManager::getInstance().setPlaybackActive(true);
        }
    } else if (wasPlaying_ && !playing) {
        // Transport just stopped — clear TE curves, then immediately rebake
        // so curves are ready before the next play. The 10ms deferred iterator
        // rebuild will complete long before the user presses play again.
        // Manual fader control still works because playbackActive_ is false.
        clearAllLanes();
        bakeAllLanes();
        AutomationManager::getInstance().setPlaybackActive(false);
    } else if (playing && needsRebake_) {
        // Automation data changed during playback — rebake
        bakeAllLanes();
    } else if (!playing && needsRebake_) {
        // Automation data changed while stopped — rebake so curves are ready
        // before transport starts (prevents transient on first block)
        bakeAllLanes();
        // Clear playback flag since we're not playing
        AutomationManager::getInstance().setPlaybackActive(false);
    }

    wasPlaying_ = playing;
    needsRebake_ = false;
}

// ============================================================================
// AutomationManagerListener
// ============================================================================

void AutomationPlaybackEngine::automationLanesChanged() {
    needsRebake_ = true;
}

void AutomationPlaybackEngine::automationPointsChanged(AutomationLaneId /*laneId*/) {
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

    // Determine the beat range of the automation data
    double dataStartBeats = 0.0;
    double dataEndBeats = 0.0;

    if (lane.isAbsolute() && !lane.absolutePoints.empty()) {
        dataStartBeats = lane.absolutePoints.front().time;
        dataEndBeats = lane.absolutePoints.back().time;
    } else if (lane.isClipBased()) {
        // Find the overall range from all clips
        bool first = true;
        for (auto clipId : lane.clipIds) {
            const auto* clip = autoMgr.getClip(clipId);
            if (!clip)
                continue;
            if (first || clip->startTime < dataStartBeats)
                dataStartBeats = clip->startTime;
            if (first || clip->getEndTime() > dataEndBeats)
                dataEndBeats = clip->getEndTime();
            first = false;
        }
    }

    if (dataEndBeats <= dataStartBeats)
        return;

    // Convert edit length from seconds to beats for range comparison
    double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
    double editLengthBeats = edit_.getLength().inSeconds() * bpm / 60.0;

    // Extend baked range: start from beat 0 and go past the last point.
    // This ensures TE has explicit values before the first automation point
    // (preventing transients from default parameter values) and after the last
    // point (holding the final value until the end of the edit).
    double startBeats = 0.0;
    double endBeats = std::max(dataEndBeats, editLengthBeats);

    // Bake interval in beats (equivalent to ~10ms at current tempo)
    double bakeIntervalBeats = kBakeIntervalSeconds * bpm / 60.0;

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

    // Bake: sample our curve at regular beat intervals and write dense linear points to TE.
    // Automation data is stored in beats; convert to seconds only for TE's addPoint().
    for (double beat = startBeats; beat <= endBeats; beat += bakeIntervalBeats) {
        double normalizedValue = autoMgr.getValueAtTime(lane.id, beat);
        float teValue = convertValue(normalizedValue);
        auto teTime = edit_.tempoSequence.toTime(te::BeatPosition::fromBeats(beat));
        curve.addPoint(teTime, teValue, 0.0f, nullptr);
    }

    // Ensure the final point is exact
    double finalValue = autoMgr.getValueAtTime(lane.id, endBeats);
    float teFinalValue = convertValue(finalValue);
    auto teFinalTime = edit_.tempoSequence.toTime(te::BeatPosition::fromBeats(endBeats));
    curve.addPoint(teFinalTime, teFinalValue, 0.0f, nullptr);

    // Add exact automation point positions to preserve sharp transitions.
    // The regular sampling may skip over exact point boundaries, causing TE's
    // linear interpolation to smooth out intended sharp edges (e.g., a step
    // drop at a bar boundary lets through the first transient).
    const std::vector<AutomationPoint>* sourcePoints = nullptr;
    if (lane.isAbsolute()) {
        sourcePoints = &lane.absolutePoints;
    }
    // TODO: handle clip-based lanes similarly

    if (sourcePoints) {
        constexpr double kStepEpsilon = 0.0001;  // tiny beat offset for step edges
        for (size_t i = 0; i < sourcePoints->size(); ++i) {
            const auto& point = (*sourcePoints)[i];

            // For step curves, add a point just before this point at the
            // previous segment's held value so TE doesn't linearly ramp.
            if (i > 0 && (*sourcePoints)[i - 1].curveType == AutomationCurveType::Step) {
                double preStepBeat = point.time - kStepEpsilon;
                if (preStepBeat >= startBeats) {
                    double preValue = autoMgr.getValueAtTime(lane.id, preStepBeat);
                    float tePreValue = convertValue(preValue);
                    auto tePreTime =
                        edit_.tempoSequence.toTime(te::BeatPosition::fromBeats(preStepBeat));
                    curve.addPoint(tePreTime, tePreValue, 0.0f, nullptr);
                }
            }

            // Add the exact point value at its exact beat position
            float tePointValue = convertValue(point.value);
            auto tePointTime = edit_.tempoSequence.toTime(te::BeatPosition::fromBeats(point.time));
            curve.addPoint(tePointTime, tePointValue, 0.0f, nullptr);
        }
    }

    // Force synchronous AutomationIterator rebuild. Without this, TE defers
    // the rebuild to a 10ms timer, during which the curve is invisible to the
    // audio thread and the parameter falls back to its manual fader value.
    param->updateStream();
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
