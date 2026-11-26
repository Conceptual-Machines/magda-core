// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_speech_to_text.hpp"

#include <mutex>

namespace magda::remote {
namespace {

std::mutex& lock() {
    static std::mutex mutex;
    return mutex;
}

SpeechToText& installed() {
    static SpeechToText speechToText;
    return speechToText;
}

}  // namespace

void setSpeechToText(SpeechToText speechToText) {
    const std::scoped_lock guard(lock());
    installed() = std::move(speechToText);
}

SpeechToText speechToText() {
    const std::scoped_lock guard(lock());
    return installed();
}

}  // namespace magda::remote
