#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace magda {

/** @brief A Linkwitz-Riley crossover's slope. */
enum class CrossoverSlope : std::uint8_t { Db12, Db24, Db48 };

/** @brief One split point of a multiband rack, between the band below it and the band above. */
struct Crossover {
    float frequencyHz = 1000.0f;
    CrossoverSlope slope = CrossoverSlope::Db24;

    bool operator==(const Crossover&) const = default;
};

/// Eight bands at most, which bounds the executor's filter state.
inline constexpr int kMaxCrossovers = 7;
inline constexpr float kMinCrossoverHz = 20.0f;
inline constexpr float kMaxCrossoverHz = 20000.0f;
/// The narrowest band a crossover may leave its neighbours, as a frequency ratio.
inline constexpr float kMinBandRatio = 1.05f;

inline int slopeDbPerOctave(CrossoverSlope slope) {
    switch (slope) {
        case CrossoverSlope::Db12:
            return 12;
        case CrossoverSlope::Db48:
            return 48;
        case CrossoverSlope::Db24:
            break;
    }
    return 24;
}

/** @brief @p hz clamped to what crossover @p index may take between its neighbours. */
inline float clampCrossoverFrequency(std::span<const Crossover> crossovers, std::size_t index,
                                     float hz) {
    const float low =
        index > 0 ? crossovers[index - 1].frequencyHz * kMinBandRatio : kMinCrossoverHz;
    const float high = index + 1 < crossovers.size()
                           ? crossovers[index + 1].frequencyHz / kMinBandRatio
                           : kMaxCrossoverHz;
    return std::fmin(std::fmax(hz, low), std::fmax(low, high));
}

/** @brief The crossover a split adds inside band @p band: the band's geometric centre. */
inline float bandSplitFrequency(std::span<const Crossover> crossovers, std::size_t band) {
    const float low = band > 0 ? crossovers[band - 1].frequencyHz : kMinCrossoverHz;
    const float high = band < crossovers.size() ? crossovers[band].frequencyHz : kMaxCrossoverHz;
    return std::sqrt(low * high);
}

/** @brief Whether band @p band is wide enough to split. */
inline bool canSplitBand(std::span<const Crossover> crossovers, std::size_t band) {
    if (static_cast<int>(crossovers.size()) >= kMaxCrossovers)
        return false;
    const float low = band > 0 ? crossovers[band - 1].frequencyHz : kMinCrossoverHz;
    const float high = band < crossovers.size() ? crossovers[band].frequencyHz : kMaxCrossoverHz;
    return high / low >= kMinBandRatio * kMinBandRatio;
}

/** @brief The crossovers a new multiband rack starts with: low, mid and high. */
inline std::vector<Crossover> defaultCrossovers() {
    return {{180.0f, CrossoverSlope::Db24}, {3200.0f, CrossoverSlope::Db24}};
}

}  // namespace magda
