#include "automation/AutomationBake.hpp"

#include <cmath>
#include <vector>

#include "../../core/AutomationInfo.hpp"
#include "../../core/ClipLaneFlattener.hpp"
#include "../../core/ParameterInfo.hpp"
#include "../../core/ParameterUtils.hpp"

namespace magda {

namespace {

/// How far ahead of a step's edge the holding point goes, in beats.
constexpr double kStepEpsilon = 0.0001;

/// How many pieces a curved segment is cut into. Coarse on purpose: each point
/// is a ValueTree mutation with a listener fan-out behind it.
constexpr int kBezierSegments = 12;

/// One point's worth of the conversion, for a target whose ParameterInfo has
/// already been resolved. Split out only so the two callers below share it;
/// nothing outside this file has the info to hand.
float toParameterValue(const AutomationTarget& target, te::AutomatableParameter* param,
                       const ParameterInfo& info, double magdaNormalized) {
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
            //
            // Compiled/internal MAGDA plugins register their TE param on a 0..1
            // native range with the display mapping (e.g. gain dB, xover Hz)
            // layered on top via the param's scale. For these the lane's
            // MAGDA-normalized value already IS the native 0..1 position, so
            // pass it straight through. Routing it via normalizedToModelValue
            // would emit the DISPLAY value (e.g. 0 dB) into the 0..1 curve,
            // collapsing it to native 0.0 (-inf) during playback.
            if (ParameterUtils::isDisplayMappedInternalValue(info))
                return juce::jlimit(0.0f, 1.0f, static_cast<float>(magdaNormalized));
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

}  // namespace

std::function<float(double)> makeParameterValueConverter(const AutomationTarget& target,
                                                         te::AutomatableParameter* param) {
    // Resolved once. getParameterInfoForTarget walks the track, rack and chain
    // tree to find the device and copies a full ParameterInfo, value table,
    // choices and shared pointers included; a lane can bake a hundred thousand
    // points, and doing this inside that loop is what used to beach-ball play
    // and stop on any edit with automation on a plugin parameter.
    const ParameterInfo info = target.kind == ControlTarget::Kind::PluginParam
                                   ? getParameterInfoForTarget(target)
                                   : ParameterInfo{};

    const bool isDeviceParam = target.kind == ControlTarget::Kind::PluginParam;
    const float teMin = info.teMinValue;
    const float teSpan = info.teMaxValue - info.teMinValue;

    const bool useTeRange = isDeviceParam && teSpan > 0.0f &&
                            !ParameterUtils::infoMatchesTeRange(info) &&
                            !ParameterUtils::isDisplayMappedInternalValue(info);
    const bool displayMapped = isDeviceParam && ParameterUtils::isDisplayMappedInternalValue(info);

    return [target, param, info, isDeviceParam, teMin, teSpan, useTeRange,
            displayMapped](double magdaNormalized) -> float {
        if (useTeRange)
            return teMin + static_cast<float>(magdaNormalized) * teSpan;
        if (displayMapped)
            return juce::jlimit(0.0f, 1.0f, static_cast<float>(magdaNormalized));
        if (!isDeviceParam)
            return toParameterValue(target, param, info, magdaNormalized);
        return ParameterUtils::normalizedToModelValue(
                   ParameterNormalizedValue::clamped(static_cast<float>(magdaNormalized)), info)
            .value;
    };
}

void bakeLaneIntoCurve(te::AutomationCurve& curve, const AutomationLaneInfo& lane,
                       const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
                       const std::function<double(double)>& valueAtBeat,
                       const std::function<float(double)>& toParameterValue) {
    // One Tracktion point per MAGDA point, plus whatever a shape needs on top.
    // A dense resampling would be thousands of mutations describing a line.
    const std::vector<AutomationPoint>* sourcePoints = nullptr;
    std::vector<AutomationPoint> clipFlattened;

    if (lane.isAbsolute()) {
        sourcePoints = &lane.absolutePoints;
    } else if (lane.isClipBased()) {
        // Unrolled into an absolute-style breakpoint list, loop iterations
        // and gap holds included, so the loop below treats it as one.
        clipFlattened = flattenClipLane(lane, getClip, valueAtBeat);
        sourcePoints = &clipFlattened;
    }

    if (sourcePoints == nullptr || sourcePoints->empty())
        return;

    const auto addPoint = [&](double beat, double normalized) {
        // Beats, so a tempo edit moves the curve with the grid.
        curve.addPoint(te::EditPosition{te::BeatPosition::fromBeats(beat)},
                       toParameterValue(normalized), 0.0f, nullptr);
    };

    for (std::size_t i = 0; i < sourcePoints->size(); ++i) {
        const auto& point = (*sourcePoints)[i];

        // A step holds right up to this point and then jumps; without the
        // holding point the linear iterator ramps between the two.
        if (i > 0 && (*sourcePoints)[i - 1].curveType == AutomationCurveType::Step) {
            const auto preStepBeat = point.beatPosition - kStepEpsilon;
            if (preStepBeat > (*sourcePoints)[i - 1].beatPosition)
                addPoint(preStepBeat, valueAtBeat(preStepBeat));
        }

        // Anything not a straight line between its endpoints is cut into
        // pieces: bezier always, a Linear segment whenever tension or a handle
        // bends it, and a hard corner.
        if (i > 0) {
            const auto& previous = (*sourcePoints)[i - 1];
            const auto bezier = previous.curveType == AutomationCurveType::Bezier;
            const auto shaped = !previous.outHandle.isZero() || !point.inHandle.isZero();
            const auto bentLinear = previous.curveType == AutomationCurveType::Linear &&
                                    (std::abs(previous.tension) >= 0.001 || shaped);
            const auto hardCorner = previous.curveType == AutomationCurveType::HardCorner;

            if (bezier || bentLinear || hardCorner) {
                const auto span = point.beatPosition - previous.beatPosition;
                if (span > 0.0) {
                    for (auto segment = 1; segment < kBezierSegments; ++segment) {
                        const auto position =
                            static_cast<double>(segment) / static_cast<double>(kBezierSegments);
                        const auto beat = previous.beatPosition + (span * position);
                        addPoint(beat, valueAtBeat(beat));
                    }
                }
            }
        }

        addPoint(point.beatPosition, point.value);
    }
}

}  // namespace magda
