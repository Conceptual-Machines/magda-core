#include "StepClock.hpp"

namespace magda::daw::audio {

StepClock::StepClock() = default;

void StepClock::reset() {
    globalTick_ = 0;
    sequenceStep_ = 0;
    goingUp_ = true;
    originBeat_ = -1.0;
    wasPlaying_ = false;
    running_ = false;
    freeRunSamples_ = 0.0;
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

    // Initialise origin on first block
    if (originBeat_ < 0.0)
        originBeat_ = std::floor(blockStartBeat / stepBeats) * stepBeats;

    // --- Walk ticks and find events in this block ---
    int eventCount = 0;

    // Beat position of a given global tick
    auto tickBeat = [&](int tick) -> double { return originBeat_ + tick * stepBeats; };

    // Skip past ticks before this block
    while (tickBeat(globalTick_) < blockStartBeat) {
        ++globalTick_;
        sequenceStep_ = advanceStep(sequenceStep_, numSteps, direction);
    }

    double beat = tickBeat(globalTick_);

    while (beat < blockEndBeat && eventCount < maxEvents) {
        // Apply swing to odd ticks
        double swungBeat = beat;
        if (globalTick_ % 2 == 1 && swing > 0.0f)
            swungBeat += static_cast<double>(swing) * stepBeats * 0.5;

        if (swungBeat >= blockStartBeat && swungBeat < blockEndBeat) {
            double frac = (swungBeat - blockStartBeat) / (blockEndBeat - blockStartBeat);
            double timeInBlock = frac * blockDurationSecs;

            events[eventCount] = {
                .stepIndex = sequenceStep_, .beatPosition = swungBeat, .timeInBlock = timeInBlock};
            ++eventCount;
        }

        ++globalTick_;
        sequenceStep_ = advanceStep(sequenceStep_, numSteps, direction);
        beat = tickBeat(globalTick_);
    }

    return eventCount;
}

}  // namespace magda::daw::audio
