#pragma once

#include <cmath>

namespace magda {

static constexpr double DEFAULT_BPM = 120.0;
static constexpr double MIN_VALID_BPM = 20.0;
static constexpr double MAX_VALID_BPM = 999.0;

static constexpr int DEFAULT_TIME_SIGNATURE_NUMERATOR = 4;
static constexpr int DEFAULT_TIME_SIGNATURE_DENOMINATOR = 4;
static constexpr int MIN_TIME_SIGNATURE_VALUE = 1;
static constexpr int MAX_TIME_SIGNATURE_VALUE = 16;

inline bool isValidBpm(double bpm) {
    return std::isfinite(bpm) && bpm >= MIN_VALID_BPM && bpm <= MAX_VALID_BPM;
}

inline double clampBpm(double bpm) {
    if (!std::isfinite(bpm))
        return DEFAULT_BPM;
    if (bpm < MIN_VALID_BPM)
        return MIN_VALID_BPM;
    if (bpm > MAX_VALID_BPM)
        return MAX_VALID_BPM;
    return bpm;
}

inline int clampTimeSignatureValue(int value) {
    if (value < MIN_TIME_SIGNATURE_VALUE)
        return MIN_TIME_SIGNATURE_VALUE;
    if (value > MAX_TIME_SIGNATURE_VALUE)
        return MAX_TIME_SIGNATURE_VALUE;
    return value;
}

/// Beats a file of @p seconds holds at @p bpm. A loop exported at a tempo
/// lands within a few samples of a whole beat; snapping there makes its cycle
/// exact instead of 15.996. Anything further off is not a loop and keeps the
/// fraction. Callers pass the result to adoption; the event never infers.
inline double beatCountForDuration(double seconds, double bpm) {
    if (seconds <= 0.0 || bpm <= 0.0)
        return 0.0;
    const double beats = seconds * bpm / 60.0;
    const double whole = std::round(beats);
    return whole > 0.0 && std::abs(beats - whole) < 0.02 ? whole : beats;
}

}  // namespace magda
