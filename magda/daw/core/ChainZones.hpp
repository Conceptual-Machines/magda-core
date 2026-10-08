#pragma once

#include <algorithm>

namespace magda {

/**
 * @brief The notes, velocities and selector positions a chain plays at (#1808, #3007).
 *
 * Each range is inclusive. A fade ramps the chain's level in from its edge over
 * that many steps: a velocity scale on each note for key and velocity, a gain on
 * the chain's output for the rack's chain selector.
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
    /// Where on the rack's 0-127 chain selector this chain sounds.
    int selectorLow = 0;
    int selectorHigh = 127;
    int selectorFadeLow = 0;
    int selectorFadeHigh = 0;

    bool operator==(const ChainZones&) const = default;

    /// True when nothing is zoned at all.
    bool isOpen() const {
        return *this == ChainZones{};
    }

    /// True when every note at every velocity plays at full level.
    bool notesOpen() const {
        ChainZones notes = *this;
        notes.selectorLow = 0;
        notes.selectorHigh = 127;
        notes.selectorFadeLow = 0;
        notes.selectorFadeHigh = 0;
        return notes == ChainZones{};
    }

    /// True when the chain plays at full level wherever the selector is.
    bool selectorOpen() const {
        return selectorLow <= 0 && selectorHigh >= 127 && selectorFadeLow == 0 &&
               selectorFadeHigh == 0;
    }

    /// Ranges inside MIDI's span, high never below low, fades never past their span.
    ChainZones clamped() const;

    /// The level a note-on of @p note at @p velocity plays at: 0 outside the zones.
    float gainFor(int note, int velocity) const;

    /// The chain's output level with the rack's selector at @p selector (0-127).
    float selectorGain(float selector) const;

    /// The level at @p value inside an inclusive range faded in from both edges.
    static float edgeGain(float value, int low, int high, int fadeLow, int fadeHigh);
};

inline float ChainZones::edgeGain(float value, int low, int high, int fadeLow, int fadeHigh) {
    if (value < static_cast<float>(low) || value > static_cast<float>(high))
        return 0.0f;
    float gain = 1.0f;
    if (fadeLow > 0 && value < static_cast<float>(low + fadeLow))
        gain = std::min(gain, (value - static_cast<float>(low) + 1.0f) / (fadeLow + 1));
    if (fadeHigh > 0 && value > static_cast<float>(high - fadeHigh))
        gain = std::min(gain, (static_cast<float>(high) - value + 1.0f) / (fadeHigh + 1));
    return gain;
}

inline ChainZones ChainZones::clamped() const {
    const auto clamp = [](int value, int low, int high) {
        return std::clamp(value, low, std::max(low, high));
    };
    ChainZones z = *this;
    z.keyLow = clamp(keyLow, 0, 127);
    z.keyHigh = clamp(keyHigh, z.keyLow, 127);
    z.velocityLow = clamp(velocityLow, 1, 127);
    z.velocityHigh = clamp(velocityHigh, z.velocityLow, 127);
    z.selectorLow = clamp(selectorLow, 0, 127);
    z.selectorHigh = clamp(selectorHigh, z.selectorLow, 127);
    z.keyFadeLow = clamp(keyFadeLow, 0, z.keyHigh - z.keyLow);
    z.keyFadeHigh = clamp(keyFadeHigh, 0, z.keyHigh - z.keyLow);
    z.velocityFadeLow = clamp(velocityFadeLow, 0, z.velocityHigh - z.velocityLow);
    z.velocityFadeHigh = clamp(velocityFadeHigh, 0, z.velocityHigh - z.velocityLow);
    z.selectorFadeLow = clamp(selectorFadeLow, 0, z.selectorHigh - z.selectorLow);
    z.selectorFadeHigh = clamp(selectorFadeHigh, 0, z.selectorHigh - z.selectorLow);
    return z;
}

inline float ChainZones::gainFor(int note, int velocity) const {
    return edgeGain(static_cast<float>(note), keyLow, keyHigh, keyFadeLow, keyFadeHigh) *
           edgeGain(static_cast<float>(velocity), velocityLow, velocityHigh, velocityFadeLow,
                    velocityFadeHigh);
}

inline float ChainZones::selectorGain(float selector) const {
    return edgeGain(selector, selectorLow, selectorHigh, selectorFadeLow, selectorFadeHigh);
}

}  // namespace magda
