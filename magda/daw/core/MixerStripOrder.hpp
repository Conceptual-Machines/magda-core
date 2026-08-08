#pragma once

#include <vector>

#include "TrackInfo.hpp"
#include "TypeIds.hpp"
#include "ViewModeState.hpp"

namespace magda {

/**
 * @brief Which tracks the mixer shows as channel strips, and in what order.
 *
 * One statement of a rule that two unrelated places need: the mixer, which
 * builds the strips, and any control surface that addresses a strip by its
 * position on screen (#1757). Those had drifted — a second, approximate copy of
 * "what a mixer strip is" meant an OSC fader could land one strip to the left
 * of the one the user was looking at.
 *
 * Three things keep a track out of the run:
 *
 *  - it is hidden in this view mode;
 *  - it is an aux, which the mixer renders in its own section rather than
 *    inline with the channel strips;
 *  - its parent group is collapsed, so the mixer is not drawing it at all.
 *
 * The master strip is not in here either. It is a separate `TrackInfo` with its
 * own place in the mixer, and it has its own OSC addresses for the same reason.
 */

/// True when `track` is drawn as an ordinary channel strip. `tracks` is the
/// list it belongs to, needed to find its parent.
bool isMixerStrip(const TrackInfo& track, const std::vector<TrackInfo>& tracks, ViewMode mode);

/// Every strip, in mixer order.
std::vector<TrackId> mixerStripOrder(const std::vector<TrackInfo>& tracks, ViewMode mode);

/**
 * @brief The track at a 1-based position in mixer order.
 *
 * `INVALID_TRACK_ID` when there is no strip there, which is the ordinary answer
 * for a surface with more faders than the project has tracks.
 *
 * Counts rather than building the list, because a control surface asks this
 * once per address per drain and the answer is one id.
 */
TrackId mixerStripAtPosition(const std::vector<TrackInfo>& tracks, ViewMode mode, int position);

}  // namespace magda
