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
        const auto xOf = [&effectiveX](const CurvePoint* point) { return effectiveX(*point); };
        std::ranges::stable_sort(ordered, {}, xOf);
    }

    return ordered;
}

}  // namespace magda
