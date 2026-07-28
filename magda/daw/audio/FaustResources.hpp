#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "plugins/FaustCustomViewKind.hpp"

namespace magda::daw::audio {

struct StarterDsp {
    juce::String name;      // From the file's `declare name`, else its filename.
    juce::String filename;  // File name as found on disk.
    juce::String source;    // The .dsp file contents.
    juce::String category;  // Containing folder, e.g. "texture".
    FaustCustomViewKind viewKind = FaustCustomViewKind::None;  // From `declare magda_view`, if any.
};

// Path to the Faust standard libraries directory bundled alongside the app.
// Returned File may not exist when running outside an installed bundle (e.g.
// unit tests); FaustPlugin falls back to a built-in passthrough DSP in that
// case so the plugin always loads.
juce::File getFaustLibrariesPath();

// Path to the runtime .dsp library staged alongside the app. Like
// getFaustLibrariesPath(), the returned File may not exist outside an
// installed bundle.
juce::File getFaustRuntimeDspPath();

// Every .dsp discovered under getFaustRuntimeDspPath(), recursively. Nothing
// is hardcoded: dropping a file into the staged folder is enough for it to
// appear, and each file describes itself via `declare name` / `declare
// magda_view`. Returns empty when the folder is missing.
std::vector<StarterDsp> getBundledStarterDsps();

}  // namespace magda::daw::audio
