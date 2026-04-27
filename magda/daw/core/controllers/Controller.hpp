#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace magda {

// ============================================================================
// ControllerId
// ============================================================================

using ControllerId = juce::Uuid;
using BindingId = juce::Uuid;

// ============================================================================
// Controller
// ============================================================================

/**
 * @brief Represents a physical MIDI controller device.
 *
 * Maps a MIDI input port to a named controller entry. For 0.6.0 the script
 * field is reserved for future scripting support.
 */
struct Controller {
    ControllerId id;
    juce::String name;
    juce::String vendor;
    juce::String inputPort;      // JUCE MIDI input device identifier (may go stale across boots)
    juce::String inputPortName;  // display name — used to re-resolve identifier when stale
    juce::String outputPort;     // optional MIDI output identifier
    juce::String script;         // empty for 0.6.0
    juce::String profileId;      // optional: references a ControllerProfile id; empty = no profile

    /** Returns true when the controller has a non-default id and a non-empty inputPort. */
    bool isValid() const;

    bool operator==(const Controller& other) const;
    bool operator!=(const Controller& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// JSON round-trip
// ============================================================================

/** Encode a Controller to a juce::var object suitable for JSON storage. */
juce::var encodeController(const Controller& c);

/**
 * @brief Decode a Controller from a juce::var object.
 * Returns nullopt when required fields are missing or malformed.
 */
std::optional<Controller> decodeController(const juce::var& v);

}  // namespace magda
