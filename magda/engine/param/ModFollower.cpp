#include "param/ModFollower.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::engine {

namespace {

/**
 * @brief The fork's time constant, which is the digital one.
 *
 * log(1%): a stage is "arrived" when it is within a hundredth of its
 * destination, and the coefficient is what that means per sample. The fork
 * offers three of these and MAGDA selects none of them, so this is the default
 * it therefore always runs at (EnvelopeFollowerModifier's digitalTC).
 */
constexpr float kTimeConstant = -2.0f;

/// A time of zero would divide the constant by nothing. Well under a sample at
/// any rate this engine runs at, so it is a guard rather than a range: the
/// model's own floor for attack and release is a millisecond.
constexpr float kMinTimeMs = 1.0e-3f;

/// How far a coefficient may be from its neighbour before it is worth
/// recomputing. The cutoffs arrive as floats off the model and comparing them
/// exactly is what the fork does; matched, so a block that changes nothing
/// recomputes nothing in either engine.
bool cutoffMoved(float current, float wanted) {
    return current != wanted;
}

/// The per-sample one-pole coefficient for a time in milliseconds.
float coefficientFor(float timeMs, double sampleRate) {
    const auto samples = std::max(timeMs, kMinTimeMs) * static_cast<float>(sampleRate) * 0.001f;
    return std::exp(kTimeConstant / samples);
}

float decibelsToGain(float db) {
    return std::pow(10.0f, db * 0.05f);
}

/// JUCE's normalisation: the five coefficients divided through by the one that
/// is not stored, computed in double and kept as float, which is what makes
/// this the same filter rather than one that agrees to a few decimals.
FollowerBiquad normalised(double c1, double c2, double c3, double c4, double c5, double c6) {
    const auto a = 1.0 / c4;

    FollowerBiquad filter;
    filter.c0 = static_cast<float>(c1 * a);
    filter.c1 = static_cast<float>(c2 * a);
    filter.c2 = static_cast<float>(c3 * a);
    filter.c3 = static_cast<float>(c5 * a);
    filter.c4 = static_cast<float>(c6 * a);
    return filter;
}

/// Inside the band the filters can describe. A cutoff at or above Nyquist has
/// no shape, and the model's own range stops at 20 kHz, which is above it at
/// 32 kHz and below.
double usableCutoff(double sampleRate, double frequency) {
    return std::clamp(frequency, 20.0, std::max(sampleRate * 0.5 - 1.0, 20.0));
}

}  // namespace

void FollowerBiquad::reset() {
    v1 = 0.0f;
    v2 = 0.0f;
}

float FollowerBiquad::process(float in) {
    const float out = (c0 * in) + v1;
    v1 = (c1 * in) - (c3 * out) + v2;
    v2 = (c2 * in) - (c4 * out);
    return out;
}

FollowerBiquad followerLowPass(double sampleRate, double frequency) {
    // JUCE's makeLowPass at its default Q of one over root two.
    const auto q = 1.0 / std::numbers::sqrt2;
    const auto n = 1.0 / std::tan(std::numbers::pi * usableCutoff(sampleRate, frequency) /
                                  std::max(sampleRate, 1.0));
    const auto nSquared = n * n;
    const auto c1 = 1.0 / (1.0 + (n / q) + nSquared);

    return normalised(c1, c1 * 2.0, c1, 1.0, c1 * 2.0 * (1.0 - nSquared),
                      c1 * (1.0 - (n / q) + nSquared));
}

FollowerBiquad followerHighPass(double sampleRate, double frequency) {
    const auto q = 1.0 / std::numbers::sqrt2;
    const auto n = std::tan(std::numbers::pi * usableCutoff(sampleRate, frequency) /
                            std::max(sampleRate, 1.0));
    const auto nSquared = n * n;
    const auto c1 = 1.0 / (1.0 + (n / q) + nSquared);

    return normalised(c1, c1 * -2.0, c1, 1.0, c1 * 2.0 * (nSquared - 1.0),
                      c1 * (1.0 - (n / q) + nSquared));
}

void detectFollowerSource(FollowerState& state, const FollowerSettings& settings,
                          std::span<const float> mono, double sampleRate,
                          std::span<float> scratch) {
    // A rate change makes every coefficient stale, filters and time constants
    // alike. Cleared rather than converted: what a filter holds is samples at
    // the old rate and there is nothing to convert them into.
    if (state.sampleRate != sampleRate) {
        state.sampleRate = sampleRate;
        state.highPassHz = 0.0f;
        state.lowPassHz = 0.0f;
        state.highPass.reset();
        state.lowPass.reset();
    }

    const auto count = std::min(mono.size(), scratch.size());
    if (count == 0) {
        state.sourcePeak = 0.0f;
        return;
    }

    const float gain = decibelsToGain(settings.gainDb);

    // The gained peak on its own where nothing is filtered, which is almost
    // every follower: the scratch buffer is only worth filling when something
    // is going to read it back.
    if (!settings.highPass && !settings.lowPass) {
        float peak = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
            peak = std::max(peak, std::abs(mono[i] * gain));

        state.sourcePeak = peak;
        return;
    }

    for (std::size_t i = 0; i < count; ++i)
        scratch[i] = mono[i] * gain;

    // High pass first and low pass second, which is the fork's order. Two
    // second-order sections do not commute exactly in floating point, so the
    // order is part of the answer rather than a detail of it.
    if (settings.highPass) {
        if (cutoffMoved(state.highPassHz, settings.highPassHz)) {
            state.highPassHz = settings.highPassHz;
            const auto carried = state.highPass;
            state.highPass = followerHighPass(sampleRate, settings.highPassHz);
            state.highPass.v1 = carried.v1;
            state.highPass.v2 = carried.v2;
        }

        for (std::size_t i = 0; i < count; ++i)
            scratch[i] = state.highPass.process(scratch[i]);
    }

    if (settings.lowPass) {
        if (cutoffMoved(state.lowPassHz, settings.lowPassHz)) {
            state.lowPassHz = settings.lowPassHz;
            const auto carried = state.lowPass;
            state.lowPass = followerLowPass(sampleRate, settings.lowPassHz);
            state.lowPass.v1 = carried.v1;
            state.lowPass.v2 = carried.v2;
        }

        for (std::size_t i = 0; i < count; ++i)
            scratch[i] = state.lowPass.process(scratch[i]);
    }

    float peak = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
        peak = std::max(peak, std::abs(scratch[i]));

    state.sourcePeak = peak;
}

float advanceFollower(FollowerState& state, const FollowerSettings& settings,
                      const BlockInfo& block, const ModTiming& timing) {
    const double sampleRate = std::max(timing.sampleRate, 1.0);
    const int numSamples = std::max(block.numSamples, 0);

    const float attack = coefficientFor(settings.attackMs, sampleRate);
    const float release = coefficientFor(settings.releaseMs, sampleRate);
    const int holdSamples =
        static_cast<int>(std::max(settings.holdMs, 0.0f) * 0.001f * static_cast<float>(sampleRate));

    // The peak the detector left, held flat across the block. The detection has
    // already reduced the block to one number and what is left for the envelope
    // is the time constant, which is why this is a run of one value rather than
    // the waveform: it is what the fork feeds an externally driven follower.
    const float input = std::max(state.sourcePeak, 0.0f);

    for (int i = 0; i < numSamples; ++i) {
        if (input > state.envelope) {
            state.envelope = (attack * (state.envelope - input)) + input;
            state.holdLeft = holdSamples;
        } else if (state.holdLeft > 0) {
            --state.holdLeft;
        } else {
            state.envelope = (release * (state.envelope - input)) + input;
        }

        state.envelope = std::clamp(state.envelope, 0.0f, 1.0f);
    }

    return state.envelope;
}

}  // namespace magda::engine
