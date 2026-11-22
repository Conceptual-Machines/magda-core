// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file speech_transcriber_stub.cpp
 * @brief SpeechTranscriber for MAGDA_LOCAL_LLM=OFF builds, where nothing loads.
 */

#include "speech_transcriber.hpp"

namespace magda {

bool SpeechTranscriber::isAvailable() {
    return false;
}

namespace {
constexpr const char* kUnavailable =
    "This build of MAGDA was built without local inference, so speech input is unavailable.";
}

SpeechTranscriber::Result SpeechTranscriber::transcribe(const std::vector<float>&,
                                                        const std::string&,
                                                        const std::atomic<bool>*) {
    return {{}, kUnavailable};
}

std::string SpeechTranscriber::preload(const std::atomic<bool>*) {
    return kUnavailable;
}

void SpeechTranscriber::unload() {}

}  // namespace magda
