#include "ClipGainEnvelope.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "magda/sdk/curve/AutomationCurve.hpp"

namespace magda {

bool ClipGainEnvelope::operator==(const ClipGainEnvelope& other) const {
    return std::equal(points.begin(), points.end(), other.points.begin(), other.points.end(),
                      [](const sdk::AutomationPoint& a, const sdk::AutomationPoint& b) {
                          return a.id == b.id && a.beatPosition == b.beatPosition &&
                                 a.value == b.value && a.curveType == b.curveType &&
                                 a.inHandle == b.inHandle && a.outHandle == b.outHandle &&
                                 a.tension == b.tension;
                      });
}

namespace gain_envelope {

double positionToDb(double position) {
    if (!(position > 0.0))
        return -std::numeric_limits<double>::infinity();
    const auto clamped = std::min(position, 1.0);
    return kMinDb + std::pow(clamped, 1.0 / kCurveExponent) * (kMaxDb - kMinDb);
}

double dbToPosition(double db) {
    if (!(db > kMinDb))
        return 0.0;
    if (db >= kMaxDb)
        return 1.0;
    return std::pow((db - kMinDb) / (kMaxDb - kMinDb), kCurveExponent);
}

double positionToGain(double position) {
    if (!(position > 0.0))
        return 0.0;
    return std::pow(10.0, positionToDb(position) / 20.0);
}

double unityPosition() {
    return dbToPosition(0.0);
}

double gainAt(const ClipGainEnvelope& envelope, double sourceSeconds) {
    if (envelope.empty())
        return 1.0;
    return positionToGain(sdk::automation::valueAtBeat(envelope.points, sourceSeconds));
}

}  // namespace gain_envelope

}  // namespace magda
