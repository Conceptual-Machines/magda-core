#pragma once

namespace magda {

/**
 * @brief Listener interface for audio engine to receive state changes from UI
 *
 * The audio engine implements this interface to respond to:
 * - Transport commands (play, stop, seek)
 * - Tempo/time signature changes
 * - Loop region changes
 *
 * Data flow:
 *   UI Action -> TimelineController -> AudioEngineListener -> Audio Engine
 */
class AudioEngineListener {
  public:
    virtual ~AudioEngineListener() = default;

    // ===== Transport =====

    /**
     * Called when playback should start from a specific position.
     * @param position The position in seconds to start from
     */
    virtual void onTransportPlay(double position) = 0;

    /**
     * Called when playback should stop.
     * @param returnPosition The position to return to (edit cursor position)
     */
    virtual void onTransportStop(double returnPosition) = 0;

    /**
     * Called when playback should pause (keep current position).
     */
    virtual void onTransportPause() = 0;

    /**
     * Called when recording should start from a specific position.
     * @param position The position in seconds to start recording from
     */
    virtual void onTransportRecord(double position) = 0;

    /**
     * Called when the edit position changes (user clicked timeline).
     * Audio engine should seek if not playing.
     * @param position The new edit position in seconds
     */
    virtual void onEditPositionChanged(double position) = 0;

    // ===== Tempo & Time Signature =====

    /**
     * Called when tempo changes.
     * @param bpm The new tempo in beats per minute
     */
    virtual void onTempoChanged(double bpm) = 0;

    /**
     * Called when time signature changes.
     * @param numerator Beats per bar
     * @param denominator Note value that gets one beat
     */
    virtual void onTimeSignatureChanged(int numerator, int denominator) = 0;

    // ===== Loop Region =====

    /**
     * Called when loop region changes.
     * @param startTime Loop start in seconds
     * @param endTime Loop end in seconds
     * @param enabled Whether looping is enabled
     */
    virtual void onLoopRegionChanged(double startTime, double endTime, bool enabled) = 0;

    /**
     * Called when loop enable state changes.
     * @param enabled Whether looping is enabled
     */
    virtual void onLoopEnabledChanged(bool enabled) = 0;

    // ===== Punch In/Out =====

    /**
     * Called when punch in/out region changes.
     * @param startTime Punch in position in seconds
     * @param endTime Punch out position in seconds
     * @param punchInEnabled Whether punch in is enabled
     * @param punchOutEnabled Whether punch out is enabled
     */
    virtual void onPunchRegionChanged(double startTime, double endTime, bool punchInEnabled,
                                      bool punchOutEnabled) {
        juce::ignoreUnused(startTime, endTime, punchInEnabled, punchOutEnabled);
    }

    /**
     * Called when punch in/out enable state changes.
     * @param punchInEnabled Whether punch in is enabled
     * @param punchOutEnabled Whether punch out is enabled
     */
    virtual void onPunchEnabledChanged(bool punchInEnabled, bool punchOutEnabled) {
        juce::ignoreUnused(punchInEnabled, punchOutEnabled);
    }
};

// Backwards compatibility alias
using TransportStateListener = AudioEngineListener;

}  // namespace magda
