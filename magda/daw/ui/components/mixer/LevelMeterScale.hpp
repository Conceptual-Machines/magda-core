#pragma once

#include <cmath>

namespace magda::level_meter_scale {

static constexpr float minDb = -60.0f;
static constexpr float maxDb = 6.0f;
static constexpr float meterCurveExponent = 3.0f;

inline float gainToDb(float gain) {
    if (gain <= 0.0f)
        return minDb;
    return 20.0f * std::log10(gain);
}

inline float dbToMeterPos(float db) {
    if (db <= minDb)
        return 0.0f;
    if (db >= maxDb)
        return 1.0f;

    const float normalized = (db - minDb) / (maxDb - minDb);
    return std::pow(normalized, meterCurveExponent);
}

inline float meterPosToDb(float pos) {
    if (pos <= 0.0f)
        return minDb;
    if (pos >= 1.0f)
        return maxDb;

    const float normalized = std::pow(pos, 1.0f / meterCurveExponent);
    return minDb + normalized * (maxDb - minDb);
}

inline double dbFillProportion(double db) {
    return static_cast<double>(dbToMeterPos(static_cast<float>(db)));
}

}  // namespace magda::level_meter_scale
