#pragma once

#include <algorithm>

namespace magda {

/**
 * @brief The notes and velocities a chain plays (#1808, #3007).
 *
 * Each range is inclusive. A fade ramps the chain's level in from its edge over
 * that many steps, which the engine applies as a velocity scale on each note.
 */
struct ChainZones {
    int keyLow = 0;
    int keyHigh = 127;
    int keyFadeLow = 0;
    int keyFadeHigh = 0;
    int velocityLow = 1;
    int velocityHigh = 127;
    int velocityFadeLow = 0;
    int velocityFadeHigh = 0;
    /// Takes turns with its siblings that also set it, one note-on each.
    bool roundRobin = false;

    bool operator==(const ChainZones&) const = default;

    /// True when every note at every velocity plays at full level.
    bool isOpen() const {
        return *this == ChainZones{};
    }

    /// The level a note-on of @p note at @p velocity plays at: 0 outside the zones.
    float gainFor(int note, int velocity) const;
};

inline float ChainZones::gainFor(int note, int velocity) const {
    const auto edge = [](int value, int low, int high, int fadeLow, int fadeHigh) {
        if (value < low || value > high)
            return 0.0f;
        float gain = 1.0f;
        if (fadeLow > 0 && value < low + fadeLow)
            gain = std::min(gain, static_cast<float>(value - low + 1) / (fadeLow + 1));
        if (fadeHigh > 0 && value > high - fadeHigh)
            gain = std::min(gain, static_cast<float>(high - value + 1) / (fadeHigh + 1));
        return gain;
    };
    return edge(note, keyLow, keyHigh, keyFadeLow, keyFadeHigh) *
           edge(velocity, velocityLow, velocityHigh, velocityFadeLow, velocityFadeHigh);
}

}  // namespace magda
