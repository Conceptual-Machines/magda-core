// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

namespace magda::arrangement_viewport {

/** Empty lane below the last track, one default track high, so a drop there makes a new track at
 * any scroll position (#3217). */
inline constexpr int kDropSpaceBelowTracks = 80;

/** Track rows plus the drop space below them: the height the arrangement scrolls through. */
inline int scrollHeight(int tracksHeight) {
    return tracksHeight + kDropSpaceBelowTracks;
}

/** Keep both arrangement panels covering the viewport so their empty area remains interactive. */
inline int panelHeight(int tracksHeight, int viewportHeight) {
    return std::max(scrollHeight(tracksHeight), viewportHeight);
}

/** The vertical scrollbar is useful only when the rows and drop space extend below the viewport. */
inline bool needsVerticalScrollBar(int tracksHeight, int viewportHeight) {
    return viewportHeight > 0 && scrollHeight(tracksHeight) > viewportHeight;
}

/** Convert the zoom bar's normalized thumb position to the viewport's pixel offset. */
inline int scrollOffset(double rangeStart, double rangeHeight, int tracksHeight,
                        int viewportHeight) {
    const double thumbTravel = std::max(0.0, 1.0 - rangeHeight);
    if (thumbTravel <= 0.0)
        return 0;

    const double scrollFraction = std::clamp(rangeStart / thumbTravel, 0.0, 1.0);
    const int maxScroll = std::max(0, scrollHeight(tracksHeight) - viewportHeight);
    return static_cast<int>(std::round(scrollFraction * maxScroll));
}

}  // namespace magda::arrangement_viewport
