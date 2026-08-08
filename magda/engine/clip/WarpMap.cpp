#include "clip/WarpMap.hpp"

#include <algorithm>
#include <cmath>

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

    auto& points = result.map.points;
    points.reserve(sorted.size());

    for (const auto& point : sorted) {
        // Strictly increasing on both sides. Equal on either is a segment with
        // no span, which is an infinite rate one way and a zero-divide the
        // other; decreasing is a map with no inverse. Both are the user having
        // dragged a marker past its neighbour, and both cost that marker.
        if (!points.empty() && (point.sourceSeconds <= points.back().sourceSeconds ||
                                point.warpSeconds <= points.back().warpSeconds)) {
            ++result.droppedMarkers;
            continue;
        }

        points.push_back(point);
    }

    return result;
}

}  // namespace magda::engine
