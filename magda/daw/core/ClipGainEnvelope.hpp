#pragma once

#include <vector>

#include "magda/sdk/curve/CurveTypes.hpp"

/**
 * @file ClipGainEnvelope.hpp
 * @brief An audio event's amplitude envelope, tied to its source (#3064).
 *
 * Points sit in forward source seconds, so they follow the sound through trim, slip, warp,
 * tempo and reverse, and repeat with each loop pass. Values are positions on a power-curve
 * dB scale, the same 0..1 the editors draw, so the curve interpolates the way it looks.
 */

namespace magda {

struct ClipGainEnvelope {
    /// beatPosition holds forward source seconds; value is a scale position (gain_envelope).
    std::vector<sdk::AutomationPoint> points;

    bool empty() const {
        return points.empty();
    }

    /// Shape, not identity: AutomationPoint's own == compares ids only.
    bool operator==(const ClipGainEnvelope& other) const;
};

namespace gain_envelope {

inline constexpr double kMinDb = -60.0;
inline constexpr double kMaxDb = 6.0;
inline constexpr double kCurveExponent = 3.0;

/// 0 is silence, 1 is kMaxDb.
double positionToDb(double position);
double dbToPosition(double db);
double positionToGain(double position);

/// Where 0 dB sits, about 0.75.
double unityPosition();

/// The gain at @p sourceSeconds; unity for an empty envelope.
double gainAt(const ClipGainEnvelope& envelope, double sourceSeconds);

}  // namespace gain_envelope

}  // namespace magda
