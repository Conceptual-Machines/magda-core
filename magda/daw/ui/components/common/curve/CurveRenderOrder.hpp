#pragma once

#include <algorithm>
#include <vector>

#include "CurveTypes.hpp"

namespace magda {

template <typename EffectiveX>
std::vector<const CurvePoint*> getCurveRenderOrder(const std::vector<CurvePoint>& points,
                                                   bool previewActive, EffectiveX&& effectiveX) {
    std::vector<const CurvePoint*> ordered;
    ordered.reserve(points.size());
    for (const auto& point : points)
        ordered.push_back(&point);

    if (previewActive) {
        std::stable_sort(ordered.begin(), ordered.end(),
                         [&effectiveX](const auto* a, const auto* b) {
                             return effectiveX(*a) < effectiveX(*b);
                         });
    }

    return ordered;
}

}  // namespace magda
