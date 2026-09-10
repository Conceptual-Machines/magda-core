#pragma once

#include <juce_core/juce_core.h>

#include <optional>

/**
 * @file AudioEngineChoice.hpp
 * @brief Which engine renders, asked in one place (#2551).
 *
 * The answer has a short life: after #2557 there is one engine and this file
 * goes. Until then it is asked from a handful of places, and where it comes
 * from is going to change -- an environment variable today, a preference once
 * there is one to read (#2559).
 *
 * So the source lives behind this function and nothing else knows it. Swapping
 * the environment for a preference is a change to one body; every caller keeps
 * asking the same question.
 */

namespace magda {

enum class AudioEngineChoice { Tracktion, Magda };

/**
 * @brief What this run renders through.
 *
 * Read wherever an engine is built. Today it is MAGDA_AUDIO_ENGINE, taking
 * "magda" or "tracktion"; anything else, including nothing, is the default.
 *
 * When a preference exists it is read here, with the environment kept as the
 * override on top of it: a bug report asking "does it still happen on the
 * other engine" is then answered by one run rather than by changing somebody's
 * settings.
 */
AudioEngineChoice chosenAudioEngine();

/// The choice as a word, for the one log line that says which engine ran.
const char* nameOf(AudioEngineChoice choice);

/**
 * @brief The choice as the word the variable takes and the config file stores.
 *
 * Not @ref nameOf, which reads as a log line rather than as a value: a setting
 * and an environment variable have to be the same vocabulary, so that a report
 * quoting one is answered by typing the other (#2559).
 */
const char* settingWordFor(AudioEngineChoice choice);

/// Nothing, rather than a default, for anything that is neither word: an unset
/// variable and a hand-edited config file are different callers' decisions.
std::optional<AudioEngineChoice> parseAudioEngine(const juce::String& word);

}  // namespace magda
