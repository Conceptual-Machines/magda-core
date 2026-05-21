#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace magda::daw::audio {

// Built-in row templates for the Drum Grid. Applying a template stamps
// (label, role) pairs onto the kit's existing chains in low-note order; chains
// beyond the template's row count are left untouched, and template rows beyond
// the chain count are dropped. Chains are not created or deleted — only the
// label (Chain.name) and role (Chain.role) are rewritten.
namespace drum_grid_templates {

struct Row {
    const char* label;
    const char* role;  // canonical role id, see DrumGridRoles.hpp
};

struct Template {
    const char* name;
    const Row* rows;
    int numRows;
};

inline constexpr std::array<Row, 9> kGmRows{{
    {"Kick", "kick"},
    {"Snare", "snare"},
    {"Snare Rim", "snare-rim"},
    {"Clap", "clap"},
    {"Closed Hat", "hh-closed"},
    {"Open Hat", "hh-open"},
    {"Pedal Hat", "hh-pedal"},
    {"Ride", "ride"},
    {"Crash", "crash"},
}};

inline constexpr std::array<Row, 9> k808Rows{{
    {"Kick", "kick"},
    {"Snare", "snare"},
    {"Clap", "clap"},
    {"Closed Hat", "hh-closed"},
    {"Open Hat", "hh-open"},
    {"Tom Low", "tom-low"},
    {"Tom Mid", "tom-mid"},
    {"Tom High", "tom-high"},
    {"Cowbell", "perc-1"},
}};

inline constexpr std::array<Row, 9> kNinePadRows{{
    {"Kick", "kick"},
    {"Snare", "snare"},
    {"Clap", "clap"},
    {"Closed Hat", "hh-closed"},
    {"Open Hat", "hh-open"},
    {"Tom Low", "tom-low"},
    {"Tom High", "tom-high"},
    {"Perc 1", "perc-1"},
    {"Perc 2", "perc-2"},
}};

inline constexpr std::array<Template, 3> kBuiltIn{{
    {"GM Drum Map", kGmRows.data(), static_cast<int>(kGmRows.size())},
    {"808", k808Rows.data(), static_cast<int>(k808Rows.size())},
    {"Classic 9-Pad", kNinePadRows.data(), static_cast<int>(kNinePadRows.size())},
}};

}  // namespace drum_grid_templates
}  // namespace magda::daw::audio
