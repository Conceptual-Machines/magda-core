#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <optional>

namespace magda::midi {

// ============================================================================
// MidiDeviceMatch
// ============================================================================
//
// Platform-neutral matching between a stored controller entry (which may hold
// an opaque JUCE device identifier whose string format differs per OS) and a
// live juce::MidiDeviceInfo. The rules live here instead of being sprinkled
// across ControllerRegistry / ControllerRouter / AudioBridge as one-off
// conditionals.
//
// Storage convention
// ------------------
//   Controller::inputPort is a single field that may hold EITHER an OS-native
//   identifier (stable on the machine that wrote it) OR a display name (stable
//   across machines, less stable across driver versions / USB ports). The
//   matcher tries identifier first, then name. This lets hand-edited configs
//   survive without a new field and lets projects move between machines when
//   the device name is unchanged.
//
// Thread-safety
// -------------
//   Pure functions; no shared state.
//

/// True if a live (identifier, name) pair matches a stored key.
///
/// Match rules, in order:
///   1. storedKey == liveIdentifier (case-sensitive) -> match
///   2. storedKey equalsIgnoreCase(liveName)         -> match
///   3. otherwise                                    -> no match
///
/// Empty storedKey never matches.
bool matches(const juce::String& storedKey, const juce::String& liveIdentifier,
             const juce::String& liveName);

/// True if we matched by name, not by identifier — i.e. the stored value is a
/// display name rather than the current identifier (or the identifier drifted
/// across a driver update / reinstall). Caller can use this to self-heal by
/// writing liveIdentifier back to storage.
bool matchedByNameOnly(const juce::String& storedKey, const juce::String& liveIdentifier,
                       const juce::String& liveName);

/// Find the live device that best matches a stored key, or nullopt.
/// Prefers an identifier match over a name match; among name matches, picks
/// the first one.
std::optional<juce::MidiDeviceInfo> resolve(const juce::Array<juce::MidiDeviceInfo>& devices,
                                            const juce::String& storedKey);

}  // namespace magda::midi
