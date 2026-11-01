// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file LinkStartStop.hpp
 * @brief Which plays and stops cross between the transport and a Link session (#3189).
 */

namespace magda {

/**
 * @brief Decides whether a play or stop on one side should reach the other.
 *
 * Holds what the two last agreed on, so a change that came from the other side is not sent back.
 * Message thread.
 */
class LinkStartStop {
  public:
    /**
     * @brief While inactive, nothing crosses.
     *
     * Turning active takes the transport as agreed: joining a session starts and stops nothing
     * (Ableton's STARTSTOPSTATE-2).
     */
    void setActive(bool active) {
        if (active && !active_)
            agreed_ = transportPlaying_;
        active_ = active;
    }

    /** @brief The session reported a change. @return Whether the transport should follow. */
    bool sessionChanged(bool playing) {
        return cross(playing);
    }

    /** @brief The transport changed. @return Whether the session should hear it. */
    bool transportChanged(bool playing) {
        transportPlaying_ = playing;
        return cross(playing);
    }

    /** @brief A project load stopped the transport, which the session plays on through. */
    void transportStoppedForLoad() {
        transportPlaying_ = false;
        agreed_ = false;
    }

    bool transportPlaying() const {
        return transportPlaying_;
    }

  private:
    bool cross(bool playing) {
        if (!active_ || playing == agreed_)
            return false;
        agreed_ = playing;
        return true;
    }

    bool active_ = false;
    bool agreed_ = false;
    bool transportPlaying_ = false;
};

}  // namespace magda
