#pragma once

#include <algorithm>
#include <set>
#include <vector>

namespace magda {

/**
 * @brief One comp region in a neutral domain (seconds for audio, beats for MIDI).
 *
 * The audio CompSection (seconds) and MidiCompSection (beats) both map to this
 * for the shared section-assignment algorithm below.
 */
struct CompSpan {
    double start = 0.0;
    double end = 0.0;
    int takeIndex = 0;
};

/**
 * @brief Rebuild the comp section list so [a, b) plays `take`, splitting/merging
 * as needed. Sections stay sorted, contiguous and cover [0, len). Domain-neutral
 * (works for seconds or beats); `baseTake` fills ranges not covered by `existing`.
 */
inline std::vector<CompSpan> assignCompSections(const std::vector<CompSpan>& existing, double len,
                                                int baseTake, double a, double b, int take) {
    a = std::clamp(a, 0.0, len);
    b = std::clamp(b, 0.0, len);
    if (b <= a)
        return existing;

    auto takeAtOld = [&](double mid) -> int {
        for (const auto& s : existing)
            if (mid >= s.start && mid < s.end)
                return s.takeIndex;
        return baseTake;
    };

    std::set<double> boundarySet{0.0, len, a, b};
    for (const auto& s : existing) {
        boundarySet.insert(s.start);
        boundarySet.insert(s.end);
    }
    std::vector<double> bounds(boundarySet.begin(), boundarySet.end());

    std::vector<CompSpan> result;
    for (size_t i = 0; i + 1 < bounds.size(); ++i) {
        double x = bounds[i];
        double y = bounds[i + 1];
        if (y - x < 1.0e-6)
            continue;
        double mid = 0.5 * (x + y);
        int t = (x >= a - 1.0e-9 && y <= b + 1.0e-9) ? take : takeAtOld(mid);
        if (!result.empty() && result.back().takeIndex == t)
            result.back().end = y;
        else
            result.push_back({x, y, t});
    }
    return result;
}

/**
 * @brief Remap comp sections after take `deleted` is removed: sections on the
 * deleted take fall back to `fallbackTake`, higher take indices shift down by
 * one, then adjacent same-take sections merge so the tiling stays contiguous.
 */
inline void remapCompSpansAfterDelete(std::vector<CompSpan>& comp, int deleted, int fallbackTake) {
    for (auto& s : comp) {
        if (s.takeIndex == deleted)
            s.takeIndex = fallbackTake;
        else if (s.takeIndex > deleted)
            s.takeIndex -= 1;
    }
    std::vector<CompSpan> merged;
    for (const auto& s : comp) {
        if (!merged.empty() && merged.back().takeIndex == s.takeIndex &&
            merged.back().end >= s.start - 1.0e-9)
            merged.back().end = s.end;
        else
            merged.push_back(s);
    }
    comp.swap(merged);
}

}  // namespace magda
