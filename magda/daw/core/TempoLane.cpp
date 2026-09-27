#include "TempoLane.hpp"

#include <juce_core/juce_core.h>

#include <cmath>

#include "ControlTarget.hpp"
#include "ParameterUtils.hpp"

namespace magda::tempo_lane {
namespace {

constexpr double kEps = 1.0e-6;

// Inverse of curvedTFromTension.
double tensionFromCurvedT(double curvedT) {
    curvedT = juce::jlimit(1.0e-4, 1.0 - 1.0e-4, curvedT);
    const double invLog2 = 1.0 / std::log(0.5);
    if (curvedT <= 0.5)
        return juce::jlimit(-1.0, 1.0, ((std::log(curvedT) * invLog2) - 1.0) * 0.5);
    return juce::jlimit(-1.0, 1.0, (1.0 - std::log(1.0 - curvedT) * invLog2) * 0.5);
}

}  // namespace

double bpmToNormalized(double bpm) {
    const auto info = getParameterInfoForTarget(ControlTarget::tempo());
    return ParameterUtils::realToNormalized(static_cast<float>(bpm), info);
}

double normalizedToBpm(double normalized) {
    const auto info = getParameterInfoForTarget(ControlTarget::tempo());
    return ParameterUtils::normalizedToReal(static_cast<float>(normalized), info);
}

double curvedTFromTension(double tension) {
    constexpr double t = 0.5;
    if (tension > 0.0)
        return std::pow(t, 1.0 + tension * 2.0);
    return 1.0 - std::pow(1.0 - t, 1.0 - tension * 2.0);
}

float segmentTension(const std::vector<AutomationPoint>& points, std::size_t i) {
    if (i + 1 >= points.size())
        return 0.0f;
    const auto& p1 = points[i];
    const auto& p2 = points[i + 1];
    const double dy = p2.value - p1.value;
    const bool hasHandle =
        std::abs(p1.outHandle.value) > kEps || std::abs(p1.outHandle.beatOffset) > kEps;
    if (std::abs(dy) <= kEps || !hasHandle)
        return juce::jlimit(-1.0f, 1.0f, static_cast<float>(p1.tension));
    const double curvedT = juce::jlimit(kEps, 1.0 - kEps, p1.outHandle.value / dy);
    return juce::jlimit(-1.0f, 1.0f, static_cast<float>(tensionFromCurvedT(curvedT)));
}

}  // namespace magda::tempo_lane
