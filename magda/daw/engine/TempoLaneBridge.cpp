#include "TempoLaneBridge.hpp"

#include <algorithm>
#include <cmath>

#include "../core/TempoLane.hpp"

namespace magda {

namespace te = tracktion;

namespace {
constexpr double kEps = 1.0e-6;
}  // namespace

double TempoLaneBridge::bpmToNormalized(double bpm) {
    return tempo_lane::bpmToNormalized(bpm);
}

double TempoLaneBridge::normalizedToBpm(double normalized) {
    return tempo_lane::normalizedToBpm(normalized);
}

void TempoLaneBridge::writePointsToSequence(const std::vector<AutomationPoint>& pointsIn,
                                            te::Edit& edit) {
    if (pointsIn.empty())
        return;  // TE requires >=1 tempo; nothing to reconcile against.

    auto points = pointsIn;
    std::ranges::sort(points, {}, &AutomationPoint::beatPosition);

    auto& ts = edit.tempoSequence;

    // Clips and automation are beat-based (Clip::syncType defaults to
    // syncBarsBeats): TE keeps them anchored to their bar/beat position across a
    // tempo change, but ONLY when the change runs through its remapper. The
    // individual tempo mutators below pass remapEdit=false, so snapshot every
    // clip's beat position up front and remap once after the rebuild. Without
    // this, edits to the tempo automation leave clips pinned to stale wall-clock
    // seconds and they drift off the grid under a varying tempo.
    te::EditTimecodeRemapperSnapshot snap;
    snap.savePreChangeState(edit);

    // Drop every tempo except index 0 (high -> low so indices stay valid).
    for (int i = ts.getNumTempos() - 1; i >= 1; --i)
        ts.removeTempo(i, false);

    // Anchor tempo[0] at beat 0 from the earliest point (a flat region before
    // the first explicit point is the correct musical reading).
    const auto& first = points.front();
    if (auto* t0 = ts.getTempo(0))
        t0->set(te::BeatPosition::fromBeats(0.0), normalizedToBpm(first.value),
                tempo_lane::segmentTension(points, 0), false);

    // Insert the remaining points at their beats (skip any extra at/below 0,
    // already represented by tempo[0]).
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& p = points[i];
        if (p.beatPosition <= 0.0)
            continue;
        ts.insertTempo(te::BeatPosition::fromBeats(p.beatPosition), normalizedToBpm(p.value),
                       tempo_lane::segmentTension(points, i));
    }

    // Reposition every clip / automation point to its saved bar/beat under the
    // new tempo map. This is what makes clips follow the tempo curve.
    snap.remapEdit(edit);
}

std::vector<AutomationPoint> TempoLaneBridge::readPointsFromSequence(te::Edit& edit) {
    std::vector<AutomationPoint> points;
    std::vector<float> curves;
    auto& ts = edit.tempoSequence;
    for (int i = 0; i < ts.getNumTempos(); ++i) {
        if (auto* t = ts.getTempo(i)) {
            AutomationPoint p;
            p.beatPosition = t->getStartBeat().inBeats();
            p.value = bpmToNormalized(t->getBpm());
            p.curveType = AutomationCurveType::Linear;
            points.push_back(p);
            curves.push_back(t->getCurve());
        }
    }

    // Reconstruct a symmetric (midpoint) bezier apex per segment from its curve
    // factor, so the lane's shaper renders the same bend the engine plays.
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const double tension = juce::jlimit(-1.0f, 1.0f, curves[i]);
        const double dy = points[i + 1].value - points[i].value;
        if (std::abs(tension) <= kEps || std::abs(dy) <= kEps)
            continue;
        const double curvedT = tempo_lane::curvedTFromTension(tension);
        const double apexValue = points[i].value + curvedT * dy;
        const double apexBeat = (points[i].beatPosition + points[i + 1].beatPosition) * 0.5;
        points[i].outHandle = {apexBeat - points[i].beatPosition, apexValue - points[i].value,
                               true};
        points[i + 1].inHandle = {apexBeat - points[i + 1].beatPosition,
                                  apexValue - points[i + 1].value, true};
    }
    return points;
}

}  // namespace magda
