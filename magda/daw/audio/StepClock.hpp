#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @brief Tempo-synced step clock used by MIDI sequencer devices.
 *
 * Handles transport state tracking, beat position resolution (from edit transport
 * or free-running clock when stopped), step timing with swing, and step advancement
 * in multiple direction modes.
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
                     int maxEvents);

    /** Current step index within the sequence (for UI display). */
    int getCurrentStep() const {
        return sequenceStep_;
    }

    /** Whether the clock is actively stepping (transport playing or notes held). */
    bool isRunning() const {
        return running_;
    }

    /** Convert rate enum to beats per step. */
    static double rateToBeats(Rate r);

  private:
    double sampleRate_ = 44100.0;

    // Transport state
    bool wasPlaying_ = false;
    bool running_ = false;

    // Timing state (linear tick counter for beat position computation)
    int globalTick_ = 0;

    // Sequence state (direction-aware position within the pattern)
    int sequenceStep_ = 0;      // Current position in the pattern (0..numSteps-1)
    bool goingUp_ = true;       // For ping-pong direction
    double originBeat_ = -1.0;  // Beat position when sequence started

    // Free-running clock (for when transport is stopped)
    double freeRunSamples_ = 0.0;

    // Random
    juce::Random random_;

    /** Advance step index based on direction. */
    int advanceStep(int current, int numSteps, Direction dir);
};

}  // namespace magda::daw::audio
