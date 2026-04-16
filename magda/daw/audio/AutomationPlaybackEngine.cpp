#include "AutomationPlaybackEngine.hpp"

#include <cmath>

#include "../core/AutomationManager.hpp"
#include "../core/ParameterInfo.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/TrackManager.hpp"
#include "AudioBridge.hpp"

// INVARIANT — Parameter value representations across MAGDA:
//
//   Stored in AutomationPoint.value:           normalized [0, 1]
//   Stored in ParameterInfo.min/max/default/currentValue: REAL (Hz, dB, %, …)
//   Stored on te::AutomatableParameter:        REAL (native parameter units)
//
// Conversions use EXCLUSIVELY ParameterUtils::normalizedToReal and
// ParameterUtils::realToNormalized — both honour info.scale and
// info.scaleAnchor. Do not add ad-hoc lerps anywhere on this boundary.
// Violating this invariant manifests as a slider/lane visual mismatch
// (display reads one value, audio hears another).

namespace magda {

AutomationPlaybackEngine::AutomationPlaybackEngine(AudioBridge& bridge, te::Edit& edit)
    : bridge_(bridge), edit_(edit) {
    auto& mgr = AutomationManager::getInstance();
    mgr.addListener(this);

    // When a user grabs an automated control, clear the baked curve so TE
    // stops writing to the parameter; when they release, rebake from the
    // stored lane data so automation resumes. This is the cheap,
    // per-gesture equivalent of a "touch" write mode.
    mgr.setTouchSuppressionListener([this](AutomationLaneId laneId, bool suppressed) {
        auto* lane = AutomationManager::getInstance().getLane(laneId);
        if (!lane)
            return;
        if (suppressed) {
            clearLane(*lane);
            if (auto* param = resolveParameter(lane->target))
                param->updateStream();
        } else {
            bakeLane(*lane);
        }
    });
}

AutomationPlaybackEngine::~AutomationPlaybackEngine() {
    auto& mgr = AutomationManager::getInstance();
    mgr.removeListener(this);
    mgr.setTouchSuppressionListener({});

    // Detach from every TE parameter we were listening on — otherwise the
    // parameter keeps a dangling pointer and will crash on the next value
    // change notification.
    for (auto& [param, info] : listenedParams_) {
        juce::ignoreUnused(info);
        if (param != nullptr)
            param->removeListener(this);
    }
    listenedParams_.clear();
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

void AutomationPlaybackEngine::automationPointsChanged(AutomationLaneId laneId) {
    needsRebake_ = true;

    // Real-time sync while the transport is stopped: rebake this single lane
    // immediately instead of waiting for the next 30Hz process() tick, then
    // push the current-playhead value through the parameter so any listening
    // UI (fader, knob, label) follows the curve edit without a perceptible
    // delay. Curve mutations are always dispatched from the UI thread, so
    // calling bakeLane — which touches te::AutomationCurve — is safe here.
    //
    // During playback we leave the coalesced needsRebake_ path in place so
    // rapid edits don't thrash TE's iterator rebuild.
    if (edit_.getTransport().isPlaying())
        return;

    auto* lane = AutomationManager::getInstance().getLane(laneId);
    if (!lane || !lane->hasData())
        return;

    // Ensure bakedTargets_ reflects this lane so syncParameterListeners
    // registers a listener on its parameter. Otherwise a curve edit that
    // introduces a brand-new lane (first point placed) would bake its values
    // without ever subscribing, and the UI would stop tracking it.
    if (std::none_of(bakedTargets_.begin(), bakedTargets_.end(),
                     [&](const AutomationTarget& t) { return t == lane->target; })) {
        bakedTargets_.push_back(lane->target);
    }

    bakeLane(*lane);
    syncParameterListeners();
    needsRebake_ = false;
}

void AutomationPlaybackEngine::automationLanePropertyChanged(AutomationLaneId /*laneId*/) {
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

    // Clear curves for any target we baked last time that is no longer
    // backed by a live lane — otherwise a deleted lane keeps driving its
    // parameter because bakeLane() only visits lanes that still exist.
    std::vector<AutomationTarget> newTargets;
    for (const auto& lane : autoMgr.getLanes()) {
        if (lane.hasData())
            newTargets.push_back(lane.target);
    }
    for (const auto& old : bakedTargets_) {
        bool stillActive = std::any_of(newTargets.begin(), newTargets.end(),
                                       [&](const AutomationTarget& t) { return t == old; });
        if (stillActive)
            continue;
        if (auto* param = resolveParameter(old)) {
            param->getCurve().clear(nullptr);
            param->updateStream();
        }
    }
    bakedTargets_ = std::move(newTargets);

    for (const auto& lane : autoMgr.getLanes()) {
        if (lane.hasData())
            bakeLane(lane);
    }

    // Subscribe to currentValueChanged on every baked param so the UI can
    // follow curve-driven writes (playback, stopped rebake, drag commits)
    // without polling.
    syncParameterListeners();
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

    // Bypass or live touch-suppression: leave the curve empty so TE's audio
    // thread falls back to the parameter's manual/static value. Force iterator
    // rebuild so the change takes effect on the next audio block rather than
    // after TE's 10ms timer.
    if (lane.bypass || lane.touchSuppressed) {
        param->updateStream();
        return;
    }

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

    // Note: dataStartBeats == dataEndBeats is legal — it means the lane has a
    // single point (or a single clip with one point). We still want to bake
    // that value across the edit so TE holds it as a constant. Only bail if
    // we truly have no data to work with.
    if (dataEndBeats < dataStartBeats)
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

    // Shared converter: maps MAGDA's 0-1 normalized to TE's parameter range.
    auto convertValue = [&](double magdaNormalized) -> float {
        return convertToTEValue(lane.target, param, magdaNormalized);
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

    // When the transport is stopped, TE's audio thread isn't evaluating the
    // curve, so the parameter would stay pinned at its manual fader value
    // until the user presses play. Push the curve's value at the current
    // playhead through immediately so the UI reflects edits while stopped.
    if (!edit_.getTransport().isPlaying()) {
        param->updateToFollowCurve(edit_.getTransport().getPosition());
    }
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

float AutomationPlaybackEngine::convertToTEValue(const AutomationTarget& target,
                                                 te::AutomatableParameter* param,
                                                 double magdaNormalized) const {
    switch (target.type) {
        case AutomationTargetType::TrackVolume: {
            // MAGDA 0-1 (FaderDB scale) → dB → TE fader position
            auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
            float dB =
                ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized), paramInfo);
            return te::decibelsToVolumeFaderPosition(dB);
        }
        case AutomationTargetType::TrackPan: {
            // MAGDA 0-1 → linear -1..+1 (same as TE's pan range)
            auto paramInfo = ParameterPresets::pan(-1, "Pan");
            return ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized), paramInfo);
        }
        default: {
            // Device parameters: convert via the parameter's own ParameterInfo
            // so log scales, scaleAnchor, and displayFormat all stay consistent
            // with what the slider and the automation lane display. See the
            // invariant at the top of this file.
            ParameterInfo info = target.getParameterInfo();
            // Fall back to the TE-reported range when ParameterInfo is missing
            // (e.g. the device hasn't been populated yet).
            if (info.maxValue <= info.minValue) {
                if (!param)
                    return static_cast<float>(magdaNormalized);
                auto range = param->getValueRange();
                return range.getStart() +
                       static_cast<float>(magdaNormalized) * (range.getEnd() - range.getStart());
            }
            return ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized), info);
        }
    }
}

void AutomationPlaybackEngine::automationPointDragPreview(AutomationLaneId laneId,
                                                          AutomationPointId /*pointId*/,
                                                          double /*previewTime*/,
                                                          double previewValue) {
    // Fluid drag preview: republish the dragged value as an automation value
    // change so UI listeners (fader labels, device knobs, custom UIs) can
    // reflect the edit without a round-trip through the TE parameter — which
    // would fight the already-baked curve and produce flicker when stopped.
    //
    // The stored lane points are untouched; on mouseUp the real commit runs
    // through automationPointsChanged → bakeLane as usual.
    AutomationManager::getInstance().notifyValueChanged(laneId, previewValue);
}

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

double AutomationPlaybackEngine::convertFromTEValue(const AutomationTarget& target,
                                                    te::AutomatableParameter* param,
                                                    float teValue) const {
    switch (target.type) {
        case AutomationTargetType::TrackVolume: {
            // TE fader position → dB → MAGDA 0-1 (FaderDB scale)
            auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
            float dB = te::volumeFaderPositionToDB(teValue);
            return ParameterUtils::realToNormalized(dB, paramInfo);
        }
        case AutomationTargetType::TrackPan: {
            auto paramInfo = ParameterPresets::pan(-1, "Pan");
            return ParameterUtils::realToNormalized(teValue, paramInfo);
        }
        default: {
            // Device parameters: go through ParameterUtils so the reverse
            // mapping honours info.scale + info.scaleAnchor, matching
            // convertToTEValue exactly. This keeps the round-trip
            // (normalized → real → normalized) self-consistent — otherwise
            // a curve-driven listener writeback snaps the UI to the wrong
            // normalized value, which the lane then re-maps through the
            // asymmetric forward path and ends up displaying a different
            // real value from what was committed.
            ParameterInfo info = target.getParameterInfo();
            if (info.maxValue > info.minValue)
                return ParameterUtils::realToNormalized(teValue, info);
            // Fall back to the TE-reported range when ParameterInfo is missing.
            if (!param)
                return teValue;
            auto range = param->getValueRange();
            float span = range.getEnd() - range.getStart();
            if (span <= 0.0f)
                return 0.0;
            return juce::jlimit(0.0, 1.0, static_cast<double>((teValue - range.getStart()) / span));
        }
    }
}

void AutomationPlaybackEngine::syncParameterListeners() {
    // Build the set of parameters that should currently be listened on —
    // one per live baked target. A target that no longer resolves (device
    // removed, track gone) drops out naturally.
    auto& autoMgr = AutomationManager::getInstance();
    std::unordered_map<te::AutomatableParameter*, ListenedParamInfo> desired;
    for (const auto& target : bakedTargets_) {
        auto* param = resolveParameter(target);
        if (!param)
            continue;
        AutomationLaneId laneId = autoMgr.getLaneForTarget(target);
        if (laneId == INVALID_AUTOMATION_LANE_ID)
            continue;
        desired[param] = ListenedParamInfo{laneId, target};
    }

    // Remove listeners for params no longer in the desired set.
    for (auto it = listenedParams_.begin(); it != listenedParams_.end();) {
        if (desired.find(it->first) == desired.end()) {
            if (it->first != nullptr)
                it->first->removeListener(this);
            it = listenedParams_.erase(it);
        } else {
            ++it;
        }
    }

    // Add listeners for new params; refresh info for existing ones so the
    // lane id tracks target re-binds.
    for (auto& [param, info] : desired) {
        auto [it, inserted] = listenedParams_.insert({param, info});
        if (inserted) {
            param->addListener(this);
        } else {
            it->second = info;
        }
    }
}

void AutomationPlaybackEngine::currentValueChanged(te::AutomatableParameter& param) {
    // Coalesced async callback from TE's message thread — fired whenever a
    // baked curve or updateToFollowCurve() writes a new value into the
    // parameter. Translate back to MAGDA 0..1 and broadcast so UI listeners
    // (fader labels, device knobs, custom UIs) track the curve live.
    auto it = listenedParams_.find(&param);
    if (it == listenedParams_.end())
        return;

    const auto& target = it->second.target;
    double normalized = convertFromTEValue(target, &param, param.getCurrentValue());
    AutomationManager::getInstance().notifyValueChanged(it->second.laneId, normalized);

    // Keep MAGDA's TrackInfo cache in sync with what TE just wrote, so any UI
    // that reads TrackInfo.volume / TrackInfo.pan (track inspector, mixer,
    // session view) follows the curve without having to subscribe to
    // AutomationManager directly. AudioBridge::trackPropertyChanged skips
    // the volume/pan writeback while playback is active, so going through
    // setTrackVolume/setTrackPan here won't fight TE's automation.
    if (target.type == AutomationTargetType::TrackVolume ||
        target.type == AutomationTargetType::TrackPan) {
        ParameterInfo info = target.getParameterInfo();
        float real = ParameterUtils::normalizedToReal(static_cast<float>(normalized), info);

        auto& trackMgr = TrackManager::getInstance();
        // Scope the re-entrancy flag so AudioBridge can distinguish this
        // automation-driven writeback from user-initiated fader/pan edits.
        AutomationManager::AutomationWriteScope writeScope;
        if (target.type == AutomationTargetType::TrackVolume) {
            // Target param range is in dB; convert back to linear gain.
            float gain = std::pow(10.0f, real / 20.0f);
            trackMgr.setTrackVolume(target.trackId, gain, /*fromAutomation=*/true);
        } else {
            trackMgr.setTrackPan(target.trackId, real, /*fromAutomation=*/true);
        }
    }
}

}  // namespace magda
