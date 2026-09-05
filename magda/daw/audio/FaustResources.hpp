#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "plugins/FaustPatchInfo.hpp"

namespace magda::daw::audio {

struct StarterDsp {
    juce::String name;      // From the file's `declare name`, else its filename.
    juce::String filename;  // File name as found on disk.
    juce::String source;    // The .dsp file contents.
    juce::String category;  // Containing folder, e.g. "texture".
};

/**
 * @brief The custom view a patch asks for via `declare magda_view "Name";`.
 *
 * Empty when the patch declares none, which is the common case. The name is a
 * key into the UI layer's view registry, so the catalogue of visuals stays
 * hardcoded C++ while the choice of one is data in the .dsp.
 *
 * Read from source on every load rather than passed in by the caller: that way
 * a file-picker load, an editor recompile and a bundled starter all resolve the
 * same way, and no call site can forget to supply it.
 */
juce::String readCustomViewName(const juce::String& source);

// Path to the Faust standard libraries directory bundled alongside the app.
// Returned File may not exist when running outside an installed bundle (e.g.
// unit tests); FaustPlugin falls back to a built-in passthrough DSP in that
// case so the plugin always loads.
juce::File getFaustLibrariesPath();

/** Refuse Faust sources that import libraries, for the whole process.
 *
 *  For test binaries that ship no faustlibraries: an importing source could
 *  only fail its compile into passthrough while still entering libfaust,
 *  which aborts on Linux under some suites (#2238). Disallowed, importing
 *  sources are passthrough by contract without touching libfaust, and
 *  self-contained sources still compile for real. Not undoable.
 */
void disallowFaustLibraryImports();

/// True once disallowFaustLibraryImports() has been called.
bool faustLibraryImportsDisallowed();

// Root of the runtime .dsp library staged alongside the app. Like
// getFaustLibrariesPath(), the returned File may not exist outside an
// installed bundle.
juce::File getFaustRuntimeDspPath();

// The staged root for one device kind: `<root>/effects` or
// `<root>/instruments`. Splitting at the top level rather than filtering one
// shared tree means a new instrument category needs no FX-side exclusion.
juce::File getFaustRuntimeDspPath(FaustPatchKind kind);

// Every .dsp discovered under getFaustRuntimeDspPath(kind), recursively.
// Nothing is hardcoded: dropping a file into that kind's staged folder is
// enough for it to appear, and each file describes itself via `declare name` /
// `declare magda_view`. Returns empty when the folder is missing.
std::vector<StarterDsp> getBundledStarterDsps(FaustPatchKind kind);

}  // namespace magda::daw::audio
