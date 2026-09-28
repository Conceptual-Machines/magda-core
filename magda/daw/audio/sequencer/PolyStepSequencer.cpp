#include "sequencer/PolyStepSequencer.hpp"

#include <algorithm>
#include <cstddef>

namespace magda::daw::audio::sequencer {

namespace {

/// Step events one block can carry: one pattern's worth.
///
/// A block covers a few steps at any sane rate, and sixteen was chosen for
/// that. But the timing warp can compress several steps into a small span, and
/// an offline block at a fast rate holds more of them than a callback does, so
/// the sixteenth was reachable - and past it the clock used to stop and lose
/// the rest. It holds them now, and this covers a whole pattern so it does not
/// have to hold them block after block (#2335).
constexpr int kMaxStepEventsPerBlock = kMaxSteps;

/// Blocks a chord may sound with no step event and no pending release before
/// the stuck-note guard cuts it.
constexpr int kMaxSilentBlocks = 4;

/// Steps' worth of silence a tie may hold its chord through, as in the mono
/// engine (#2335).
constexpr int kMaxTieHoldSteps = 4;

/// Note-offs land just before the note-ons that replace them.
constexpr double kNoteOffLead = 0.0001;

}  // namespace

void PolyStepSequencer::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    clock_.setSampleRate(sampleRate_);
}

void PolyStepSequencer::setRandomSeed(std::uint32_t seed) {
    rngState_ = seed != 0 ? seed : 0x9E3779B9U;
}

float PolyStepSequencer::nextRandom01() {
    auto x = rngState_;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    rngState_ = x;
    return static_cast<float>(x >> 8U) * (1.0f / 16777216.0f);
}

void PolyStepSequencer::reset(NoteSink* sink) {
    if (sink != nullptr)
        killAllNotes(*sink, 0.0);
    soundingCount_ = 0;
    soundingVelocity_ = 0;
    noteOffCountdown_ = 0;
    silentBlockCount_ = 0;
    tieHoldCountdown_ = 0;
    currentStep_ = -1;
    clock_.reset();
}

void PolyStepSequencer::killAllNotes(NoteSink& sink, double time) {
    for (int i = 0; i < soundingCount_; ++i) {
        sink.addNoteEvent({.timeInBlock = time,
                           .noteNumber = soundingNotes_[static_cast<size_t>(i)],
                           .isNoteOn = false});
    }
    soundingCount_ = 0;
    soundingVelocity_ = 0;
    noteOffCountdown_ = 0;
    tieHoldCountdown_ = 0;
}

void PolyStepSequencer::processBlock(const StepClock::BlockTiming& timing,
                                     const PolyPattern& pattern, const Params& params,
                                     NoteSink& sink) {
    if (!timing.isPlaying) {
        killAllNotes(sink, 0.0);
        clock_.reset();
        currentStep_ = -1;
        silentBlockCount_ = 0;
        return;
    }

    const auto rate = params.rate;
    const auto direction = params.direction;
    const float swing = std::clamp(params.swing, 0.0f, 1.0f);
    const float gateLength = std::clamp(params.gateLength, 0.05f, 1.0f);
    const float ramp = std::clamp(params.ramp, -1.0f, 1.0f);
    const float skew = std::clamp(params.skew, -1.0f, 1.0f);
    const int rampCycles = std::clamp(params.rampCycles, 1, 8);
    const bool hardAngle = params.hardAngle;
    const float quantize = std::clamp(params.quantize, 0.0f, 1.0f);
    const int quantizeSub = std::clamp(params.quantizeSub, 16, 512);

    const int stepCount = pattern.playingLength();
    const int blockSamples = timing.numSamples;
    const double blockDurationSecs = static_cast<double>(blockSamples) / sampleRate_;

    // The block's own extent gives the tempo (see MonoStepSequencer).
    const double beatSpan = timing.endBeat - timing.startBeat;
    const double samplesPerBeat = (beatSpan > 0.0 && blockSamples > 0)
                                      ? static_cast<double>(blockSamples) / beatSpan
                                      : sampleRate_ * 0.5;  // 120 bpm
    const double stepBeats = StepClock::rateToBeats(rate);
    const int stepDurationSamples = std::max(1, static_cast<int>(stepBeats * samplesPerBeat));

    const int rateIndex = static_cast<int>(rate);
    if (rateIndex != prevRate_ || rampCycles != prevCycles_ || hardAngle != prevHardAngle_) {
        killAllNotes(sink, 0.0);
        clock_.reset();
        prevRate_ = rateIndex;
        prevCycles_ = rampCycles;
        prevHardAngle_ = hardAngle;
    }

    StepClock::StepEvent events[kMaxStepEventsPerBlock]{};
    const int eventCount = clock_.processBlock(timing, rate, direction, swing, stepCount, events,
                                               kMaxStepEventsPerBlock, ramp, skew, rampCycles,
                                               hardAngle, quantize, quantizeSub);

    // A release scheduled by an earlier block, on the mono engine's terms: it
    // yields to a step that arrives first, and never cuts a tie short.
    if (noteOffCountdown_ > 0 && soundingCount_ > 0) {
        if (noteOffCountdown_ <= blockSamples) {
            const double countdownTime = static_cast<double>(noteOffCountdown_) / sampleRate_;
            const bool stepFiresFirst = eventCount > 0 && events[0].timeInBlock <= countdownTime;
            const bool nextStepIsTie = eventCount > 0 && pattern.step(events[0].stepIndex).tie &&
                                       pattern.step(events[0].stepIndex).gate;

            if (!stepFiresFirst && !nextStepIsTie)
                killAllNotes(sink, countdownTime);
            else
                noteOffCountdown_ = 0;
        } else if (eventCount > 0) {
            noteOffCountdown_ = 0;
        } else {
            noteOffCountdown_ -= blockSamples;
        }
    }

    for (int i = 0; i < eventCount; ++i) {
        const auto& event = events[i];
        const auto& step = pattern.step(event.stepIndex);

        currentStep_ = event.stepIndex;

        // A rest ends whatever was sounding.
        if (!step.gate) {
            killAllNotes(sink, event.timeInBlock);
            continue;
        }

        // A tie holds the sounding chord: no retrigger, no new countdown, and
        // no roll - the step never decides anything, so it never draws.
        if (step.tie && soundingCount_ > 0) {
            noteOffCountdown_ = 0;
            tieHoldCountdown_ = kMaxTieHoldSteps * stepDurationSamples;
            continue;
        }

        // A failed probability roll, and a step with nothing in it, both play
        // as a rest.
        if (step.probability < 1.0f && nextRandom01() >= step.probability) {
            killAllNotes(sink, event.timeInBlock);
            continue;
        }
        if (step.noteCount == 0) {
            killAllNotes(sink, event.timeInBlock);
            continue;
        }

        if (soundingCount_ > 0)
            killAllNotes(sink, std::max(0.0, event.timeInBlock - kNoteOffLead));

        int displayVelocity = std::clamp(step.velocity, 1, 127);
        const int noteCount = std::min(step.noteCount, kMaxNotesPerStep);
        for (int n = 0; n < noteCount; ++n) {
            const auto& note = step.notes[static_cast<size_t>(n)];
            const int velocity =
                std::clamp(note.velocity > 0 ? note.velocity : step.velocity, 1, 127);
            sink.addNoteEvent({.timeInBlock = event.timeInBlock,
                               .noteNumber = note.noteNumber,
                               .velocity = velocity,
                               .isNoteOn = true});
            soundingNotes_[static_cast<size_t>(n)] = note.noteNumber;
            displayVelocity = velocity;
        }
        soundingCount_ = noteCount;
        soundingVelocity_ = displayVelocity;
        tieHoldCountdown_ = 0;

        // Hold the full step when the next one ties onto this chord - the step
        // the CLOCK will play next, not stepIndex + 1, and a tie without a gate
        // is a rest here as it is in the countdown branch above (#2335).
        const int nextIndex =
            (i + 1 < eventCount) ? events[i + 1].stepIndex : clock_.getCurrentStep();
        const auto& nextStep = pattern.step(nextIndex);
        const double gateRatio =
            (nextStep.tie && nextStep.gate) ? 1.0 : static_cast<double>(gateLength);
        const int noteOnSample = static_cast<int>(event.timeInBlock * sampleRate_);
        const int gateSamples = static_cast<int>(stepDurationSamples * gateRatio);
        noteOffCountdown_ = gateSamples - (blockSamples - noteOnSample);
        if (noteOffCountdown_ <= 0) {
            const double offTime =
                std::min(event.timeInBlock + (static_cast<double>(gateSamples) / sampleRate_),
                         blockDurationSecs);
            killAllNotes(sink, offTime);
        }
    }

    // The mono engine's stuck-note guard, with the same bounded tie hold.
    if (eventCount > 0) {
        silentBlockCount_ = 0;
    } else if (soundingCount_ > 0 && noteOffCountdown_ <= 0) {
        if (tieHoldCountdown_ > 0) {
            tieHoldCountdown_ -= blockSamples;
            if (tieHoldCountdown_ <= 0)
                killAllNotes(sink, 0.0);
        } else {
            ++silentBlockCount_;
            if (silentBlockCount_ > kMaxSilentBlocks) {
                killAllNotes(sink, 0.0);
                silentBlockCount_ = 0;
            }
        }
    }

    if (eventCount == 0 && !clock_.isRunning())
        currentStep_ = -1;
}

}  // namespace magda::daw::audio::sequencer
