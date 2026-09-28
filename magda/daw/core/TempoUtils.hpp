#pragma once

#include <algorithm>
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

/// A beat is a quarter note; the signature's beat is its denominator's note: 0.5 in 6/8.
inline double signatureBeatLength(int denominator) {
    return 4.0 / static_cast<double>(clampTimeSignatureValue(denominator));
}

/// Quarter-note beats in one bar: 4 in 4/4, 3 in 6/8, 3.5 in 7/8.
inline double beatsPerBar(int numerator, int denominator) {
    return static_cast<double>(clampTimeSignatureValue(numerator)) *
           signatureBeatLength(denominator);
}

static constexpr int TICKS_PER_SIGNATURE_BEAT = 480;

/// Zero-based bars, signature beats and ticks of a signature beat.
struct BarsBeatsTicks {
    int bars = 0;
    int beats = 0;
    int ticks = 0;
};

inline BarsBeatsTicks toBarsBeatsTicks(double beats, int numerator, int denominator) {
    const long long barTicks =
        static_cast<long long>(clampTimeSignatureValue(numerator)) * TICKS_PER_SIGNATURE_BEAT;
    const long long totalTicks = std::llround(
        std::max(beats, 0.0) / signatureBeatLength(denominator) * TICKS_PER_SIGNATURE_BEAT);
    const long long inBar = totalTicks % barTicks;
    return {static_cast<int>(totalTicks / barTicks),
            static_cast<int>(inBar / TICKS_PER_SIGNATURE_BEAT),
            static_cast<int>(inBar % TICKS_PER_SIGNATURE_BEAT)};
}

inline double fromBarsBeatsTicks(BarsBeatsTicks position, int numerator, int denominator) {
    return position.bars * beatsPerBar(numerator, denominator) +
           (position.beats + position.ticks / static_cast<double>(TICKS_PER_SIGNATURE_BEAT)) *
               signatureBeatLength(denominator);
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
