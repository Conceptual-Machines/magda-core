// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file VoiceControl.hpp
 * @brief The AI console's mic, for callers below the UI such as controller scripts.
 */

#pragma once

#include <functional>

namespace magda::voice {

/** @brief What a caller asks of the mic. */
enum class Action {
    /// Start listening, like pressing the mic button.
    press,

    /// Stop and send what was said, like letting the button go.
    release,

    /// press or release, for a key or pad that sends one event per press.
    toggle,
};

/** @brief Set by the UI while it can take the mic; empty clears it. */
void setHandler(std::function<void(Action)> handler);

/** @brief Hand @p action to the UI, or do nothing without one. Message thread. */
void trigger(Action action);

}  // namespace magda::voice
