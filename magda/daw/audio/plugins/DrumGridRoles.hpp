#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace magda::daw::audio {

// Closed vocabulary of drum-row roles used by the drummer agent (#859) and the
// Drum Grid templates. Role IDs are the canonical strings written to the
// ValueTree and emitted by the agent as drum tokens. Display labels are for
// menu UI; short tags are the small badge rendered next to the row label.
namespace drum_grid_roles {

struct RoleInfo {
    const char* id;            // canonical ID, persisted + agent token
    const char* displayLabel;  // shown in "Set instrument" submenu
    const char* shortTag;      // small badge next to the row label
};

inline constexpr std::array<RoleInfo, 17> kRoles{{
    {"kick", "Kick", "K"},
    {"snare", "Snare", "S"},
    {"snare-rim", "Snare Rim", "SR"},
    {"clap", "Clap", "C"},
    {"hh-closed", "Closed Hat", "HH"},
    {"hh-open", "Open Hat", "OH"},
    {"hh-pedal", "Pedal Hat", "PH"},
    {"ride", "Ride", "R"},
    {"ride-bell", "Ride Bell", "RB"},
    {"crash", "Crash", "CR"},
    {"tom-high", "Tom High", "TH"},
    {"tom-mid", "Tom Mid", "TM"},
    {"tom-low", "Tom Low", "TL"},
    {"perc-1", "Perc 1", "P1"},
    {"perc-2", "Perc 2", "P2"},
    {"perc-3", "Perc 3", "P3"},
    {"perc-4", "Perc 4", "P4"},
}};

inline bool isValidRoleId(const juce::String& id) {
    if (id.isEmpty())
        return false;
    for (const auto& r : kRoles) {
        if (id == juce::String(r.id))
            return true;
    }
    return false;
}

inline juce::String displayLabelForRole(const juce::String& id) {
    for (const auto& r : kRoles) {
        if (id == juce::String(r.id))
            return r.displayLabel;
    }
    return {};
}

inline juce::String shortTagForRole(const juce::String& id) {
    for (const auto& r : kRoles) {
        if (id == juce::String(r.id))
            return r.shortTag;
    }
    return {};
}

}  // namespace drum_grid_roles
}  // namespace magda::daw::audio
