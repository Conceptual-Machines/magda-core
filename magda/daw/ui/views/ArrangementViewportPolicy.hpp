#pragma once

#include <algorithm>
#include <cmath>

namespace magda::arrangement_viewport {

/** Keep both arrangement panels covering the viewport so their empty area remains interactive. */
inline int panelHeight(int tracksHeight, int viewportHeight) {
    return std::max(tracksHeight, viewportHeight);
}

/** The vertical scrollbar is useful only when track rows extend below the viewport. */
inline bool needsVerticalScrollBar(int tracksHeight, int viewportHeight) {
    return viewportHeight > 0 && tracksHeight > viewportHeight;
}

/** Convert the zoom bar's normalized thumb position to the viewport's pixel offset. */
inline int scrollOffset(double rangeStart, double rangeHeight, int tracksHeight,
                        int viewportHeight) {
    const double thumbTravel = std::max(0.0, 1.0 - rangeHeight);
    if (thumbTravel <= 0.0)
        return 0;

    const double scrollFraction = std::clamp(rangeStart / thumbTravel, 0.0, 1.0);
    const int maxScroll = std::max(0, tracksHeight - viewportHeight);
    return static_cast<int>(std::round(scrollFraction * maxScroll));
}

}  // namespace magda::arrangement_viewport
