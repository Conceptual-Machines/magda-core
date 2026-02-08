#pragma once
#include <tracktion_engine/tracktion_engine.h>

namespace magda {

class MagdaEngineBehaviour : public tracktion::EngineBehaviour {
  public:
    // Disable JUCE driver timestamps for MIDI — works around a JUCE 8.0.10 bug
    // where CoreMIDI timestamps are incorrectly scaled (1e6 instead of 1e-6).
    // When false, TE uses getMillisecondCounterHiRes() which is accurate and correct.
    // TODO: Re-evaluate when upgrading to JUCE >= 8.0.11 (fix: 8b0ae502ff)
    bool isMidiDriverUsedForIncommingMessageTiming() override {
        return false;
    }
};

}  // namespace magda
