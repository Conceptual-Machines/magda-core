// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file remote_speech_to_text.hpp
 * @brief The speech to text clips.analyzeSpeech transcribes phrases with (#3445).
 *
 * The app installs it at startup: whisper lives in magda_agents, which depends on this library.
 */

#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

namespace magda::remote {

struct Transcription {
    juce::String text;
    juce::String error;  ///< Empty on success.
};

/// Mono @p samples at @p sampleRate to text; blocks, so never on the message thread.
using SpeechToText = std::function<Transcription(
    const std::vector<float>& samples, double sampleRate, const std::atomic<bool>* abort)>;

/** @brief Install @p speechToText before the remote API starts; an empty one removes it. */
void setSpeechToText(SpeechToText speechToText);

/** @brief The installed speech to text, or an empty function. */
SpeechToText speechToText();

}  // namespace magda::remote
