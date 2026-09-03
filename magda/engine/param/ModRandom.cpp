#include "param/ModRandom.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::engine {

namespace {

/// The rate guard the LFO uses, and for the same reason: a modulated rate can
/// arrive at the bottom of its range and a period is one divided by it.
constexpr double kMinHz = 1.0e-4;

/// One draw from a splitmix64 walk. Deterministic, seedable and one line, which
/// is the whole requirement: the walk has to be the same on every run of a
/// project and independent between two modulators in it.
std::uint64_t nextBits(std::uint64_t& seed) {
    seed += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = seed;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/// A draw in [0, 1).
float nextUnitFloat(std::uint64_t& seed) {
    // The top 24 bits, which is a float's mantissa: taking the low ones would
    // read the weakest part of the word.
    return static_cast<float>(nextBits(seed) >> 40) / static_cast<float>(1 << 24);
}

/// The fork's S-curve (AudioFadeCurve::SCurve), which is what the smoothing
/// control bends the phase towards. Transcribed rather than approximated: it is
/// audible on a ramped step and the two engines have to bend it alike.
float sCurve(float alpha) {
    const auto quarterTurn = alpha * 0.5f * std::numbers::pi_v<float>;
    return ((1.0f - alpha) * (1.0f - std::cos(quarterTurn))) + (alpha * std::sin(quarterTurn));
}

/// Into [0, 1), which is where a phase has to be before anything reads it.
float wrapPhase(float phase) {
    phase -= std::floor(phase);
    return phase >= 1.0f ? 0.0f : phase;
}

/**
 * @brief Take one step of the walk.
 *
 * The next value lands within @p stepDepth of the one before it, clipped to the
 * output range. Clipped rather than reflected or wrapped: a walk that bounced
 * off an end would visit the extremes more often than the middle, and one that
 * wrapped would jump the full range from a control that says how far it may
 * move.
 *
 * The half-range is half the control, which is the fork's arithmetic for a
 * unipolar modulator: its own bipolar parameter is off, because MAGDA keeps
 * polarity per link.
 */
void takeStep(RandomState& state, float stepDepth) {
    state.previous = state.current;

    const float half = std::clamp(stepDepth, 0.0f, 1.0f) * 0.5f;
    const float low = std::max(state.current - half, 0.0f);
    const float high = std::min(state.current + half, 1.0f);

    state.current = low >= high ? low : low + (nextUnitFloat(state.seed) * (high - low));
}

}  // namespace

void seedRandom(RandomState& state, std::uint64_t address) {
    // Run through the generator once, so two modifiers at adjacent addresses do
    // not open on adjacent numbers.
    state.seed = address;
    state.seed = nextBits(state.seed);
}

void restartRandom(RandomState& state, const RandomSettings& settings) {
    // The LFO's rule: a modulator whose phase is a function of the timeline, or
    // is simply free running, has a phase that is not a trigger's to move.
    if (settings.sync != ModSync::Note)
        return;

    state.cycles = 0.0;

    // A step as well as a phase. A restart that only moved the phase would
    // replay the ramp towards the number the walk was already heading for,
    // which is a retrigger that does not sound like one.
    state.stepPending = true;
}

float advanceRandom(RandomState& state, const RandomSettings& settings, const BlockInfo& block,
                    const ModTiming& timing) {
    const double hz = std::max(static_cast<double>(settings.rate.hz), kMinHz);
    const double beatsPerCycle =
        std::max(cycleBeats(settings.rate.rateType, timing.numerator, timing.denominator), 1.0e-6);

    const bool noise = settings.type == RandomShape::Noise;

    // A timeline-locked walk steps on the grid rather than on how many blocks
    // have gone by, so two of them at one rate step together. Noise has no grid
    // to lock to: it steps once a block whatever the transport is doing.
    if (settings.sync == ModSync::Transport && !noise) {
        state.cycles = settings.tempoSync
                           ? modBarPosition(block, timing) / barFractionOf(settings.rate.rateType)
                           : block.seconds.start * hz;
    }

    const float raw = wrapPhase(static_cast<float>(state.cycles - std::floor(state.cycles)));

    // Smoothing bends the phase, not the output, so it eases the ends of a
    // ramped step and leaves a held one alone: there is nothing to ease when
    // the value does not travel. The fork applies it here, before the wrap is
    // looked for, and the two have to agree about where a step boundary is.
    const float smooth = std::clamp(settings.smooth, 0.0f, 1.0f);
    const float phase = wrapPhase(((1.0f - smooth) * raw) + (smooth * sCurve(raw)));

    if (noise || state.stepPending || phase < state.phase) {
        state.stepPending = false;
        takeStep(state, settings.stepDepth);
    }

    const float shape = std::clamp(settings.shape, 0.0f, 1.0f);

    // Noise has no step to sit inside, so what it publishes is the number it
    // just drew. A stepped walk holds the previous number until the last
    // @p shape of the step and travels to the new one across it, which at a
    // shape of zero is a sample and hold a step behind and at one is a ramp all
    // the way across.
    float value = state.current;

    if (!noise) {
        const float travelFrom = 1.0f - shape;
        value = phase < travelFrom
                    ? state.previous
                    : state.previous + ((state.current - state.previous) *
                                        ((phase - travelFrom) / std::max(shape, 1.0e-6f)));
    }

    state.phase = phase;
    state.value = std::clamp(value, 0.0f, 1.0f);

    // Moved on after this block's value has been settled, which is the LFO's
    // order and the fork's: the value a block renders with is the value at its
    // first sample.
    if (settings.sync != ModSync::Transport) {
        if (settings.tempoSync) {
            // The beats the block covered over the beats a step lasts, off the
            // block rather than through a bpm, which is the LFO's advance and
            // is what makes a step across a tempo change the map's length
            // rather than the opening tempo's (#2340).
            state.cycles += modBeatsElapsed(block, timing) / beatsPerCycle;
        } else {
            const double blockSeconds = static_cast<double>(std::max(block.numSamples, 0)) /
                                        std::max(timing.sampleRate, 1.0);
            state.cycles += blockSeconds * hz;
        }
    }

    // Kept bounded, so a walk left running for hours steps as precisely as one
    // that started a moment ago.
    if (state.cycles >= 1.0)
        state.cycles -= std::floor(state.cycles);

    return state.value;
}

}  // namespace magda::engine
