#include "clip/WarpMap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace magda::engine {

namespace {

/// Interpolate between two points, on whichever side @p from names.
///
/// One function for both directions because the two are the same arithmetic
/// over a pair that is sorted in both coordinates, and writing it twice is how
/// the forward and the inverse come to disagree by a rounding.
double between(double value, double fromLow, double fromHigh, double toLow, double toHigh) {
    const auto span = fromHigh - fromLow;
    if (!(span > 0.0))
        return toLow;

    return toLow + ((value - fromLow) / span) * (toHigh - toLow);
}

}  // namespace

double WarpMap::sourceSecondsAt(double warpSeconds) const noexcept {
    if (points.empty())
        return warpSeconds;

    const auto& first = points.front();
    if (warpSeconds <= first.warpSeconds)
        return first.sourceSeconds + (warpSeconds - first.warpSeconds);

    const auto& last = points.back();
    if (warpSeconds >= last.warpSeconds)
        return last.sourceSeconds + (warpSeconds - last.warpSeconds);

    const auto above = std::upper_bound(
        points.begin(), points.end(), warpSeconds,
        [](double value, const Point& point) { return value < point.warpSeconds; });
    const auto below = above - 1;

    return between(warpSeconds, below->warpSeconds, above->warpSeconds, below->sourceSeconds,
                   above->sourceSeconds);
}

double WarpMap::sourceToWarpSeconds(double sourceSeconds) const noexcept {
    if (points.empty())
        return sourceSeconds;

    const auto& first = points.front();
    if (sourceSeconds <= first.sourceSeconds)
        return first.warpSeconds + (sourceSeconds - first.sourceSeconds);

    const auto& last = points.back();
    if (sourceSeconds >= last.sourceSeconds)
        return last.warpSeconds + (sourceSeconds - last.sourceSeconds);

    const auto above = std::upper_bound(
        points.begin(), points.end(), sourceSeconds,
        [](double value, const Point& point) { return value < point.sourceSeconds; });
    const auto below = above - 1;

    return between(sourceSeconds, below->sourceSeconds, above->sourceSeconds, below->warpSeconds,
                   above->warpSeconds);
}

double WarpMap::maxSourcePerWarp() const noexcept {
    // One, not zero: outside the markers the map runs at slope 1, and every map
    // has an outside.
    double steepest = 1.0;

    for (std::size_t i = 1; i < points.size(); ++i) {
        const auto sourceSpan = points[i].sourceSeconds - points[i - 1].sourceSeconds;
        const auto warpSpan = points[i].warpSeconds - points[i - 1].warpSeconds;

        if (warpSpan > 0.0)
            steepest = std::max(steepest, sourceSpan / warpSpan);
    }

    return steepest;
}

WarpCompileResult compileWarpMap(const std::vector<WarpMarker>& markers) {
    WarpCompileResult result;

    std::vector<WarpMap::Point> sorted;
    sorted.reserve(markers.size());

    for (const auto& marker : markers) {
        if (std::isfinite(marker.sourceTime) && std::isfinite(marker.warpTime))
            sorted.push_back({marker.sourceTime, marker.warpTime});
        else
            ++result.droppedMarkers;
    }

    // By source time, because that is the side the model's own map is a
    // function of. Storage order is not guaranteed either way
    // (AudioEvent::warpedSourceSeconds says so), so this is a sort rather than
    // a check.
    std::sort(sorted.begin(), sorted.end(), [](const WarpMap::Point& a, const WarpMap::Point& b) {
        return a.sourceSeconds < b.sourceSeconds;
    });

    // Coincident on the source side first. Two markers on one instant of the
    // file are a segment with no span, which is an infinite rate; the earlier
    // one is kept so that which survives does not depend on the order they were
    // stored in.
    std::vector<WarpMap::Point> distinct;
    distinct.reserve(sorted.size());

    for (const auto& point : sorted) {
        if (!distinct.empty() && point.sourceSeconds <= distinct.back().sourceSeconds) {
            ++result.droppedMarkers;
            continue;
        }

        distinct.push_back(point);
    }

    // Then the largest set of them that runs forwards on the warp side too.
    //
    // Largest, rather than whatever a single forward pass happens to keep. A
    // greedy pass anchors on whatever it saw first, so one marker dragged far
    // ahead of its neighbours survives and every marker it cleared is dropped
    // behind it: markers at warp 0, 100, 1, 2, 3 would cost four of the five
    // when dropping the one at 100 leaves a perfectly good map. What an
    // offending marker costs should be itself, and that is a longest increasing
    // subsequence.
    //
    // Deterministic: ties resolve towards the subsequence with the smallest
    // tail, so two compiles of one marker list keep the same markers.
    constexpr auto kNone = std::numeric_limits<std::size_t>::max();

    std::vector<std::size_t> tails;  // tails[k]: index ending the best run of length k+1
    std::vector<std::size_t> previous(distinct.size(), kNone);

    for (std::size_t i = 0; i < distinct.size(); ++i) {
        const auto at = std::lower_bound(tails.begin(), tails.end(), distinct[i].warpSeconds,
                                         [&](std::size_t candidate, double value) {
                                             return distinct[candidate].warpSeconds < value;
                                         });

        const auto length = static_cast<std::size_t>(at - tails.begin());

        if (length > 0)
            previous[i] = tails[length - 1];

        if (at == tails.end())
            tails.push_back(i);
        else
            *at = i;
    }

    auto& points = result.map.points;
    points.resize(tails.size());

    for (auto index = tails.empty() ? kNone : tails.back(), slot = tails.size(); index != kNone;
         index = previous[index])
        points[--slot] = distinct[index];

    result.droppedMarkers += static_cast<int>(distinct.size() - points.size());

    return result;
}

}  // namespace magda::engine
