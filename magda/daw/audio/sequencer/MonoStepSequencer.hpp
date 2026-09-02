#pragma once

#include "sequencer/NoteSink.hpp"
#include "sequencer/StepClock.hpp"
#include "sequencer/StepPattern.hpp"

namespace magda::daw::audio::sequencer {

/**
 * @brief The 303-style monophonic sequencer: pattern and block in, notes out.
 *
 * Everything the mono step sequencer does between "the clock says step 5 fires
 * here" and "these notes sound" lives here (#2313): one voice at a time, rests,
 * ties that hold the previous note, glide that stretches a step to its full
 * length, accent velocities, and the gate countdown that releases a note some
 * blocks after the one that started it.
 *
 * The pattern is an argument, never a member: the model owns it and hands the
 * same value to whoever is playing. The device shell around this class supplies
 * the block, publishes what is sounding, and passes MIDI thru; the offline clip
 * export drives the same engine with synthetic blocks, which is what makes an
 * exported clip sound like the device.
 */
class MonoStepSequencer {
  public:
    /// Everything that can change per block, in the device's display domain.
    struct Params {
        StepClock::Rate rate = StepClock::Rate::Sixteenth;
        StepClock::Direction direction = StepClock::Direction::Forward;
        float swing = 0.0f;        ///< 0-1, delays every odd tick
        float gateLength = 0.8f;   ///< 0.05-1, fraction of a step a note holds
        int accentVelocity = 120;  ///< velocity for an accented step
        int normalVelocity = 90;   ///< velocity for an unaccented step
        float ramp = 0.0f;         ///< -1..1 bezier timing depth
        float skew = 0.0f;         ///< -1..1 bezier control-point offset
        int rampCycles = 1;        ///< 1-8 curve repetitions per pattern cycle
        bool hardAngle = false;    ///< piecewise-linear instead of bezier
        float quantize = 0.0f;     ///< 0-1 pull of warped steps toward a grid
        int quantizeSub = 16;      ///< grid subdivisions for quantize
    };

    void setSampleRate(double sampleRate);
    double sampleRate() const {
        return sampleRate_;
    }

    /**
     * Silence the voice and rewind the clock.
     *
     * A note left sounding is released through @p sink at the start of the
     * block; pass nullptr where nothing can be sounding (teardown, or a fresh
     * prepare) and it is simply forgotten.
     */
    void reset(NoteSink* sink = nullptr);

    /// Play one block of @p pattern, writing what sounds into @p sink.
    void processBlock(const StepClock::BlockTiming& timing, const MonoPattern& pattern,
                      const Params& params, NoteSink& sink);

    /// Step the pattern is on, or -1 when nothing is playing. For the UI.
    int currentStep() const {
        return currentStep_;
    }

    /// Position within the current cycle - 0 at a cycle boundary.
    int cycleStep() const {
        return clock_.getCycleStep();
    }

    bool isRunning() const {
        return clock_.isRunning();
    }

    /// The note sounding right now, or -1. For the UI's note strip.
    int soundingNote() const {
        return soundingNote_;
    }
    int soundingVelocity() const {
        return soundingVelocity_;
    }

  private:
    /// Release the sounding note at @p time and forget it.
    void killNote(NoteSink& sink, double time);

    StepClock clock_;
    double sampleRate_ = 44100.0;

    int currentStep_ = -1;
    int soundingNote_ = -1;
    int soundingVelocity_ = 0;
    int noteOffCountdown_ = 0;  ///< samples until the sounding note releases
    int silentBlockCount_ = 0;  ///< blocks with no step event, for the stuck-note guard
    /// A tie is holding the sounding note deliberately, with no release
    /// scheduled. Without this the stuck-note guard reads a held tie as a note
    /// nothing will ever release and cuts it after a few blocks.
    bool heldByTie_ = false;

    // Timing settings whose change re-anchors the grid, so a change is noticed.
    int prevRate_ = -1;
    int prevCycles_ = 1;
    bool prevHardAngle_ = false;
};

}  // namespace magda::daw::audio::sequencer
