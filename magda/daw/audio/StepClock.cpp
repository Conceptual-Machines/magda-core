#include "StepClock.hpp"

namespace magda::daw::audio {

StepClock::StepClock() = default;

void StepClock::reset() {
    nextStepBeat_ = -1.0;
    tickParity_ = 0;
    sequenceStep_ = 0;
    cycleStep_ = 0;
    cycleOriginBeat_ = 0.0;
    goingUp_ = true;
    wasPlaying_ = false;
    running_ = false;
}

double StepClock::rateToBeats(Rate r) {
    switch (r) {
        case Rate::DottedQuarter:
            return 1.5;
        case Rate::Quarter:
            return 1.0;
        case Rate::TripletQuarter:
            return 2.0 / 3.0;
        case Rate::DottedEighth:
            return 0.75;
        case Rate::Eighth:
            return 0.5;
        case Rate::TripletEighth:
            return 1.0 / 3.0;
        case Rate::DottedSixteenth:
            return 0.375;
        case Rate::Sixteenth:
            return 0.25;
        case Rate::TripletSixteenth:
            return 0.5 / 3.0;
        case Rate::ThirtySecond:
            return 0.125;
        default:
            return 0.5;
    }
}

double StepClock::applyRampCurve(double t, float depth, float skew) {
    double d = static_cast<double>(juce::jlimit(-0.99f, 0.99f, depth));
    double s =
        static_cast<double>(juce::jlimit(0.01, 0.99, 0.5 + static_cast<double>(skew) * 0.49));

    if (std::abs(d) < 0.001)
        return t;

    double u;
    double a = 1.0 - 2.0 * s;
    if (std::abs(a) < 1e-10) {
        u = t;
    } else {
        double disc = s * s + a * t;
        u = (-s + std::sqrt(std::max(0.0, disc))) / a;
        u = juce::jlimit(0.0, 1.0, u);
    }

    return 2.0 * (1.0 - u) * u * (s + d) + u * u;
}

int StepClock::advanceStep(int current, int numSteps, Direction dir) {
    if (numSteps <= 1)
        return 0;

    switch (dir) {
        case Direction::Forward:
            return (current + 1) % numSteps;

        case Direction::Reverse:
            return (current - 1 + numSteps) % numSteps;

        case Direction::PingPong:
            if (goingUp_) {
                if (current >= numSteps - 1) {
                    goingUp_ = false;
                    return current - 1;
                }
                return current + 1;
            } else {
                if (current <= 0) {
                    goingUp_ = true;
                    return current + 1;
                }
                return current - 1;
            }

        case Direction::Random:
            return random_.nextInt(numSteps);

        default:
            return (current + 1) % numSteps;
    }
}

int StepClock::processBlock(const te::PluginRenderContext& fc, te::Edit& edit, Rate rate,
                            Direction direction, float swing, int numSteps, StepEvent* events,
                            int maxEvents, float rampDepth, float rampSkew) {
    if (numSteps <= 0 || maxEvents <= 0)
        return 0;

    // --- Handle transport transitions ---
    if (fc.isPlaying && !wasPlaying_) {
        reset();
        wasPlaying_ = true;
        running_ = true;
    } else if (!fc.isPlaying && wasPlaying_) {
        reset();
        return 0;
    }

    // Only run when transport is playing
    if (!fc.isPlaying) {
        running_ = false;
        return 0;
    }

    running_ = true;

    // --- Get beat positions for this block ---
    auto& tempoSeq = edit.tempoSequence;
    double blockStartBeat = tempoSeq.toBeats(fc.editTime.getStart()).inBeats();
    double blockEndBeat = tempoSeq.toBeats(fc.editTime.getEnd()).inBeats();

    if (blockEndBeat <= blockStartBeat)
        return 0;

    double stepBeats = rateToBeats(rate);
    double cycleBeats = stepBeats * numSteps;
    double blockDurationSecs = static_cast<double>(fc.bufferNumSamples) / sampleRate_;
    bool hasRamp = std::abs(rampDepth) > 0.001f && numSteps > 1;

    // Helper: compute the warped duration for a given step in the cycle
    auto warpedStepDuration = [&](int stepInCycle) -> double {
        if (!hasRamp)
            return stepBeats;
        double t0 = static_cast<double>(stepInCycle) / static_cast<double>(numSteps);
        double t1 = static_cast<double>(stepInCycle + 1) / static_cast<double>(numSteps);
        return (applyRampCurve(t1, rampDepth, rampSkew) - applyRampCurve(t0, rampDepth, rampSkew)) *
               cycleBeats;
    };

    // Initialise on first block — quantise to the nearest cycle grid position
    if (nextStepBeat_ < 0.0) {
        cycleOriginBeat_ = std::floor(blockStartBeat / cycleBeats) * cycleBeats;
        cycleStep_ = 0;
        if (hasRamp) {
            // Find which step in the cycle we're at
            double beatInCycle = blockStartBeat - cycleOriginBeat_;
            double accum = 0.0;
            for (int i = 0; i < numSteps; ++i) {
                double dur = warpedStepDuration(i);
                if (accum + dur > beatInCycle + 1e-10) {
                    cycleStep_ = i;
                    nextStepBeat_ = cycleOriginBeat_ + accum;
                    break;
                }
                accum += dur;
            }
            if (nextStepBeat_ < 0.0)
                nextStepBeat_ = cycleOriginBeat_ + cycleBeats;
        } else {
            nextStepBeat_ = std::floor(blockStartBeat / stepBeats) * stepBeats;
        }
    }

    // Catch up if we fell behind (e.g. transport jumped forward)
    int catchUp = 0;
    while (nextStepBeat_ < blockStartBeat && catchUp < numSteps * 2) {
        nextStepBeat_ += warpedStepDuration(cycleStep_);
        sequenceStep_ = advanceStep(sequenceStep_, numSteps, direction);
        cycleStep_ = (cycleStep_ + 1) % numSteps;
        if (cycleStep_ == 0)
            cycleOriginBeat_ = nextStepBeat_;
        ++tickParity_;
        ++catchUp;
    }
    // If still behind, re-anchor
    if (nextStepBeat_ < blockStartBeat) {
        cycleOriginBeat_ = std::floor(blockStartBeat / cycleBeats) * cycleBeats;
        cycleStep_ = 0;
        nextStepBeat_ = cycleOriginBeat_;
        while (nextStepBeat_ < blockStartBeat) {
            nextStepBeat_ += warpedStepDuration(cycleStep_);
            cycleStep_ = (cycleStep_ + 1) % numSteps;
        }
        if (cycleStep_ == 0)
            cycleOriginBeat_ = nextStepBeat_;
    }

    // --- Emit step events within this block ---
    int eventCount = 0;

    while (nextStepBeat_ < blockEndBeat && eventCount < maxEvents) {
        // Apply swing to odd ticks
        double swungBeat = nextStepBeat_;
        if (tickParity_ % 2 == 1 && swing > 0.0f)
            swungBeat += static_cast<double>(swing) * warpedStepDuration(cycleStep_) * 0.5;

        if (swungBeat >= blockStartBeat && swungBeat < blockEndBeat) {
            double frac = (swungBeat - blockStartBeat) / (blockEndBeat - blockStartBeat);
            double timeInBlock = frac * blockDurationSecs;

            events[eventCount] = {
                .stepIndex = sequenceStep_, .beatPosition = swungBeat, .timeInBlock = timeInBlock};
            ++eventCount;
        }

        // Advance to next step
        nextStepBeat_ += warpedStepDuration(cycleStep_);
        sequenceStep_ = advanceStep(sequenceStep_, numSteps, direction);
        cycleStep_ = (cycleStep_ + 1) % numSteps;
        if (cycleStep_ == 0)
            cycleOriginBeat_ = nextStepBeat_;
        ++tickParity_;
    }

    return eventCount;
}

}  // namespace magda::daw::audio
