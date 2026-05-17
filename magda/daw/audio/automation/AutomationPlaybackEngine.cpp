#include "automation/AutomationPlaybackEngine.hpp"

#include <algorithm>
#include <cmath>

#include "../../core/AutomationManager.hpp"
#include "../../core/ParameterInfo.hpp"
#include "../../core/ParameterUtils.hpp"
#include "../../core/TrackManager.hpp"
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
        DBG("[AutoPb] touchSuppressionListener lane=" << laneId << " suppressed=" << (int)suppressed
                                                      << " bypass=" << (int)lane->bypass);
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
    // change notification. Done first so the curve clears below don't
    // trigger callbacks back at us. Re-resolve via the cached target before
    // dereferencing the pointer: if a device was removed mid-session, our
    // cached pointer dangles and removeListener would crash.
    for (auto& [param, info] : listenedParams_) {
        auto* live = resolveParameter(info.target);
        if (live != nullptr && live == param)
            param->removeListener(this);
    }
    listenedParams_.clear();

    // Clear all baked curves while the Edit is still alive. If we don't,
    // MacroParameters and Modifier AutomatableParameters get destroyed with
    // a populated curve → their AutomationCurveSource holds a live
    // ScopedActiveParameter → its destructor decrements
    // automatableEditElement.numActiveParameters which can have already
    // been zeroed during teardown (TE's per-edit-element bookkeeping
    // interleaves with macro/mod parameter destruction order). Plugin
    // params don't hit this because their AutomatableEditElement (the
    // plugin) lives at least as long as the parameter's TE objects.
    clearAllLanes();
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
        // Release any touchSuppressed flag a UI gesture may have left behind
        // (component destroyed mid-drag, modal opened, etc.) — otherwise the
        // upcoming bake skips that lane and the parameter silently stops
        // following automation until the user re-touches the control.
        AutomationManager::getInstance().clearAllTouchSuppression();
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
            // Re-resolve each param from its target before touching it: when
            // a device is removed mid-session, the lane stays in
            // bakedTargets_ until the next bake, but the cached pointer in
            // listenedParams_ now dangles. Dropping the entry here keeps
            // the next iteration safe; full cleanup happens in the next
            // syncParameterListeners() pass.
            for (auto it = listenedParams_.begin(); it != listenedParams_.end();) {
                auto* live = resolveParameter(it->second.target);
                if (live == nullptr || live != it->first) {
                    // Stale entry. Do NOT call removeListener — the cached
                    // pointer may already be freed.
                    it = listenedParams_.erase(it);
                    continue;
                }
                live->updateToFollowCurve(edit_.getTransport().getPosition());
                ++it;
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

    // Immediate cleanup of any baked targets whose lane was just deleted (or
    // emptied of points). Without this the deleted lane's last-baked value
    // keeps driving TE's parameter until the next bakeAllLanes() — which only
    // runs on transport start. The fader UI shows the user's manual value but
    // TE plays the stale automated one.
    auto& autoMgr = AutomationManager::getInstance();
    std::vector<AutomationTarget> live;
    live.reserve(bakedTargets_.size());
    for (const auto& lane : autoMgr.getLanes()) {
        if (lane.hasData())
            live.push_back(lane.target);
    }
    for (const auto& old : bakedTargets_) {
        bool stillActive = std::any_of(live.begin(), live.end(),
                                       [&](const AutomationTarget& t) { return t == old; });
        if (!stillActive)
            clearStaleTarget(old);
    }
    bakedTargets_ = std::move(live);
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
        if (!stillActive)
            clearStaleTarget(old);
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

void AutomationPlaybackEngine::clearStaleTarget(const AutomationTarget& target) {
    auto* param = resolveParameter(target);
    if (!param)
        return;

    param->getCurve().clear(nullptr);

    // Restore the user's manual value where MAGDA tracks one separately. For
    // device parameters / macros / sends there's no separate manual store, so
    // we just clear the curve and leave the parameter at whatever TE last had.
    const auto* track = TrackManager::getInstance().getTrack(target.devicePath.trackId);
    if (track) {
        if (target.kind == ControlTarget::Kind::TrackVolume) {
            float manualDb = juce::Decibels::gainToDecibels(track->manualVolume);
            param->setParameter(te::decibelsToVolumeFaderPosition(manualDb),
                                juce::sendNotificationSync);
        } else if (target.kind == ControlTarget::Kind::TrackPan) {
            param->setParameter(track->manualPan, juce::sendNotificationSync);
        }
    }

    param->updateStream();
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
        dataStartBeats = lane.absolutePoints.front().beatPosition;
        dataEndBeats = lane.absolutePoints.back().beatPosition;
    } else if (lane.isClipBased()) {
        // Find the overall range from all clips
        bool first = true;
        for (auto clipId : lane.clipIds) {
            const auto* clip = autoMgr.getClip(clipId);
            if (!clip)
                continue;
            if (first || clip->startBeats < dataStartBeats)
                dataStartBeats = clip->startBeats;
            if (first || clip->getEndBeats() > dataEndBeats)
                dataEndBeats = clip->getEndBeats();
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
    // getParameterInfoForTarget(target) per call, which walks the track /
    // rack / chain tree to resolve the device and copies a full
    // ParameterInfo (incl. valueTable, choices, shared_ptrs). With
    // ~100k loop iterations on a long edit that lookup is enough to
    // beach-ball the UI on every play / stop bake. Compute the TE
    // mapping once here and inline the 2 FLOPS in the hot loop.
    const ParameterInfo bakedInfo = (lane.target.kind == ControlTarget::Kind::PluginParam)
                                        ? getParameterInfoForTarget(lane.target)
                                        : ParameterInfo{};
    const bool bakedIsDeviceParam = lane.target.kind == ControlTarget::Kind::PluginParam;
    const float bakedTeMin = bakedInfo.teMinValue;
    const float bakedTeSpan = bakedInfo.teMaxValue - bakedInfo.teMinValue;
    const bool bakedUseTeRange = bakedIsDeviceParam && bakedTeSpan > 0.0f &&
                                 !ParameterUtils::infoMatchesTeRange(bakedInfo) &&
                                 !ParameterUtils::isDisplayMappedInternalValue(bakedInfo);
    // For the info == TE-range path (most internal plugins, VSTs without
    // AI-Detect), we still need normalizedToReal to honour info.scale/
    // scaleAnchor. Precompute the info once — convertToTEValue itself
    // would otherwise re-fetch via getParameterInfoForTarget(target) on every
    // sample and walk the track/rack tree each time, beach-balling
    // play/stop on any edit with automation on a VST parameter.
    auto convertValue = [&](double magdaNormalized) -> float {
        if (bakedUseTeRange)
            return bakedTeMin + static_cast<float>(magdaNormalized) * bakedTeSpan;
        if (!bakedIsDeviceParam)
            return convertToTEValue(lane.target, param, magdaNormalized);
        return ParameterUtils::normalizedToModelValue(
                   ParameterNormalizedValue::clamped(static_cast<float>(magdaNormalized)),
                   bakedInfo)
            .value;
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
        // Store as beats so tempo changes shift the curve with the grid
        // instead of leaving it pinned at seconds offsets from bake time.
        curve.addPoint(te::EditPosition{te::BeatPosition::fromBeats(beat)}, teValue, 0.0f, nullptr);
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
                double preStepBeat = point.beatPosition - kStepEpsilon;
                if (preStepBeat > (*sourcePoints)[i - 1].beatPosition)
                    addTEPoint(preStepBeat, autoMgr.getValueAtBeat(lane.id, preStepBeat));
            }

            // Tessellate any non-straight segment so TE's linear iterator
            // follows the shape. Bezier always needs it; Linear segments need
            // it whenever tension is non-zero (the UI bends the slope via
            // interpolateWithTension, which without tessellation would be
            // baked as just the two endpoints and play back as a straight
            // ramp regardless of the visible curve).
            if (i > 0) {
                const auto& prev = (*sourcePoints)[i - 1];
                const bool isBezier = prev.curveType == AutomationCurveType::Bezier;
                const bool isCurvedLinear = prev.curveType == AutomationCurveType::Linear &&
                                            std::abs(prev.tension) >= 0.001;
                if (isBezier || isCurvedLinear) {
                    const double span = point.beatPosition - prev.beatPosition;
                    if (span > 0.0) {
                        for (int s = 1; s < kBezierSegments; ++s) {
                            double t = static_cast<double>(s) / kBezierSegments;
                            double beat = prev.beatPosition + span * t;
                            addTEPoint(beat, autoMgr.getValueAtBeat(lane.id, beat));
                        }
                    }
                }
            }

            addTEPoint(point.beatPosition, point.value);
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
    switch (target.kind) {
        case ControlTarget::Kind::DeviceMacro:
            // Macros are stored as 0..1 on both sides — no display/percent
            // scale conversion. Going through the percent ParameterInfo
            // fallback would write 100 to TE for a 1.0 MAGDA value, and the
            // inverse writeback would then divide by 100, pinning the UI
            // knob near zero throughout playback.
            return juce::jlimit(0.0f, 1.0f, static_cast<float>(magdaNormalized));

        case ControlTarget::Kind::TrackVolume:
        case ControlTarget::Kind::SendLevel: {
            // MAGDA 0-1 (FaderDB scale) → dB → TE fader position. Same
            // mapping for both: AuxSendPlugin's `gain` parameter uses
            // volume-fader-position units just like VolAndPanPlugin.
            auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
            float dB =
                ParameterUtils::normalizedToReal(static_cast<float>(magdaNormalized), paramInfo);
            return te::decibelsToVolumeFaderPosition(dB);
        }
        case ControlTarget::Kind::TrackPan: {
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
            ParameterInfo info = getParameterInfoForTarget(target);
            const float teSpan = info.teMaxValue - info.teMinValue;
            if (teSpan <= 0.0f) {
                if (!param)
                    return static_cast<float>(magdaNormalized);
                auto range = param->getValueRange();
                return range.getStart() +
                       static_cast<float>(magdaNormalized) * (range.getEnd() - range.getStart());
            }
            return ParameterUtils::normalizedToModelValue(
                       ParameterNormalizedValue::clamped(static_cast<float>(magdaNormalized)), info)
                .value;
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

    // For Macro / ModParameter targets the slider reads from MAGDA state
    // (MacroInfo.value / ModInfo.rate), not the TE parameter — so without
    // a state writeback the slider stays stuck during a drag while
    // stopped. Mirror the writeback that currentValueChanged does on the
    // playback side. Wrapped in AutomationWriteScope so the corresponding
    // resync paths (deviceModifiersChanged / macroValueChanged) skip
    // pushing back into TE while the user is editing the curve.
    auto* lane = AutomationManager::getInstance().getLane(laneId);
    if (!lane)
        return;
    const auto& target = lane->target;
    if (target.kind == ControlTarget::Kind::DeviceMacro) {
        writeMacroValueFromCurve(target, previewValue, true);
    } else if (target.kind == ControlTarget::Kind::ModParam && target.modParamIndex == 0) {
        writeModRateFromCurve(target, previewValue);

        // The MAGDA-side writeback above is wrapped in AutomationWriteScope
        // to prevent the deviceModifiersChanged → resync path from fighting
        // the baked curve. As a side-effect TE's LFO never sees the new rate
        // until mouse-up, so the user hears no change while dragging. Push
        // the dragged value directly to the active TE param (rateParam in Hz
        // mode, rateTypeParam in sync mode — findModifierParameterForAutomation
        // picks the right one) so audio tracks the drag in real time.
        if (auto* teParam = bridge_.getPluginManager().findModifierParameterForAutomation(
                target.devicePath.trackId, target.devicePath, target.modId, 0)) {
            ParameterInfo info = getParameterInfoForTarget(target);
            float real = ParameterUtils::normalizedToReal(static_cast<float>(previewValue), info);
            // Sync mode lane stores 0-based display index; the TE rateType
            // param expects a 1-based ordinal, so shift by +1 there. In Hz
            // mode the lane stores the Hz value directly.
            float teValue = info.scale == ParameterScale::Discrete
                                ? static_cast<float>(
                                      juce::jlimit(1, 23, static_cast<int>(std::round(real)) + 1))
                                : real;
            teParam->setParameterFromHost(teValue, juce::dontSendNotification);
        }
    }
}

te::AutomatableParameter* AutomationPlaybackEngine::resolveParameter(
    const AutomationTarget& target) {
    return bridge_.resolveControlTarget(target);
}

double AutomationPlaybackEngine::convertFromTEValue(const AutomationTarget& target,
                                                    te::AutomatableParameter* param,
                                                    float teValue) const {
    switch (target.kind) {
        case ControlTarget::Kind::DeviceMacro:
            // Mirror of convertToTEValue: macros are 0..1 on both sides.
            return juce::jlimit(0.0, 1.0, static_cast<double>(teValue));

        case ControlTarget::Kind::TrackVolume:
        case ControlTarget::Kind::SendLevel: {
            // TE fader position → dB → MAGDA 0-1 (FaderDB scale). Mirror of
            // the forward path; kept identical for TrackVolume and SendLevel.
            auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
            float dB = te::volumeFaderPositionToDB(teValue);
            return ParameterUtils::realToNormalized(dB, paramInfo);
        }
        case ControlTarget::Kind::TrackPan: {
            auto paramInfo = ParameterPresets::pan(-1, "Pan");
            return ParameterUtils::realToNormalized(teValue, paramInfo);
        }
        default: {
            // Inverse of convertToTEValue — keep the two symmetric or the
            // round-trip (MAGDA normalized -> TE raw -> MAGDA normalized)
            // will drift and the UI will fight the curve.
            ParameterInfo info = getParameterInfoForTarget(target);
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
            return ParameterUtils::modelToNormalizedValue(ParameterModelValue{teValue}, info).value;
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

    // Remove listeners for params no longer in the desired set. Re-resolve
    // each cached entry's target before calling removeListener — if the
    // target no longer resolves to the same param (device removed, or
    // re-bound to a fresh pointer), the cached pointer is dangling and
    // calling removeListener on it would crash. TE's listener list lives
    // inside the parameter, so when the parameter dies the registration
    // dies with it; skipping removeListener here just avoids the UAF.
    for (auto it = listenedParams_.begin(); it != listenedParams_.end();) {
        if (desired.find(it->first) == desired.end()) {
            auto* live = resolveParameter(it->second.target);
            if (live != nullptr && live == it->first)
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
    if (target.kind == ControlTarget::Kind::DeviceMacro &&
        std::find(macroWritebacksInProgress_.begin(), macroWritebacksInProgress_.end(), target) !=
            macroWritebacksInProgress_.end()) {
        return;
    }

    double normalized = convertFromTEValue(target, &param, param.getCurrentValue());
    AutomationManager::getInstance().notifyValueChanged(it->second.laneId, normalized);

    // Keep MAGDA's TrackInfo cache in sync with what TE just wrote, so any UI
    // that reads TrackInfo.volume / TrackInfo.pan (track inspector, mixer,
    // session view) follows the curve without having to subscribe to
    // AutomationManager directly. AudioBridge::trackPropertyChanged skips
    // the volume/pan writeback while playback is active, so going through
    // setTrackVolume/setTrackPan here won't fight TE's automation.
    if (target.kind == ControlTarget::Kind::TrackVolume ||
        target.kind == ControlTarget::Kind::TrackPan ||
        target.kind == ControlTarget::Kind::SendLevel) {
        ParameterInfo info = getParameterInfoForTarget(target);
        float real = ParameterUtils::normalizedToReal(static_cast<float>(normalized), info);

        auto& trackMgr = TrackManager::getInstance();
        // Scope the re-entrancy flag so AudioBridge can distinguish this
        // automation-driven writeback from user-initiated fader/pan edits.
        AutomationManager::AutomationWriteScope writeScope;
        if (target.kind == ControlTarget::Kind::TrackVolume) {
            // Target param range is in dB; convert back to linear gain.
            float gain = std::pow(10.0f, real / 20.0f);
            trackMgr.setTrackVolume(target.devicePath.trackId, gain, /*fromAutomation=*/true);
        } else if (target.kind == ControlTarget::Kind::TrackPan) {
            trackMgr.setTrackPan(target.devicePath.trackId, real, /*fromAutomation=*/true);
        } else {
            // SendLevel: same fader-dB → linear-gain mapping as TrackVolume.
            float gain = std::pow(10.0f, real / 20.0f);
            trackMgr.setSendLevel(target.devicePath.trackId, target.sendBusIndex, gain,
                                  /*fromAutomation=*/true);
        }
    } else if (target.kind == ControlTarget::Kind::DeviceMacro) {
        // Mirror the curve value back into MacroInfo.value so the knob UI
        // (which reads from TrackManager) follows the curve. The TE macro
        // param already holds this value when currentValueChanged fires; only
        // drag preview needs to seed it manually.
        writeMacroValueFromCurve(target, normalized, false);
    } else if (target.kind == ControlTarget::Kind::ModParam && target.modParamIndex == 0) {
        // Mirror the curve value back into MAGDA's mod state. The lane is
        // mode-aware — Hz value or sync division depending on tempoSync.
        // AudioBridge::deviceModifiersChanged checks AutomationWriteScope and
        // skips its resync to avoid fighting the live TE curve.
        writeModRateFromCurve(target, normalized);
    }
}

void AutomationPlaybackEngine::writeMacroValueFromCurve(const AutomationTarget& target,
                                                        double normalized,
                                                        bool updateTracktionMacroParam) {
    auto& trackMgr = TrackManager::getInstance();
    const float value = juce::jlimit(0.0f, 1.0f, static_cast<float>(normalized));
    const auto path = target.devicePath.isValid()
                          ? target.devicePath
                          : ChainNodePath::trackLevel(target.devicePath.trackId);

    auto node = static_cast<const TrackManager&>(trackMgr).resolveChainNode(path);
    if (!node.valid())
        return;

    AutomationManager::AutomationWriteScope writeScope;
    trackMgr.setMacroValue(path, target.paramIndex, value);

    if (auto* macroParam = dynamic_cast<te::MacroParameter*>(
            bridge_.getPluginManager().findMacroParameterForAutomation(path.trackId, path,
                                                                       target.paramIndex))) {
        if (updateTracktionMacroParam) {
            macroWritebacksInProgress_.push_back(target);
            macroParam->setParameterFromHost(value, juce::sendNotificationSync);
            macroWritebacksInProgress_.pop_back();
        }

        auto position = edit_.getTransport().getPosition();
        for (auto param : te::getAllParametersBeingModifiedBy(edit_, *macroParam))
            if (param)
                param->updateFromAutomationSources(position);
    }
}

void AutomationPlaybackEngine::writeModRateFromCurve(const AutomationTarget& target,
                                                     double normalized) {
    ParameterInfo info = getParameterInfoForTarget(target);
    auto& trackMgr = TrackManager::getInstance();
    AutomationManager::AutomationWriteScope writeScope;

    // tempoSync flag drives both the lane's ParameterInfo (built above) and
    // the writeback target. Resolving the mod again here keeps the two in
    // lockstep without threading the flag through the call.
    auto* track = trackMgr.getTrack(target.devicePath.trackId);
    const ModInfo* mod = nullptr;
    if (track) {
        if (target.devicePath.isValid()) {
            auto resolved = trackMgr.resolvePath(target.devicePath);
            if (resolved.valid && resolved.rack) {
                for (const auto& m : resolved.rack->mods)
                    if (m.id == target.modId) {
                        mod = &m;
                        break;
                    }
            } else if (resolved.valid && resolved.device) {
                for (const auto& m : resolved.device->mods)
                    if (m.id == target.modId) {
                        mod = &m;
                        break;
                    }
            }
        }
        if (!mod) {
            for (const auto& m : track->mods)
                if (m.id == target.modId) {
                    mod = &m;
                    break;
                }
        }
    }
    const bool sync = mod && mod->tempoSync;

    if (sync) {
        // Lane stores 0-based display index — shift +1 to recover TE ordinal.
        float real = ParameterUtils::normalizedToReal(static_cast<float>(normalized), info);
        int ordinal = juce::jlimit(1, 23, static_cast<int>(std::round(real)) + 1);
        SyncDivision division = teRateOrdinalToSyncDivision(ordinal);
        if (target.devicePath.isValid()) {
            switch (target.devicePath.getType()) {
                case ChainNodeType::Rack:
                    trackMgr.setModSyncDivision(target.devicePath, target.modId, division);
                    return;
                case ChainNodeType::TopLevelDevice:
                case ChainNodeType::Device:
                    trackMgr.setModSyncDivision(target.devicePath, target.modId, division);
                    return;
                default:
                    break;
            }
        }
        trackMgr.setModSyncDivision(ChainNodePath::trackLevel(target.devicePath.trackId),
                                    target.modId, division);
        return;
    }

    float real = ParameterUtils::normalizedToReal(static_cast<float>(normalized), info);
    if (target.devicePath.isValid()) {
        switch (target.devicePath.getType()) {
            case ChainNodeType::Rack:
                trackMgr.setModRate(target.devicePath, target.modId, real);
                return;
            case ChainNodeType::TopLevelDevice:
            case ChainNodeType::Device:
                trackMgr.setModRate(target.devicePath, target.modId, real);
                return;
            default:
                break;
        }
    }
    trackMgr.setModRate(ChainNodePath::trackLevel(target.devicePath.trackId), target.modId, real);
}

}  // namespace magda
