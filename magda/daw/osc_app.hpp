#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

// App-level OSC wiring (#1757, #2091).
//
// `OscService` and `OscFeedbackProjector` are owned by MagdaDAWApplication,
// which nothing in the UI layer can see. These read-only accessors are how the
// OSC section of ControllersDialog answers the two questions a surface that
// will not talk to MAGDA raises — is anything arriving, and is anything going
// out — without that section needing visibility into the JUCEApplication
// subclass. The same shape as `scripting_app`, and for the same reason.
//
// Settings are not written through here: both objects are `ConfigListener`s, so
// writing to `Config` and saving reconfigures them. Implementations live in
// magda_daw_main.cpp.

namespace magda::osc_app {

/// Whether a socket is bound. False when OSC is off, or when the port was
/// taken.
bool isListening();

/// The address and port actually bound, or empty / 0 when not listening. Not
/// necessarily what was configured: a port of 0 asks the OS to choose.
juce::String boundAddress();
int boundPort();

/// Messages accepted since launch. Zero while a surface is misconfigured, which
/// is the first thing worth knowing about one.
std::uint64_t acceptedMessageCount();

/// Whether feedback has a destination connected.
bool feedbackIsSending();

/// Feedback messages sent since launch.
std::uint64_t feedbackSentMessageCount();

}  // namespace magda::osc_app
