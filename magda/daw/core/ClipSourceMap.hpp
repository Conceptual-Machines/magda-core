// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <vector>

#include "ClipInfo.hpp"

namespace magda {

/**
 * @brief Which source second plays at each beat of an audio clip, at a constant tempo (#3064).
 *
 * The engine's mapping (EventPlacement.cpp: readingPositionAt, then sourceSecondsOfReading)
 * restated over the model, so the arrangement draws a source-tied curve where it plays. Speed-ramp
 * fades are left out: they bend only the clip's edges.
 */
class ClipSourceMap {
  public:
    /// Nothing for a clip with no audio event or an unusable rate or tempo.
    static std::optional<ClipSourceMap> of(const ClipInfo& clip, double projectBpm,
                                           double sourceRate);

    /// Forward source seconds at @p clipBeat, measured from the clip's start.
    double sourceSecondsAt(double clipBeat) const;

    /// Every clip beat in [@p from, @p to] where @p sourceSeconds plays, one per loop pass, found
    /// over @p steps samples of the forward map.
    std::vector<double> clipBeatsOf(double sourceSeconds, double from, double to, int steps) const;

    /// The primary event's span, in clip beats.
    double eventStartBeat() const {
        return eventStart_;
    }
    double eventEndBeat() const {
        return eventEnd_;
    }

  private:
    AudioEvent event_;
    double eventStart_ = 0.0;
    double eventEnd_ = 0.0;
    double projectBpm_ = 120.0;
    double sourceRate_ = 44100.0;

    bool beatFace_ = false;
    bool warped_ = false;
    double rate_ = 1.0;
    double extent_ = 0.0;  // material the event reads: warp time when warped

    double anchor_ = 0.0;  // in warp time when warped
    double region_ = 0.0;
    double loopStart_ = 0.0;
    double loopLength_ = 0.0;  // 0 = not looping
    double loopSourceLength_ = 0.0;
};

}  // namespace magda
