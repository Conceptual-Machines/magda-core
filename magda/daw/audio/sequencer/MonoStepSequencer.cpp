#include "sequencer/MonoStepSequencer.hpp"

#include <algorithm>

namespace magda::daw::audio::sequencer {

namespace {

/// Step events one block can carry. A block covers a few steps at any sane
/// rate; the clock stops filling at this many.
constexpr int kMaxStepEventsPerBlock = 16;

/// Blocks a note may sound with no step event and no pending release before the
/// stuck-note guard cuts it - about 50 ms at 44.1 kHz and a 512-sample block.
constexpr int kMaxSilentBlocks = 4;

/// Note-offs land just before the note-on that replaces them, so a host that
/// sorts by timestamp cannot reorder them into a stuck note.
constexpr double kNoteOffLead = 0.0001;

}  // namespace

void MonoStepSequencer::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    clock_.setSampleRate(sampleRate_);
}

void MonoStepSequencer::reset(NoteSink* sink) {
    if (sink != nullptr)
        killNote(*sink, 0.0);
    soundingNote_ = -1;
    soundingVelocity_ = 0;
    noteOffCountdown_ = 0;
    silentBlockCount_ = 0;
    heldByTie_ = false;
    currentStep_ = -1;
    clock_.reset();
}

void MonoStepSequencer::killNote(NoteSink& sink, double time) {
    if (soundingNote_ < 0)
        return;
    sink.addNoteEvent({.timeInBlock = time, .noteNumber = soundingNote_, .isNoteOn = false});
    soundingNote_ = -1;
    soundingVelocity_ = 0;
    noteOffCountdown_ = 0;
    heldByTie_ = false;
}

void MonoStepSequencer::processBlock(const StepClock::BlockTiming& timing,
                                     const MonoPattern& pattern, const Params& params,
                                     NoteSink& sink) {
    // Stopped transport: release, rewind, and wait. The step the UI highlights
    // goes with it.
    if (!timing.isPlaying) {
        killNote(sink, 0.0);
        clock_.reset();
        currentStep_ = -1;
        silentBlockCount_ = 0;
        return;
    }

    const auto rate = params.rate;
    const auto direction = params.direction;
    const float swing = std::clamp(params.swing, 0.0f, 1.0f);
    const float gateLength = std::clamp(params.gateLength, 0.05f, 1.0f);
    const int accentVelocity = std::clamp(params.accentVelocity, 1, 127);
    const int normalVelocity = std::clamp(params.normalVelocity, 1, 127);
    const float ramp = std::clamp(params.ramp, -1.0f, 1.0f);
    const float skew = std::clamp(params.skew, -1.0f, 1.0f);
    const int rampCycles = std::clamp(params.rampCycles, 1, 8);
    const bool hardAngle = params.hardAngle;
    const float quantize = std::clamp(params.quantize, 0.0f, 1.0f);
    const int quantizeSub = std::clamp(params.quantizeSub, 16, 512);

    const int stepCount = pattern.playingLength();
    const int blockSamples = timing.numSamples;
    const double blockDurationSecs = static_cast<double>(blockSamples) / sampleRate_;

    // A step's length in samples, for the gate countdown. The block's own
    // extent gives the tempo - beats spanned over samples spent - so the
    // sequencer needs no tempo map of its own.
    const double beatSpan = timing.endBeat - timing.startBeat;
    const double samplesPerBeat = (beatSpan > 0.0 && blockSamples > 0)
                                      ? static_cast<double>(blockSamples) / beatSpan
                                      : sampleRate_ * 0.5;  // 120 bpm
    const double stepBeats = StepClock::rateToBeats(rate);
    const int stepDurationSamples = std::max(1, static_cast<int>(stepBeats * samplesPerBeat));

    // A change to the grid's structure re-anchors the clock so the pattern
    // stays in phase with the bar. Pattern length is deliberately not one of
    // these - the clock wraps into whatever length it is handed, so a pattern
    // growing under a streaming edit does not stutter. Nor are ramp depth and
    // skew, which the warp recomputes each block anyway.
    const int rateIndex = static_cast<int>(rate);
    if (rateIndex != prevRate_ || rampCycles != prevCycles_ || hardAngle != prevHardAngle_) {
        killNote(sink, 0.0);
        clock_.reset();
        prevRate_ = rateIndex;
        prevCycles_ = rampCycles;
        prevHardAngle_ = hardAngle;
    }

    StepClock::StepEvent events[kMaxStepEventsPerBlock]{};
    const int eventCount = clock_.processBlock(timing, rate, direction, swing, stepCount, events,
                                               kMaxStepEventsPerBlock, ramp, skew, rampCycles,
                                               hardAngle, quantize, quantizeSub);

    // A release scheduled by an earlier block. It only fires if no step gets
    // there first - a step that does handles the transition itself - and never
    // when the step arriving is a tie, which the sounding note has to survive.
    if (noteOffCountdown_ > 0 && soundingNote_ >= 0) {
        if (noteOffCountdown_ <= blockSamples) {
            const double countdownTime = static_cast<double>(noteOffCountdown_) / sampleRate_;
            const bool stepFiresFirst = eventCount > 0 && events[0].timeInBlock <= countdownTime;
            const bool nextStepIsTie = eventCount > 0 && pattern.step(events[0].stepIndex).tie &&
                                       pattern.step(events[0].stepIndex).gate;

            if (!stepFiresFirst && !nextStepIsTie)
                killNote(sink, countdownTime);
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
            killNote(sink, event.timeInBlock);
            continue;
        }

        // A tie holds the sounding note: no retrigger, no new countdown. The
        // next step is what releases it.
        if (step.tie && soundingNote_ >= 0) {
            noteOffCountdown_ = 0;
            heldByTie_ = true;
            continue;
        }

        if (soundingNote_ >= 0) {
            const double offTime = std::max(0.0, event.timeInBlock - kNoteOffLead);
            killNote(sink, offTime);
        }

        const int noteNumber = std::clamp(step.noteNumber + (step.octaveShift * 12), 0, 127);
        const int velocity = step.accent ? accentVelocity : normalVelocity;
        sink.addNoteEvent({.timeInBlock = event.timeInBlock,
                           .noteNumber = noteNumber,
                           .velocity = velocity,
                           .isNoteOn = true});
        soundingNote_ = noteNumber;
        soundingVelocity_ = velocity;
        heldByTie_ = false;

        // Hold the full step when this one glides into the next, or when the
        // next one ties onto this note; either way the note has to reach it.
        const auto& nextStep = pattern.step((event.stepIndex + 1) % stepCount);
        const double gateRatio =
            (step.glide || nextStep.tie) ? 1.0 : static_cast<double>(gateLength);
        const int noteOnSample = static_cast<int>(event.timeInBlock * sampleRate_);
        const int gateSamples = static_cast<int>(stepDurationSamples * gateRatio);
        noteOffCountdown_ = gateSamples - (blockSamples - noteOnSample);
        if (noteOffCountdown_ <= 0) {
            const double offTime =
                std::min(event.timeInBlock + (static_cast<double>(gateSamples) / sampleRate_),
                         blockDurationSecs);
            killNote(sink, offTime);
        }
    }

    // Stuck-note guard: a note sounding with nothing scheduled to release it
    // and no steps arriving is a note the pattern has forgotten about. A tie
    // is exactly that shape on purpose, so it is exempt - the step that ends
    // the tie is what releases the note.
    if (eventCount > 0) {
        silentBlockCount_ = 0;
    } else if (soundingNote_ >= 0 && noteOffCountdown_ <= 0 && !heldByTie_) {
        ++silentBlockCount_;
        if (silentBlockCount_ > kMaxSilentBlocks) {
            killNote(sink, 0.0);
            silentBlockCount_ = 0;
        }
    }

    if (eventCount == 0 && !clock_.isRunning())
        currentStep_ = -1;
}

}  // namespace magda::daw::audio::sequencer
