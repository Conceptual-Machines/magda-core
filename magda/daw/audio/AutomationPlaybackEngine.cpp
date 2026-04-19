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

    // Stopped-state playhead sync: if the user scrubs the transport while
    // stopped, push the curve value at the new position onto each baked
    // parameter so the device UI, custom UIs, and the plugin's own UI all
    // land on the same value the lane readout / scale labels report. Without
    // this the plugin stays on whatever value we last committed (bake-end,
    // or an earlier scrub), and the user sees lane-vs-device drift.
    if (!playing) {
        const double nowSec = edit_.getTransport().getPosition().inSeconds();
        if (std::abs(nowSec - lastStoppedPlayheadSeconds_) > 1e-6) {
            for (auto& [param, info] : listenedParams_) {
                juce::ignoreUnused(info);
                if (param != nullptr)
                    param->updateToFollowCurve(edit_.getTransport().getPosition());
            }
            lastStoppedPlayheadSeconds_ = nowSec;
        }
    } else {
        lastStoppedPlayheadSeconds_ = -1.0;
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

    // Bake only the range that actually contains automation points — not
    // the full edit length. TE's AutomationCurve already clamps to the
    // first/last point's value for times outside the baked range, so
    // extending to bar 0 / edit end just to cover "no transients" adds
    // tens of thousands of redundant curve points on a long edit (e.g.
    // 512 bars × 10 ms = 100k samples per bake). That made every new
    // automation point lock the UI while we re-sampled the whole edit.
    double startBeats = dataStartBeats;
    double endBeats = dataEndBeats;

    // Bake interval in beats (equivalent to ~10ms at current tempo)
    double bakeIntervalBeats = kBakeIntervalSeconds * bpm / 60.0;

    // Shared converter: maps MAGDA's 0-1 normalized to TE's parameter range.
    //
    // Hoisted out of the sample loop — convertToTEValue fetches
    // target.getParameterInfo() per call, which walks the track /
    // rack / chain tree to resolve the device and copies a full
    // ParameterInfo (incl. valueTable, choices, shared_ptrs). With
    // ~100k loop iterations on a long edit that lookup is enough to
    // beach-ball the UI on every play / stop bake. Compute the TE
    // mapping once here and inline the 2 FLOPS in the hot loop.
    const ParameterInfo bakedInfo = (lane.target.type == AutomationTargetType::DeviceParameter)
                                        ? lane.target.getParameterInfo()
                                        : ParameterInfo{};
    const bool bakedIsDeviceParam = lane.target.type == AutomationTargetType::DeviceParameter;
    const float bakedTeMin = bakedInfo.teMinValue;
    const float bakedTeSpan = bakedInfo.teMaxValue - bakedInfo.teMinValue;
    const bool bakedUseTeRange = bakedIsDeviceParam && bakedTeSpan > 0.0f &&
                                 (std::abs(bakedInfo.minValue - bakedInfo.teMinValue) > 1e-6f ||
                                  std::abs(bakedInfo.maxValue - bakedInfo.teMaxValue) > 1e-6f);
    // For the info == TE-range path (most internal plugins, VSTs without
    // AI-Detect), we still need normalizedToReal to honour info.scale/
    // scaleAnchor. Precompute the info once — convertToTEValue itself
    // would otherwise re-fetch via target.getParameterInfo() on every
    // sample and walk the track/rack tree each time, beach-balling
    // play/stop on any edit with automation on a VST parameter.
    auto convertValue = [&](double magdaNormalized) -> float {
        if (bakedUseTeRange)
            return bakedTeMin + static_cast<float>(magdaNormalized) * bakedTeSpan;
        if (!bakedIsDeviceParam)
            return convertToTEValue(lane.target, param, magdaNormalized);
        if (bakedInfo.maxValue > bakedInfo.minValue)
            return ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized), bakedInfo);
        if (bakedTeSpan > 0.0f)
            return bakedTeMin + static_cast<float>(magdaNormalized) * bakedTeSpan;
        return static_cast<float>(magdaNormalized);
    };

    // Bake: write ONE TE point per source MAGDA point. te::AutomationCurve
    // already linearly interpolates between its stored points (matching
    // MAGDA's Linear curve type), so the dense 10ms resampling we used to
    // do was redundant — and each te::AutomationCurve::addPoint is ~2ms
    // (valueTree mutation + listener fan-out), so 3000 samples = 6 seconds
    // of main-thread stall per bake, which is what was locking up the UI
    // after every automation edit. For bezier / step curves we inject a
    // couple of helper points so TE's linear-only iterator still matches
    // the MAGDA shape.
    const std::vector<AutomationPoint>* sourcePoints = nullptr;
    if (lane.isAbsolute())
        sourcePoints = &lane.absolutePoints;
    // TODO: handle clip-based lanes similarly

    auto addTEPoint = [&](double beat, double normalizedValue) {
        float teValue = convertValue(normalizedValue);
        auto teTime = edit_.tempoSequence.toTime(te::BeatPosition::fromBeats(beat));
        curve.addPoint(teTime, teValue, 0.0f, nullptr);
    };

    if (sourcePoints && !sourcePoints->empty()) {
        constexpr double kStepEpsilon = 0.0001;  // tiny beat offset for step edges
        constexpr int kBezierSegments = 12;      // coarse tessellation for bezier curves
        for (size_t i = 0; i < sourcePoints->size(); ++i) {
            const auto& point = (*sourcePoints)[i];

            // Step curve: hold the previous segment's value right up to this
            // point's position, then jump — TE's linear iterator otherwise
            // ramps between the two points and lets the old value through.
            if (i > 0 && (*sourcePoints)[i - 1].curveType == AutomationCurveType::Step) {
                double preStepBeat = point.time - kStepEpsilon;
                if (preStepBeat > (*sourcePoints)[i - 1].time)
                    addTEPoint(preStepBeat, autoMgr.getValueAtTime(lane.id, preStepBeat));
            }

            // Bezier: tessellate the segment between the previous and current
            // point, so TE's linear interpolation follows the curved shape.
            if (i > 0 && (*sourcePoints)[i - 1].curveType == AutomationCurveType::Bezier) {
                const auto& prev = (*sourcePoints)[i - 1];
                const double span = point.time - prev.time;
                if (span > 0.0) {
                    for (int s = 1; s < kBezierSegments; ++s) {
                        double t = static_cast<double>(s) / kBezierSegments;
                        double beat = prev.time + span * t;
                        addTEPoint(beat, autoMgr.getValueAtTime(lane.id, beat));
                    }
                }
            }

            addTEPoint(point.time, point.value);
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
        case AutomationTargetType::TrackVolume:
        case AutomationTargetType::SendLevel: {
            // MAGDA 0-1 (FaderDB scale) → dB → TE fader position. Same
            // mapping for both: AuxSendPlugin's `gain` parameter uses
            // volume-fader-position units just like VolAndPanPlugin.
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
            // Device parameters: the lane stores MAGDA-normalized [0,1] values.
            // TE's AutomatableParameter stores plugin-native values —
            // always [0,1] for external VSTs, the raw native range for
            // internal plugins (e.g. 0..135 for 4OSC filterFreq).
            //
            // When info.min/max match the TE-native range (internal plugins,
            // or external VSTs before AI-Detect) go through
            // normalizedToReal so log scales and scaleAnchors are honoured.
            //
            // When they differ (external VST with AI-Detect display range)
            // normalizedToReal would return a display-range value (e.g.
            // -48..+48 semitones) that TE then clips to its 0..1 param
            // range — the source of the "curve moves but plugin doesn't"
            // drift. Fall back to a linear mapping onto the NATIVE TE
            // range instead, so the lane's normalized [0,1] reaches the
            // plugin unchanged.
            ParameterInfo info = target.getParameterInfo();
            const float teSpan = info.teMaxValue - info.teMinValue;
            if (teSpan <= 0.0f) {
                if (!param)
                    return static_cast<float>(magdaNormalized);
                auto range = param->getValueRange();
                return range.getStart() +
                       static_cast<float>(magdaNormalized) * (range.getEnd() - range.getStart());
            }
            const bool infoMatchesTeRange = std::abs(info.minValue - info.teMinValue) < 1e-6f &&
                                            std::abs(info.maxValue - info.teMaxValue) < 1e-6f;
            if (infoMatchesTeRange && info.maxValue > info.minValue)
                return ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized), info);
            return info.teMinValue + static_cast<float>(magdaNormalized) * teSpan;
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

        case AutomationTargetType::SendLevel: {
            auto* track = bridge_.getAudioTrack(target.trackId);
            if (!track)
                return nullptr;
            if (auto* auxSend = track->getAuxSendPlugin(target.sendBusIndex)) {
                return auxSend->gain.get();
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
        case AutomationTargetType::TrackVolume:
        case AutomationTargetType::SendLevel: {
            // TE fader position → dB → MAGDA 0-1 (FaderDB scale). Mirror of
            // the forward path; kept identical for TrackVolume and SendLevel.
            auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
            float dB = te::volumeFaderPositionToDB(teValue);
            return ParameterUtils::realToNormalized(dB, paramInfo);
        }
        case AutomationTargetType::TrackPan: {
            auto paramInfo = ParameterPresets::pan(-1, "Pan");
            return ParameterUtils::realToNormalized(teValue, paramInfo);
        }
        default: {
            // Inverse of convertToTEValue — keep the two symmetric or the
            // round-trip (MAGDA normalized -> TE raw -> MAGDA normalized)
            // will drift and the UI will fight the curve.
            ParameterInfo info = target.getParameterInfo();
            const float teSpan = info.teMaxValue - info.teMinValue;
            if (teSpan <= 0.0f) {
                if (!param)
                    return teValue;
                auto range = param->getValueRange();
                float span = range.getEnd() - range.getStart();
                if (span <= 0.0f)
                    return 0.0;
                return juce::jlimit(0.0, 1.0,
                                    static_cast<double>((teValue - range.getStart()) / span));
            }
            const bool infoMatchesTeRange = std::abs(info.minValue - info.teMinValue) < 1e-6f &&
                                            std::abs(info.maxValue - info.teMaxValue) < 1e-6f;
            if (infoMatchesTeRange && info.maxValue > info.minValue)
                return ParameterUtils::realToNormalized(teValue, info);
            return juce::jlimit(0.0, 1.0,
                                static_cast<double>((teValue - info.teMinValue) / teSpan));
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
        target.type == AutomationTargetType::TrackPan ||
        target.type == AutomationTargetType::SendLevel) {
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
        } else if (target.type == AutomationTargetType::TrackPan) {
            trackMgr.setTrackPan(target.trackId, real, /*fromAutomation=*/true);
        } else {
            // SendLevel: same fader-dB → linear-gain mapping as TrackVolume.
            float gain = std::pow(10.0f, real / 20.0f);
            trackMgr.setSendLevel(target.trackId, target.sendBusIndex, gain,
                                  /*fromAutomation=*/true);
        }
    }
}

}  // namespace magda
