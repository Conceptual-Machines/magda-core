#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/TypeIds.hpp"

namespace magda {

/**
 * @brief Show the automation-lane menu for a track and act on the choice.
 *
 * Lists the global show/hide toggle, the track's existing lanes (as visibility
 * toggles), and an "Add New Lane..." submenu walking the track's volume/pan,
 * macros, modulators, sends, and full device chain. Works for any track,
 * including the master channel (trackId == MASTER_TRACK_ID): everything keys
 * off TrackManager::getTrack(trackId) and the device-path resolver, both of
 * which handle the master. Track Pan is omitted for the master (it has none).
 *
 * @param relativeTo Anchor component for the popup (may be null).
 * @param onShowAutomationLane Optional: called after a lane is created/shown so
 *        the host can scroll it into view. The master band updates via its own
 *        AutomationManager listener, so it can pass an empty callback.
 */
void showAutomationMenu(TrackId trackId, juce::Component* relativeTo,
                        std::function<void(TrackId, AutomationLaneId)> onShowAutomationLane = {});

}  // namespace magda
