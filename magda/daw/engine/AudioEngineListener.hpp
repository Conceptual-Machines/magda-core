#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * State-change sink implemented by an audio engine.
 *
 * This lives beside AudioEngine so engine implementations do not depend on
 * headers owned by the UI layer.
 */
class AudioEngineListener {
  public:
    virtual ~AudioEngineListener() = default;

    virtual void onTransportPlay(double positionSeconds) = 0;
    virtual void onTransportStop(double returnPositionSeconds) = 0;
    virtual void onTransportPause() = 0;
    virtual void onTransportRecord(double positionSeconds) = 0;
    virtual void onTransportStopRecording() = 0;
    virtual void onEditPositionChanged(double positionSeconds) = 0;

    virtual void onTempoChanged(double bpm) = 0;
    virtual void onTimeSignatureChanged(int numerator, int denominator) = 0;

    virtual void onLoopRegionChanged(double startSeconds, double endSeconds, bool enabled) = 0;
    virtual void onLoopEnabledChanged(bool enabled) = 0;

    virtual void onPunchRegionChanged(double startSeconds, double endSeconds, bool punchInEnabled,
                                      bool punchOutEnabled) {
        juce::ignoreUnused(startSeconds, endSeconds, punchInEnabled, punchOutEnabled);
    }

    virtual void onPunchEnabledChanged(bool punchInEnabled, bool punchOutEnabled) {
        juce::ignoreUnused(punchInEnabled, punchOutEnabled);
    }
};

using TransportStateListener = AudioEngineListener;

}  // namespace magda
