#pragma once

#include <vector>

namespace magda {

/**
 * @brief Ramer–Douglas–Peucker curve simplification for automation points.
 *
 * Reduces a recorded automation polyline to the minimum set of points that
 * preserves its shape within a tolerance. Operates on (time, value) pairs
 * where value is normalized [0, 1]. Time units are arbitrary (beats).
 *
 * The "distance" metric is vertical distance from the candidate point to
 * the straight line between the range endpoints — i.e. how much the linear
 * interpolation would deviate from the actual recorded value at that time.
 * This is what matters for automation fidelity.
 *
 * First and last points are always kept.
 */
class AutomationCurveSimplifier {
  public:
    struct Point {
        double time;
        double value;
    };

    /**
     * @brief Return the indices of points to keep (sorted ascending).
     *
     * @param points   Input points, already sorted by time.
     * @param epsilon  Tolerance in value-axis units (normalized). Points within
     *                 epsilon of the interpolated line are dropped. Typical
     *                 range: 0.005–0.02 (0.5%–2% of parameter range).
     * @return Indices into `points` that should be kept. Always includes 0
     *         and points.size()-1 if there are >= 2 points.
     */
    static std::vector<size_t> simplify(const std::vector<Point>& points, double epsilon);
};

}  // namespace magda
