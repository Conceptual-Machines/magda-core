#include "AudioEngineChoice.hpp"

#include <juce_core/juce_core.h>

#include <cstdlib>

namespace magda {

AudioEngineChoice chosenAudioEngine() {
    if (const auto* value = std::getenv("MAGDA_AUDIO_ENGINE")) {
        const auto asked = juce::String(value).trim().toLowerCase();
        if (asked == "magda")
            return AudioEngineChoice::Magda;
        if (asked == "tracktion")
            return AudioEngineChoice::Tracktion;
    }

    return AudioEngineChoice::Tracktion;
}

const char* nameOf(AudioEngineChoice choice) {
    // Named by what each engine is rather than by which one is the newcomer:
    // "native" only means anything while there is something for it to be
    // native against, and after #2557 there will not be.
    return choice == AudioEngineChoice::Magda ? "magda::engine" : "Tracktion";
}

}  // namespace magda
