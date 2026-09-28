#include "AudioEngineChoice.hpp"

#include <juce_core/juce_core.h>

#include <cstdlib>

#include "../core/Config.hpp"

namespace magda {

AudioEngineChoice chosenAudioEngine() {
    // The variable on top of the setting, so "does it still happen on the other
    // engine" is answered by one run rather than by changing somebody's
    // preferences (#2559).
    if (const auto* value = std::getenv("MAGDA_AUDIO_ENGINE"))
        if (const auto asked = parseAudioEngine(value))
            return *asked;

    return parseAudioEngine(Config::getInstance().getAudioEngine())
        .value_or(AudioEngineChoice::Tracktion);
}

const char* settingWordFor(AudioEngineChoice choice) {
    return choice == AudioEngineChoice::Magda ? "magda" : "tracktion";
}

std::optional<AudioEngineChoice> parseAudioEngine(const juce::String& word) {
    const auto asked = word.trim().toLowerCase();
    if (asked == settingWordFor(AudioEngineChoice::Magda))
        return AudioEngineChoice::Magda;
    if (asked == settingWordFor(AudioEngineChoice::Tracktion))
        return AudioEngineChoice::Tracktion;
    return std::nullopt;
}

const char* nameOf(AudioEngineChoice choice) {
    // Named by what each engine is rather than by which one is the newcomer:
    // "native" only means anything while there is something for it to be
    // native against, and after #2557 there will not be.
    return choice == AudioEngineChoice::Magda ? "magda::engine" : "Tracktion";
}

}  // namespace magda
