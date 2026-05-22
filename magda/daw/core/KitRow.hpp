#pragma once

#include <juce_core/juce_core.h>

namespace magda {

// One row of a drum kit: a MIDI note number paired with a human-readable label
// and a closed-vocabulary role id (see DrumGridRoles.hpp).
//
// Used in three places, all with this same shape:
//   * Per-instance kit on DeviceInfo (lives on the device, persists with the
//     project, governs what the drum grid shows for clips on that track).
//   * User-global default kit in PluginPreferences (stamped onto new instances
//     when they're created).
//   * Drumkit templates managed by DrumkitManager (importable presets on disk).
struct KitRow {
    int noteNumber = 0;
    juce::String label;
    juce::String role;
};

}  // namespace magda
