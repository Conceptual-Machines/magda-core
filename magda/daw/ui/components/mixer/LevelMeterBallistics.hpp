#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace magda::level_meter_ballistics {

static constexpr float nominalFrameMs = 1000.0f / 60.0f;
static constexpr float attackCoeffAt60Hz = 0.9f;
static constexpr float releaseCoeffAt60Hz = 0.05f;
static constexpr float peakHoldMs = 1500.0f;
static constexpr float peakDecayDbPerFrameAt60Hz = 0.8f;
static constexpr float peakDecayDbPerMs = peakDecayDbPerFrameAt60Hz / nominalFrameMs;

inline double restartClock() {
    return juce::Time::getMillisecondCounterHiRes();
}

inline float getElapsedMs(double& lastUpdateMs) {
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsedMs =
        lastUpdateMs > 0.0 ? static_cast<float>(now - lastUpdateMs) : nominalFrameMs;
    lastUpdateMs = now;
    return juce::jmax(0.0f, elapsedMs);
}

inline float coefficientForElapsedMs(float coefficientAt60Hz, float elapsedMs) {
    return 1.0f - std::pow(1.0f - coefficientAt60Hz, elapsedMs / nominalFrameMs);
}

inline bool updateLevel(float& display, float target, float elapsedMs) {
    const float prev = display;
    const float coeff = coefficientForElapsedMs(
        target > display ? attackCoeffAt60Hz : releaseCoeffAt60Hz, elapsedMs);
    display += (target - display) * coeff;
    if (display < 0.001f)
        display = 0.0f;
    return std::abs(display - prev) > 0.0001f;
}

inline bool updatePeak(float& peakDb, float& holdTimeMs, float currentDb, float minDb,
                       float elapsedMs) {
    const float prev = peakDb;
    if (currentDb > peakDb) {
        peakDb = currentDb;
        holdTimeMs = peakHoldMs;
    } else if (holdTimeMs > 0.0f) {
        holdTimeMs = juce::jmax(0.0f, holdTimeMs - elapsedMs);
    } else {
        peakDb = juce::jmax(minDb, peakDb - peakDecayDbPerMs * elapsedMs);
    }
    return std::abs(peakDb - prev) > 0.01f;
}

}  // namespace magda::level_meter_ballistics
