#pragma once

#include "magda/sdk/meter/MeterModel.hpp"

/// MAGDA's meter scale, the SDK default (docs/meter.md in magda-sdk).
namespace magda::level_meter_scale {

inline constexpr sdk::MeterScale scale{};

static constexpr float minDb = scale.minDb;
static constexpr float maxDb = scale.maxDb;
static constexpr float meterCurveExponent = scale.curveExponent;

inline float gainToDb(float gain) {
    return scale.gainToDb(gain);
}

inline float dbToMeterPos(float db) {
    return scale.dbToPosition(db);
}

inline float meterPosToDb(float pos) {
    return scale.positionToDb(pos);
}

inline double dbFillProportion(double db) {
    return static_cast<double>(dbToMeterPos(static_cast<float>(db)));
}

}  // namespace magda::level_meter_scale
