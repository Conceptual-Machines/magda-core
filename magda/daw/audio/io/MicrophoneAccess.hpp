// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file MicrophoneAccess.hpp
 * @brief Asking macOS about the microphone without waiting on the answer.
 */

#include <functional>

namespace magda {

/** @brief Whether the system still has to ask about the microphone, and how to ask it. */
struct MicrophoneAccess {
    /// Empty never waits, which is what other platforms have.
    std::function<bool()> undecided;

    /// Shows the prompt and returns; @p answered runs on any thread once it is answered.
    std::function<void(std::function<void()> answered)> request;
};

#if defined(__APPLE__)
/** @brief The system's own; empty for a process with no usage description to prompt with. */
MicrophoneAccess systemMicrophoneAccess();
#else
inline MicrophoneAccess systemMicrophoneAccess() {
    return {};
}
#endif

}  // namespace magda
