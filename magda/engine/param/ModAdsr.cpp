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

/**
 * @brief Run the stage machine forward by @p seconds.
 *
 * The fork's own loop. A stage that ends inside the block hands what is left of
 * it to the next one, so a block longer than the attack arrives somewhere in
 * the decay rather than being clamped at the top of it.
 */
void advanceStages(AdsrState& state, const AdsrSettings& settings, const ModTiming& timing,
                   double seconds, bool freeRunning, bool gateOpen) {
    const double attackSeconds = adsrStageSeconds(settings.attackMs, settings, timing);
    const double decaySeconds = adsrStageSeconds(settings.decayMs, settings, timing);
    const double releaseSeconds = adsrStageSeconds(settings.releaseMs, settings, timing);
    const float sustain = std::clamp(settings.sustain, 0.0f, 1.0f);

    /// One timed stage: what it travels from, what it arrives at, how long it
    /// takes, how it is bent, and where it goes when it is through.
    const auto run = [&](double length, float destination, float curve, AdsrStage next) {
        if (length <= kMinStageSeconds) {
            state.value = destination;
            enterStage(state, next, destination);
            return true;
        }

        state.timeInStage += seconds;

        if (state.timeInStage >= length) {
            seconds = state.timeInStage - length;
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
                if (run(attackSeconds, 1.0f, settings.attackCurve, AdsrStage::Decay))
                    continue;
                return;

            case AdsrStage::Decay:
                if (run(decaySeconds, sustain, settings.decayCurve, AdsrStage::Sustain))
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
                if (run(releaseSeconds, 0.0f, settings.releaseCurve, AdsrStage::Idle))
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
    if (settings.tempoSync && settings.rateType != static_cast<int>(magda::ModRateType::Hertz)) {
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

    const double blockSeconds =
        static_cast<double>(std::max(block.numSamples, 0)) / std::max(timing.sampleRate, 1.0);

    advanceStages(state, settings, timing, blockSeconds, freeRunning, gateOpen);

    // Advanced first and published after, which is the fork's ADSR timer and
    // the opposite of its LFO timer: the value a block renders with is where
    // the envelope ends up rather than where it started. Reproduced rather than
    // tidied, because the fork is what a parity case compares against.
    state.value = std::clamp(state.value, 0.0f, 1.0f);
    return state.value;
}

}  // namespace magda::engine
