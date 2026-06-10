#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <functional>

#include "Controller.hpp"

namespace magda {

struct ChainNodePath;

namespace controllers {

// ============================================================================
// ControllerActivation
// ============================================================================
//
// One source of truth for "is a controller actually driving something right
// now", so every indicator (the device automap/pinned dots, the footer badge,
// the controllers dialog) derives connection / activation state the same way
// instead of each re-deriving it from raw config.
//
// Two facts make a profile mapping *live* rather than merely configured:
//   1. The controller-profile system is the active input surface. While a Lua
//      script owns the surface the profile system is suppressed (see FooterBar)
//      so its bindings must not light any indicator.
//   2. The owning controller is connected (its input port is a live MIDI
//      input), not merely enabled.

// --- Connectivity ----------------------------------------------------------

/** True if the controller's input port matches one of the given live MIDI
 *  inputs (robust identifier-or-name match). An empty inputPort never matches. */
bool isControllerConnected(const Controller& c,
                           const juce::Array<juce::MidiDeviceInfo>& liveInputs);

/** Overload that resolves the controller by id and fetches the live MIDI input
 *  list itself. Returns false for an unknown id or one with no input port. */
bool isControllerConnected(const ControllerId& id);

// --- Surface ownership -----------------------------------------------------

/** The app layer injects a provider reporting whether the controller-profile
 *  system is the active input surface (false while a Lua script owns it). Kept
 *  as an injected callback so this core module needs no scripting dependency.
 *  With no provider set, defaults to true (profile system active). */
void setProfileSurfaceActiveProvider(std::function<bool()> provider);
bool isProfileSurfaceActive();

// --- Device indicators -----------------------------------------------------

/** True when a device's green automap dot should be lit: the profile surface
 *  is active AND a resolver (automap) binding resolves here from a *connected*
 *  controller. */
bool isDeviceAutomapLive(const ChainNodePath& devicePath);

/** True when a device's orange pinned dot should be lit: the profile surface is
 *  active AND an explicit user mapping targets this device from a *connected*
 *  controller. */
bool isDeviceUserMapLive(const ChainNodePath& devicePath);

}  // namespace controllers
}  // namespace magda
