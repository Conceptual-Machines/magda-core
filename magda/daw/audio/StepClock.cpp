#include "StepClock.hpp"

namespace magda::daw::audio {

StepClock::StepClock() = default;

void StepClock::reset() {
    nextStepBeat_ = -1.0;
    tickParity_ = 0;
    sequenceStep_ = 0;
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
                            int maxEvents) {
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
    double blockDurationSecs = static_cast<double>(fc.bufferNumSamples) / sampleRate_;

    // Initialise on first block — quantise to the nearest step grid position
    if (nextStepBeat_ < 0.0)
        nextStepBeat_ = std::floor(blockStartBeat / stepBeats) * stepBeats;

    // Catch up if we fell behind (e.g. transport jumped forward)
    // Limit iterations to prevent runaway loops
    int catchUp = 0;
    while (nextStepBeat_ < blockStartBeat && catchUp < numSteps) {
        nextStepBeat_ += stepBeats;
        sequenceStep_ = advanceStep(sequenceStep_, numSteps, direction);
        ++tickParity_;
        ++catchUp;
    }
    // If still behind after numSteps iterations, re-anchor
    if (nextStepBeat_ < blockStartBeat) {
        nextStepBeat_ = std::floor(blockStartBeat / stepBeats) * stepBeats;
        if (nextStepBeat_ < blockStartBeat)
            nextStepBeat_ += stepBeats;
    }

    // --- Emit step events within this block ---
    int eventCount = 0;

    while (nextStepBeat_ < blockEndBeat && eventCount < maxEvents) {
        // Apply swing to odd ticks
        double swungBeat = nextStepBeat_;
        if (tickParity_ % 2 == 1 && swing > 0.0f)
            swungBeat += static_cast<double>(swing) * stepBeats * 0.5;

        if (swungBeat >= blockStartBeat && swungBeat < blockEndBeat) {
            double frac = (swungBeat - blockStartBeat) / (blockEndBeat - blockStartBeat);
            double timeInBlock = frac * blockDurationSecs;

            events[eventCount] = {
                .stepIndex = sequenceStep_, .beatPosition = swungBeat, .timeInBlock = timeInBlock};
            ++eventCount;
        }

        // Advance to next step using the CURRENT rate (immune to future rate changes)
        nextStepBeat_ += stepBeats;
        sequenceStep_ = advanceStep(sequenceStep_, numSteps, direction);
        ++tickParity_;
    }

    return eventCount;
}

}  // namespace magda::daw::audio
