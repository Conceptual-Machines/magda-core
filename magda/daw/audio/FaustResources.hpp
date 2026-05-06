#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "plugins/FaustCustomViewKind.hpp"

namespace magda::daw::audio {

struct StarterDsp {
    juce::String name;      // Display name shown in the loader UI.
    juce::String filename;  // For matching against juce::BinaryData.
    juce::String source;    // The .dsp file contents.
    FaustCustomViewKind viewKind =
        FaustCustomViewKind::None;  // Bound bespoke FaustUI view, if any.
};

// Path to the Faust standard libraries directory bundled alongside the app.
// Returned File may not exist when running outside an installed bundle (e.g.
// unit tests); FaustPlugin falls back to a built-in passthrough DSP in that
// case so the plugin always loads.
juce::File getFaustLibrariesPath();

// Returns the bundled starter .dsp files (compiled into MagdaAssets via
// juce_add_binary_data).
std::vector<StarterDsp> getBundledStarterDsps();

}  // namespace magda::daw::audio
