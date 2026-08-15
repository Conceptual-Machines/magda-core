#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

// App-level OSC wiring (#1757, #2091, #2096).
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

/// One surface MAGDA has heard from (#2096), and what has passed in each
/// direction. The host is where feedback is being sent; the port is not shown,
/// because it is the one the user configured and the same for all of them.
struct Surface {
    juce::String host;
    std::uint64_t received = 0;  ///< Datagrams from it, parsed or not.
    std::uint64_t sent = 0;      ///< Feedback messages to it.
    juce::int64 lastSeenMs = 0;  ///< Wall clock, for "how long ago".
};

/// Most recently heard from first. Empty when nothing has ever talked to MAGDA,
/// which is what a silent surface's owner needs to be able to see.
std::vector<Surface> surfaces();

}  // namespace magda::osc_app
