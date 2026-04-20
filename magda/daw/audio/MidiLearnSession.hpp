#pragma once

#include <juce_core/juce_core.h>

#include "../core/controllers/Binding.hpp"
#include "../core/controllers/Controller.hpp"

namespace magda {

// ============================================================================
// LearnCapture
// ============================================================================

/**
 * @brief Snapshot of the MIDI event that triggered a learn session capture.
 *
 * Produced on the MIDI thread, delivered to the caller on the message thread
 * via a callAsync lambda inside ControllerRouter.
 */
struct LearnCapture {
    juce::String portId;
    juce::String portName;
    ControllerId controllerId;  // Id of the controller that sent the message
    BindingMsgType msgType;
    int channel = 0;  // 1..16
    int number = 0;   // CC number, note number, or 0 for pitch-bend
    int rawValue = 0;
};

// ============================================================================
// LearnSessionConfig
// ============================================================================

/**
 * @brief Configuration for a MIDI learn capture session.
 */
struct LearnSessionConfig {
    /** Minimum gap in ms between Note-on and Note-off needed to suppress the
     *  Note-off from triggering a second capture. The Note-on is always the
     *  captured event; a Note-off arriving within this window is silently dropped. */
    int captureDebounceMs = 50;

    /** When true, the captured channel is used verbatim in the binding source.
     *  When false, channel is stored as 0 (any channel). */
    bool lockChannel = false;
};

}  // namespace magda
