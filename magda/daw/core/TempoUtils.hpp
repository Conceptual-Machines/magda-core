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

}  // namespace magda
