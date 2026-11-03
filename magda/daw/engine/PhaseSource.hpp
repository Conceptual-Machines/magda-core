// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <optional>

/**
 * @file PhaseSource.hpp
 * @brief An external session's beat, read by the engine on the audio thread (#3188).
 */

namespace magda {

/** @brief A timeline the transport follows: Ableton Link's session, or an incoming MIDI clock. */
class PhaseSource {
  public:
    virtual ~PhaseSource() = default;

    /**
     * @brief The session's beat @p ahead from now, in a cycle of @p quantum beats.
     *
     * Nothing while there is no session to follow. Audio thread, and realtime-safe.
     */
    virtual std::optional<double> beatAfter(std::chrono::microseconds ahead, double quantum) = 0;

    /// Whether a start joins the session where it already is, not on its next bar (#3190).
    virtual bool joinsOnStart() const {
        return false;
    }
};

}  // namespace magda
