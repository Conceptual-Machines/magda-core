#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <vector>

#include "../daw/core/ClipTypes.hpp"

namespace magda {

class MagdaApi;

/**
 * Limits for the MIDI context attached to an agent request.
 *
 * The picker may contain any number of clips, but prompt construction stays
 * bounded so selecting a whole project cannot exhaust the model context window.
 */
struct MidiContextOptions {
    std::size_t maxClips = 16;
    std::size_t maxNotesPerClip = 128;
    std::size_t maxTotalNotes = 256;
    std::size_t maxChordAnnotationsPerClip = 32;
};

/**
 * Serialize the selected MIDI clips into compact, deterministic prompt context.
 *
 * Clip ids are resolved against the live project, then ordered by track order
 * and clip order. Missing and non-MIDI clips are ignored.
 */
juce::String buildMidiContext(MagdaApi& api, const std::vector<ClipId>& clipIds,
                              const MidiContextOptions& options = {});

}  // namespace magda
