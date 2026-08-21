#include "automation/AutomationBake.hpp"

#include <cmath>
#include <vector>

#include "../../core/ClipLaneFlattener.hpp"

namespace magda {

namespace {

/// How far ahead of a step's edge the holding point goes, in beats. Small
/// enough to be inaudible at any tempo, large enough that Tracktion keeps it as
/// a separate point rather than folding it into the edge.
constexpr double kStepEpsilon = 0.0001;

/// How many pieces a curved segment is cut into. Coarse on purpose: the
/// iterator is linear, and each point is a ValueTree mutation with a listener
/// fan-out behind it, so a fine tessellation is paid for on the message thread
/// every time a point moves.
constexpr int kBezierSegments = 12;

}  // namespace

void bakeLaneIntoCurve(te::AutomationCurve& curve, const AutomationLaneInfo& lane,
                       const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
                       const std::function<double(double)>& valueAtBeat,
                       const std::function<float(double)>& toParameterValue) {
    // One Tracktion point per MAGDA point, plus whatever a shape needs on top.
    // Tracktion interpolates linearly between its own points, which is what a
    // Linear segment already is, so a dense resampling of every lane would be
    // thousands of ValueTree mutations describing a line.
    const std::vector<AutomationPoint>* sourcePoints = nullptr;
    std::vector<AutomationPoint> clipFlattened;

    if (lane.isAbsolute()) {
        sourcePoints = &lane.absolutePoints;
    } else if (lane.isClipBased()) {
        // A clip-based lane unrolled into an absolute-style breakpoint list --
        // loop iterations, gap holds and wrap jumps included -- so the loop
        // below treats it exactly like an absolute lane.
        clipFlattened = flattenClipLane(lane, getClip, valueAtBeat);
        sourcePoints = &clipFlattened;
    }

    if (sourcePoints == nullptr || sourcePoints->empty())
        return;

    const auto addPoint = [&](double beat, double normalized) {
        // Stored as beats, so a tempo edit moves the curve with the grid rather
        // than leaving it pinned at the seconds it was baked at.
        curve.addPoint(te::EditPosition{te::BeatPosition::fromBeats(beat)},
                       toParameterValue(normalized), 0.0f, nullptr);
    };

    for (std::size_t i = 0; i < sourcePoints->size(); ++i) {
        const auto& point = (*sourcePoints)[i];

        // A step holds the previous value right up to this point and then
        // jumps. Without the holding point the linear iterator ramps between
        // the two and lets every value in between through.
        if (i > 0 && (*sourcePoints)[i - 1].curveType == AutomationCurveType::Step) {
            const auto preStepBeat = point.beatPosition - kStepEpsilon;
            if (preStepBeat > (*sourcePoints)[i - 1].beatPosition)
                addPoint(preStepBeat, valueAtBeat(preStepBeat));
        }

        // Anything that is not a straight line between its endpoints has to be
        // cut into pieces, because a linear iterator handed two endpoints plays
        // a straight line whatever the user drew. Bezier always; a Linear
        // segment whenever tension or a handle bends it; a hard corner because
        // its shape is not its endpoints either.
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
