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
            const auto model = ParameterUtils::normalizedToModelValue(
                ParameterNormalizedValue::clamped(static_cast<float>(magdaNormalized)), info);
            return ParameterUtils::modelToTeValue(model, info);
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
    ParameterInfo info = target.kind == ControlTarget::Kind::PluginParam
                             ? getParameterInfoForTarget(target)
                             : ParameterInfo{};

    const bool isDeviceParam = target.kind == ControlTarget::Kind::PluginParam;
    if (isDeviceParam && info.teMaxValue <= info.teMinValue && param != nullptr) {
        const auto range = param->getValueRange();
        info.teMinValue = range.getStart();
        info.teMaxValue = range.getEnd();
    }

    return [target, param, info, isDeviceParam](double magdaNormalized) -> float {
        if (!isDeviceParam)
            return toParameterValue(target, param, info, magdaNormalized);
        const auto model = ParameterUtils::normalizedToModelValue(
            ParameterNormalizedValue::clamped(static_cast<float>(magdaNormalized)), info);
        return ParameterUtils::modelToTeValue(model, info);
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
