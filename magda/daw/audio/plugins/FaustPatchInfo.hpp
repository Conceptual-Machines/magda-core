#pragma once

#include <juce_core/juce_core.h>

namespace magda::daw::audio {

/**
 * @brief Which of the two runtime Faust devices a patch belongs to.
 *
 * An effect patch and an instrument patch are not interchangeable — one
 * processes an input signal, the other synthesises from MIDI against Faust's
 * reserved freq/gain/gate voice controls. So the bundled starter library and
 * the user's saved library are both split by this, and neither device lists
 * the other's patches.
 *
 * Lives beside FaustPatchInfo so both `IFaustEditorModel` (which reports its
 * kind) and `FaustResources` (which scans per kind) can name it without either
 * depending on the other.
 */
enum class FaustPatchKind { Effect, Instrument };

/**
 * @brief Authorship metadata a .dsp declares about itself.
 *
 * Populated from the patch's global `declare author` / `version` /
 * `license` / `description`. Every field is optional and stays empty when
 * the patch omits the corresponding declare.
 *
 * Lives here rather than in FaustResources.hpp so `IFaustEditorModel` can
 * return it by value without the plugins/ headers depending back on the
 * audio root. `readPatchInfo` is implemented in FaustResources.cpp, which
 * already owns the declare-scanning helper.
 */
struct FaustPatchInfo {
    juce::String author;
    juce::String version;
    juce::String license;
    juce::String description;

    bool isEmpty() const {
        return author.isEmpty() && version.isEmpty() && license.isEmpty() && description.isEmpty();
    }
};

/// Pull the authorship declares out of .dsp source. Pure text scan, so it
/// works on source that has never been compiled, and on source that does
/// not compile at all.
FaustPatchInfo readPatchInfo(const juce::String& source);

}  // namespace magda::daw::audio
