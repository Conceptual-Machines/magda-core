#include "TempoLaneBridge.hpp"

#include <algorithm>

#include "../core/ControlTarget.hpp"
#include "../core/ParameterUtils.hpp"

namespace magda {

namespace te = tracktion;

namespace {
// TempoSetting and AutomationPoint share the [-1, +1] curve/tension convention,
// so the mapping is the identity (clamped for safety).
float tensionToCurve(double tension) {
    return juce::jlimit(-1.0f, 1.0f, static_cast<float>(tension));
}
double curveToTension(float curve) {
    return juce::jlimit(-1.0f, 1.0f, curve);
}
}  // namespace

double TempoLaneBridge::bpmToNormalized(double bpm) {
    const auto info = getParameterInfoForTarget(ControlTarget::tempo());
    return ParameterUtils::realToNormalized(static_cast<float>(bpm), info);
}

double TempoLaneBridge::normalizedToBpm(double normalized) {
    const auto info = getParameterInfoForTarget(ControlTarget::tempo());
    return ParameterUtils::normalizedToReal(static_cast<float>(normalized), info);
}

void TempoLaneBridge::writePointsToSequence(const std::vector<AutomationPoint>& pointsIn,
                                            te::Edit& edit) {
    if (pointsIn.empty())
        return;  // TE requires >=1 tempo; nothing to reconcile against.

    auto points = pointsIn;
    std::sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) {
        return a.beatPosition < b.beatPosition;
    });

    auto& ts = edit.tempoSequence;

    // Drop every tempo except index 0 (high -> low so indices stay valid).
    for (int i = ts.getNumTempos() - 1; i >= 1; --i)
        ts.removeTempo(i, false);

    // Anchor tempo[0] at beat 0 from the earliest point (a flat region before
    // the first explicit point is the correct musical reading).
    const auto& first = points.front();
    if (auto* t0 = ts.getTempo(0))
        t0->set(te::BeatPosition::fromBeats(0.0), normalizedToBpm(first.value),
                tensionToCurve(first.tension), false);

    // Insert the remaining points at their beats (skip any extra at/below 0,
    // already represented by tempo[0]).
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& p = points[i];
        if (p.beatPosition <= 0.0)
            continue;
        ts.insertTempo(te::BeatPosition::fromBeats(p.beatPosition), normalizedToBpm(p.value),
                       tensionToCurve(p.tension));
    }
}

std::vector<AutomationPoint> TempoLaneBridge::readPointsFromSequence(te::Edit& edit) {
    std::vector<AutomationPoint> points;
    auto& ts = edit.tempoSequence;
    for (int i = 0; i < ts.getNumTempos(); ++i) {
        if (auto* t = ts.getTempo(i)) {
            AutomationPoint p;
            p.beatPosition = t->getStartBeat().inBeats();
            p.value = bpmToNormalized(t->getBpm());
            p.curveType = AutomationCurveType::Linear;
            p.tension = curveToTension(t->getCurve());
            points.push_back(p);
        }
    }
    return points;
}

}  // namespace magda
