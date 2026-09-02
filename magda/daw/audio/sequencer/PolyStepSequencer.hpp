#pragma once

#include <array>
#include <cstdint>

#include "sequencer/NoteSink.hpp"
#include "sequencer/StepClock.hpp"
#include "sequencer/StepPattern.hpp"

namespace magda::daw::audio::sequencer {

/**
 * @brief The polyphonic sequencer: a chord per step, one chord at a time.
 *
 * The mono engine's rules with a chord in place of a note (#2313): a step's
 * notes sound together and release together, a tie holds them across the next
 * step, a rest or a failed probability roll ends them, and the gate countdown
 * releases the whole chord. Velocity comes from the step, or from a note that
 * overrides it.
 *
 * Like MonoStepSequencer the pattern is an argument, not a member: the model
 * owns it, and the device shell, the clip export and the tests all hand the
 * same value to the same engine.
 */
class PolyStepSequencer {
  public:
    /// Everything that can change per block, in the device's display domain.
    struct Params {
        StepClock::Rate rate = StepClock::Rate::Sixteenth;
        StepClock::Direction direction = StepClock::Direction::Forward;
        float swing = 0.0f;       ///< 0-1, delays every odd tick
        float gateLength = 0.8f;  ///< 0.05-1, fraction of a step a chord holds
        float ramp = 0.0f;        ///< -1..1 bezier timing depth
        float skew = 0.0f;        ///< -1..1 bezier control-point offset
        int rampCycles = 1;       ///< 1-8 curve repetitions per pattern cycle
        bool hardAngle = false;   ///< piecewise-linear instead of bezier
        float quantize = 0.0f;    ///< 0-1 pull of warped steps toward a grid
        int quantizeSub = 16;     ///< grid subdivisions for quantize
    };

    void setSampleRate(double sampleRate);
    double sampleRate() const {
        return sampleRate_;
    }

    /**
     * Silence the chord and rewind the clock.
     *
     * Sounding notes are released through @p sink at the start of the block;
     * pass nullptr where nothing can be sounding and they are forgotten.
     */
    void reset(NoteSink* sink = nullptr);

    /// Play one block of @p pattern, writing what sounds into @p sink.
    void processBlock(const StepClock::BlockTiming& timing, const PolyPattern& pattern,
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

    /// Lowest-indexed note of the sounding chord, or -1. For the note strip.
    int soundingNote() const {
        return soundingCount_ > 0 ? soundingNotes_[0] : -1;
    }
    int soundingVelocity() const {
        return soundingVelocity_;
    }
    int soundingNoteCount() const {
        return soundingCount_;
    }

    /// Seed the probability roll, so a test can pin what a pattern plays.
    void setRandomSeed(std::uint32_t seed);

  private:
    /// Release every sounding note at @p time and forget them.
    void killAllNotes(NoteSink& sink, double time);

    /// xorshift32, inline and allocation-free: this runs on the audio thread.
    float nextRandom01();

    StepClock clock_;
    double sampleRate_ = 44100.0;

    int currentStep_ = -1;
    std::array<int, kMaxNotesPerStep> soundingNotes_{};
    int soundingCount_ = 0;
    int soundingVelocity_ = 0;
    int noteOffCountdown_ = 0;  ///< samples until the sounding chord releases
    int silentBlockCount_ = 0;  ///< blocks with no step event, for the stuck-note guard
    /// A tie is holding the sounding chord deliberately, with no release
    /// scheduled (see MonoStepSequencer).
    bool heldByTie_ = false;

    std::uint32_t rngState_ = 0x9E3779B9U;

    // Timing settings whose change re-anchors the grid, so a change is noticed.
    int prevRate_ = -1;
    int prevCycles_ = 1;
    bool prevHardAngle_ = false;
};

}  // namespace magda::daw::audio::sequencer
