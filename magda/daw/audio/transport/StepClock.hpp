#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @brief Tempo-synced step clock used by MIDI sequencer devices.
 *
 * Handles transport state tracking, beat position resolution, step timing with
 * swing, and step advancement in multiple direction modes.
 *
 * Tracks the next step's beat position directly rather than computing from a
 * fixed grid, so rate changes (e.g. from macro modulation) only affect future
 * steps and never cause note drops.
 *
 * Used by composition — each MIDI device that needs step-based timing owns a StepClock
 * and calls processBlock() each audio buffer to get the steps that fire within that block.
 */
class StepClock {
  public:
    // --- Rate divisions ---
    enum class Rate {
        DottedQuarter = 0,
        Quarter,
        TripletQuarter,
        DottedEighth,
        Eighth,
        TripletEighth,
        DottedSixteenth,
        Sixteenth,
        TripletSixteenth,
        ThirtySecond
    };

    // --- Direction modes ---
    enum class Direction { Forward = 0, Reverse, PingPong, Random };

    // --- Step event emitted by processBlock ---
    struct StepEvent {
        int stepIndex;        // Which step fired (0-based, within sequence length)
        double beatPosition;  // Absolute beat position of this step
        double timeInBlock;   // Time offset in seconds from block start
    };

    StepClock();

    /** Reset all state (call on plugin reset or transport stop). */
    void reset();

    /** Set the sample rate (call from plugin::initialise). */
    void setSampleRate(double sr) {
        sampleRate_ = sr;
    }

    /**
     * @brief Process one audio block and return step events that fire within it.
     *
     * @param fc          Plugin render context (provides editTime, isPlaying, etc.)
     * @param edit        The edit (for tempo sequence)
     * @param rate        Current rate division
     * @param direction   Current direction mode
     * @param swing       Swing amount 0-1
     * @param numSteps    Number of active steps in the sequence
     * @param events      Output: step events that fire within this block
     * @param maxEvents   Maximum events to write
     * @return            Number of events written
     */
    int processBlock(const te::PluginRenderContext& fc, te::Edit& edit, Rate rate,
                     Direction direction, float swing, int numSteps, StepEvent* events,
                     int maxEvents, float rampDepth = 0.0f, float rampSkew = 0.0f,
                     int rampCycles = 1, bool hardAngle = false, float quantizeAmount = 0.0f,
                     int quantizeSub = 16);

    /** Current step index within the sequence (for UI display). */
    int getCurrentStep() const {
        return sequenceStep_;
    }

    /** Whether the clock is actively stepping (transport playing or notes held). */
    bool isRunning() const {
        return running_;
    }

    /** Current linear step within the cycle (0..numSteps-1, for ramp curve). */
    int getCycleStep() const {
        return cycleStep_;
    }

    /** Convert rate enum to beats per step. */
    static double rateToBeats(Rate r);

    /** Timing curve (shared by arpeggiator and step sequencer).
     *  Control point at (skew, skew+depth) in unit square.
     *  depth > 0 → front-loaded (log-like), depth < 0 → back-loaded (exp-like).
     *  skew shifts the control point horizontally (-1..1 mapped to 0.01..0.99).
     *  hardAngle = true → piecewise linear (two straight segments through control point).
     *  hardAngle = false → quadratic bezier (smooth curve through control point). */
    static double applyRampCurve(double t, float depth, float skew, bool hardAngle = false);

  private:
    double sampleRate_ = 44100.0;

    // Transport state
    bool wasPlaying_ = false;
    bool running_ = false;

    // Timing — tracks the next step beat directly (immune to rate changes)
    double nextStepBeat_ = -1.0;  // Beat position of the next step to emit (monotonic space)
    int tickParity_ = 0;          // Even/odd counter for swing

    // Monotonic beat tracking — makes the clock immune to arrangement loop wraps.
    // Edit beats wrap at arrangement loop boundaries; we accumulate an offset so
    // the step clock always sees monotonically increasing beats.
    double beatOffset_ = 0.0;
    double lastBlockEndBeat_ = 0.0;

    // Sequence state (direction-aware position within the pattern)
    int sequenceStep_ = 0;          // Current position in the pattern (0..numSteps-1)
    int cycleStep_ = 0;             // Linear step count within current cycle (for ramp curve)
    double cycleOriginBeat_ = 0.0;  // Beat position where current cycle started (monotonic space)
    bool goingUp_ = true;           // For ping-pong direction

    // Random
    juce::Random random_;

    /** Advance step index based on direction. */
    int advanceStep(int current, int numSteps, Direction dir);
};

}  // namespace magda::daw::audio
