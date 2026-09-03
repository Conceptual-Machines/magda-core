#include "param/ModAdsr.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

/// A stage shorter than this is one the user means to be instant. The fork's
/// own floor, and it is a comparison rather than a clamp: a zero-length attack
/// arrives at the top and hands the rest of the block to the decay, which is
/// what makes an envelope with no attack a click on purpose.
constexpr double kMinStageSeconds = 0.0001;

/// A single block can step through several stages when the lengths are very
/// short, and every one of them consumes what is left of the block. The bound
/// is what stops a set of zero-length stages from spinning; the fork's own,
/// and reached only by an envelope that has no time in it anywhere.
constexpr int kMaxStagesPerBlock = 8;

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * @brief The control point of the fork's quadratic bezier.
 *
 * Transcribed from BezierHelpers::getQuadraticControlPoint rather than
 * reasoned out again: the curvature is a persisted number and the shape it
 * stands for is whatever that function says it is. The two branches are the
 * rising and falling segment, which are not mirror images of each other in the
 * original and are not made into one here.
 */
Point controlPointFor(Point start, Point end, float curve) {
    const float c = std::clamp(curve * 2.0f, -1.0f, 1.0f);
    const float halfRun = (end.x - start.x) / 2.0f;
    const float x = start.x + halfRun + (halfRun * c);

    if (end.y > start.y) {
        const float halfRise = (end.y - start.y) / 2.0f;
        return Point{x, start.y + halfRise - (halfRise * c)};
    }

    const float halfRise = (start.y - end.y) / 2.0f;
    return Point{x, end.y + halfRise + (halfRise * c)};
}

/// The bezier's y at an x, by solving for t and evaluating. The fork's
/// BezierHelpers::getQuadraticYFromX, on the same terms.
float quadraticYFromX(float x, Point start, Point control, Point end) {
    if (start.x == end.x || start.y == end.y)
        return start.y;

    const float a = start.x - (2.0f * control.x) + end.x;
    const float b = (-2.0f * start.x) + (2.0f * control.x);
    const float c = start.x - x;

    float t = 0.0f;

    if (a == 0.0f) {
        t = -c / b;
    } else {
        const float discriminant = std::max(b * b - 4.0f * a * c, 0.0f);
        const float root = std::sqrt(discriminant);
        t = (-b + root) / (2.0f * a);

        if (t < 0.0f || t > 1.0f)
            t = (-b - root) / (2.0f * a);
    }

    const float u = 1.0f - t;
    return u * u * start.y + 2.0f * t * u * control.y + t * t * end.y;
}

/// Where the gate comes from this block, which is the whole of what the sync
/// mode decides for an envelope.
bool gateOpenFor(const AdsrState& state, const AdsrSettings& settings, const BlockInfo& block) {
    switch (settings.sync) {
        case ModSync::Free:
            // Held open, so the envelope cycles rather than waiting for
            // something that is never going to arrive.
            return true;

        case ModSync::Transport:
            return block.playing;

        case ModSync::Note:
            return !state.gated;
    }

    return true;
}

void enterStage(AdsrState& state, AdsrStage stage, float startValue) {
    state.stage = stage;
    state.timeInStage = 0.0;
    state.stagePhase = 0.0f;
    state.stageStartValue = startValue;
}

/// Whether an envelope's stages are a musical length rather than a duration. A
/// division of Hertz is not a division, and falls back to the milliseconds,
/// which is the fork's rule (adsrStageSeconds says so).
bool runsInBars(const AdsrSettings& settings) {
    return settings.tempoSync && settings.rateType != static_cast<int>(magda::ModRateType::Hertz);
}

/**
 * @brief The three timed stages of one envelope, and the floor below which a
 *        stage is instant.
 *
 * All four in one unit: seconds for an envelope whose stages are milliseconds,
 * bars for one whose stages are a division. The machine below only compares an
 * elapsed amount against a length and divides one by the other, so it does not
 * care which, and running a synced envelope in bars is what lets the block say
 * how many it moved through rather than a bpm and a signature read once
 * (#2340).
 */
struct StageLengths {
    double attack = 0.0;
    double decay = 0.0;
    double release = 0.0;
    double instant = 0.0;
};

StageLengths stageLengthsFor(const AdsrSettings& settings, const ModTiming& timing) {
    if (runsInBars(settings)) {
        // One division for all three stages, which is what the model carries,
        // and a division is a number of bars whatever the signature is doing:
        // no signature to ask, unlike the seconds this same division is worth
        // (adsrStageSeconds).
        const auto bars = barFractionOf(settings.rateType);

        // The same instant the seconds floor stands for, converted once at the
        // tempo and signature the block opens on: what counts as no time at all
        // is a duration rather than a musical length, and a hundredth of a
        // millisecond is far below any division a project can name.
        return {bars, bars, bars,
                kMinStageSeconds * timing.bpm / 60.0 /
                    std::max(barBeatsOf(timing.numerator, timing.denominator), 1.0e-6)};
    }

    return {adsrStageSeconds(settings.attackMs, settings, timing),
            adsrStageSeconds(settings.decayMs, settings, timing),
            adsrStageSeconds(settings.releaseMs, settings, timing), kMinStageSeconds};
}

/**
 * @brief Run the stage machine forward by @p elapsed.
 *
 * The fork's own loop. A stage that ends inside the block hands what is left of
 * it to the next one, so a block longer than the attack arrives somewhere in
 * the decay rather than being clamped at the top of it.
 *
 * @p elapsed and @p lengths are in the same unit and the machine never asks
 * which (@ref StageLengths).
 */
void advanceStages(AdsrState& state, const AdsrSettings& settings, const StageLengths& lengths,
                   double elapsed, bool freeRunning, bool gateOpen) {
    const float sustain = std::clamp(settings.sustain, 0.0f, 1.0f);

    /// One timed stage: what it travels from, what it arrives at, how long it
    /// takes, how it is bent, and where it goes when it is through.
    const auto run = [&](double length, float destination, float curve, AdsrStage next) {
        if (length <= lengths.instant) {
            state.value = destination;
            enterStage(state, next, destination);
            return true;
        }

        state.timeInStage += elapsed;

        if (state.timeInStage >= length) {
            elapsed = state.timeInStage - length;
            state.value = destination;
            enterStage(state, next, destination);
            return true;
        }

        state.stagePhase = static_cast<float>(state.timeInStage / length);
        state.value = adsrSegmentAt(state.stagePhase, state.stageStartValue, destination, curve);
        return false;
    };

    for (int guard = 0; guard < kMaxStagesPerBlock; ++guard) {
        switch (state.stage) {
            case AdsrStage::Idle:
                state.value = 0.0f;
                state.stagePhase = 0.0f;

                // A free-running envelope has nothing to wait for, so it starts
                // again rather than resting: that is what makes it a cycle.
                if (freeRunning && gateOpen) {
                    enterStage(state, AdsrStage::Attack, 0.0f);
                    continue;
                }
                return;

            case AdsrStage::Attack:
                if (run(lengths.attack, 1.0f, settings.attackCurve, AdsrStage::Decay))
                    continue;
                return;

            case AdsrStage::Decay:
                if (run(lengths.decay, sustain, settings.decayCurve, AdsrStage::Sustain))
                    continue;
                return;

            case AdsrStage::Sustain:
                // Tracked live, so moving the sustain knob under a held note
                // moves the envelope rather than waiting for the next one.
                state.value = sustain;
                state.stagePhase = 0.0f;

                // Free running skips the hold, which is what turns A-D-S-R into
                // the A-D-R cycle a free-running envelope is.
                if (freeRunning) {
                    enterStage(state, AdsrStage::Release, state.value);
                    continue;
                }
                return;

            case AdsrStage::Release:
                if (run(lengths.release, 0.0f, settings.releaseCurve, AdsrStage::Idle))
                    continue;
                return;
        }
    }
}

}  // namespace

double adsrStageSeconds(float milliseconds, const AdsrSettings& settings, const ModTiming& timing) {
    // A division of Hertz is not a division. The model reaches that state by
    // having tempo sync on with nothing musical selected, and the fork's answer
    // is to fall back to the milliseconds rather than to invent a period.
    if (runsInBars(settings)) {
        const auto beats = cycleBeats(settings.rateType, timing.numerator, timing.denominator);
        return beats * 60.0 / std::max(timing.bpm, 1.0e-6);
    }

    return std::max(static_cast<double>(milliseconds), 0.0) / 1000.0;
}

float adsrSegmentAt(float t, float startValue, float endValue, float curve) {
    t = std::clamp(t, 0.0f, 1.0f);

    // Nothing to bend, and the bezier would divide by the run it does not have.
    if (curve == 0.0f || startValue == endValue)
        return startValue + (endValue - startValue) * t;

    const Point start{0.0f, startValue};
    const Point end{1.0f, endValue};
    return quadraticYFromX(t, start, controlPointFor(start, end, curve), end);
}

void restartAdsr(AdsrState& state, const AdsrSettings& settings, bool fromZero) {
    const float from = fromZero ? 0.0f : state.value;

    enterStage(state, AdsrStage::Attack, from);
    state.value = from;

    // The gate was opened by whoever triggered, and this is that gate being
    // seen: without it the next block reads a rising edge and enters the attack
    // a second time, one block in.
    state.lastGate = true;
    state.started = true;
    state.trigger = settings.trigger;
}

float advanceAdsr(AdsrState& state, const AdsrSettings& settings, const BlockInfo& block,
                  const ModTiming& timing) {
    // A fresh state takes its gate from the settings, and so does one whose
    // trigger mode has changed underneath it. The LFO's rule, for the LFO's
    // reason (advanceLfo says it): a gate belongs to the triggers of the mode
    // that opened it, and a mode change retires all of them.
    if (!state.started || state.trigger != settings.trigger) {
        state.started = true;
        state.trigger = settings.trigger;
        state.gated = settings.startGated;
        state.heldNotes = 0;

        // The edge goes with the gate. Shut rather than whatever the old mode
        // left, so a mode whose gate is open is an opening edge on the first
        // block under it: an audio-triggered envelope switched to free running
        // starts, rather than sitting idle waiting for a rise it will never see
        // because its gate is already up.
        state.lastGate = false;
    }

    // A trigger asked for one block of nothing, and this is the first block a
    // device can see it in. The envelope is held where the restart left it, so
    // the attack starts on the block after rather than a block into itself.
    if (state.forceZero) {
        state.forceZero = false;
        state.value = 0.0f;
        state.stagePhase = 0.0f;
        return 0.0f;
    }

    const bool freeRunning = settings.sync == ModSync::Free;
    const bool gateOpen = gateOpenFor(state, settings, block);

    // The edge, not the level. Retriggered and released from wherever the
    // envelope currently is, so a gate change is click-free from any stage.
    if (gateOpen && !state.lastGate)
        enterStage(state, AdsrStage::Attack, state.value);
    else if (!gateOpen && state.lastGate && state.stage != AdsrStage::Idle)
        enterStage(state, AdsrStage::Release, state.value);

    state.lastGate = gateOpen;

    const auto lengths = stageLengthsFor(settings, timing);

    // A tempo-sync toggle mid-stage changes what unit timeInStage is counted
    // in without emptying it, and reinterpreting a duration as a bar count (or
    // the reverse) would snap the envelope to wherever that number happens to
    // land in the new unit rather than leaving it where it was. Converted
    // instead, from the phase the old unit had already worked out: the current
    // stage's length in the new unit times how far through it the envelope is
    // puts the accumulator back at the same point (#2340).
    if (runsInBars(settings) != state.stageInBars) {
        const auto currentLength = [&]() {
            switch (state.stage) {
                case AdsrStage::Attack:
                    return lengths.attack;
                case AdsrStage::Decay:
                    return lengths.decay;
                case AdsrStage::Release:
                    return lengths.release;
                case AdsrStage::Idle:
                case AdsrStage::Sustain:
                    return 0.0;
            }
            return 0.0;
        }();

        state.timeInStage = static_cast<double>(state.stagePhase) * currentLength;
        state.stageInBars = runsInBars(settings);
    }

    // A synced envelope's stages are a number of bars, so the block moves it on
    // by the bars it covered, which the map answered for exactly this stretch.
    // A millisecond envelope is a duration and moves on by how long the block
    // lasted (#2340).
    const double elapsed =
        runsInBars(settings)
            ? modBarsElapsed(block, timing)
            : static_cast<double>(std::max(block.numSamples, 0)) / std::max(timing.sampleRate, 1.0);

    advanceStages(state, settings, lengths, elapsed, freeRunning, gateOpen);

    // Advanced first and published after, which is the fork's ADSR timer and
    // the opposite of its LFO timer: the value a block renders with is where
    // the envelope ends up rather than where it started. Reproduced rather than
    // tidied, because the fork is what a parity case compares against.
    state.value = std::clamp(state.value, 0.0f, 1.0f);
    return state.value;
}

}  // namespace magda::engine
