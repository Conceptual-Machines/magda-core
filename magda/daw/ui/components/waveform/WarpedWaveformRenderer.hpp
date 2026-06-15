#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "audio/AudioThumbnailManager.hpp"
#include "audio/WarpMarkerManager.hpp"  // magda::WarpMarkerInfo

namespace magda::daw::ui {

/**
 * Single source of truth for drawing a warp-mapped audio waveform.
 *
 * Both the warp editor (WaveformGridComponent) and the arrangement clip
 * (ClipComponent) render the same warped audio, just in different coordinate
 * systems. Previously each reimplemented the warp-segment math, and they drifted
 * (the arrangement skipped the tempo conversion the editor applied, and ignored
 * warp entirely for looped clips). This routine owns the segment iteration,
 * source clamping, and loop tiling once; callers supply only their own
 * warp-time -> pixel-x transform.
 *
 * Domain: marker sourceTime/warpTime are in source-file seconds (TE's warp
 * markers; identity-mapped on creation, warpTime diverges as the user drags).
 * warpToPixelX must be affine (it always is: an offset + linear scale), which is
 * what makes loop tiling a constant warp-time shift per cycle.
 */
struct WarpedWaveformSpec {
    juce::Rectangle<int> clipArea;               // pixel rect to draw within and clip to
    std::function<double(double)> warpToPixelX;  // warp-seconds -> x pixel (affine)
    double fileDuration = 0.0;                   // source clamp; 0 = unknown
    juce::Colour colour;
    float verticalScale = 1.0f;  // passed to drawWaveform (editor: vZoom, arrangement: gain)
    bool useHighRes = true;
    bool thick = false;

    // Loop tiling. When looped, the marker set repeats every cycleWarp (warp
    // seconds) until clipArea is filled. cycleWarp <= 0 falls back to one pass.
    bool looped = false;
    double cycleWarp = 0.0;
};

inline void drawWarpedWaveform(juce::Graphics& g, magda::AudioThumbnailManager& thumbs,
                               const juce::String& file, std::vector<magda::WarpMarkerInfo> markers,
                               const WarpedWaveformSpec& spec) {
    if (markers.size() < 2 || !spec.warpToPixelX || spec.clipArea.isEmpty())
        return;

    std::sort(markers.begin(), markers.end(),
              [](const auto& a, const auto& b) { return a.warpTime < b.warpTime; });

    const double leftX = spec.clipArea.getX();
    const double rightX = spec.clipArea.getRight();
    const int areaY = spec.clipArea.getY();
    const int areaH = spec.clipArea.getHeight();

    // Draw one pass of the marker set, shifted by warpShift (warp seconds).
    auto drawPass = [&](double warpShift) {
        for (size_t i = 0; i + 1 < markers.size(); ++i) {
            const double segX0 = spec.warpToPixelX(markers[i].warpTime + warpShift);
            const double segX1 = spec.warpToPixelX(markers[i + 1].warpTime + warpShift);
            const double segW = segX1 - segX0;
            if (segW <= 0.0)
                continue;

            const double visX0 = std::max(segX0, leftX);
            const double visX1 = std::min(segX1, rightX);
            if (visX1 <= visX0)
                continue;

            // Map the visible pixel sub-range back to the segment's source range.
            const double srcStart = markers[i].sourceTime;
            const double srcEnd = markers[i + 1].sourceTime;
            const double r0 = (visX0 - segX0) / segW;
            const double r1 = (visX1 - segX0) / segW;
            double cs = srcStart + r0 * (srcEnd - srcStart);
            double ce = srcStart + r1 * (srcEnd - srcStart);

            cs = std::max(0.0, cs);
            if (spec.fileDuration > 0.0)
                ce = std::min(ce, spec.fileDuration);
            if (ce <= cs)
                continue;

            const int px = (int)std::lround(visX0);
            const int pw = (int)std::lround(visX1) - px;
            if (pw <= 0)
                continue;

            thumbs.drawWaveform(g, juce::Rectangle<int>(px, areaY, pw, areaH), file, cs, ce,
                                spec.colour, spec.verticalScale, spec.useHighRes, spec.thick);
        }
    };

    if (!spec.looped || spec.cycleWarp <= 0.0) {
        drawPass(0.0);
        return;
    }

    // Pixels occupied by one loop cycle. warpToPixelX is affine, so this is a
    // constant. If a cycle is sub-pixel the repeats are invisible -- and tiling it
    // would iterate once per (sub-pixel) cycle across the whole clip, blowing the
    // count into the millions and freezing the message thread. A single pass is
    // pixel-identical, so collapse to it. (Also handles a degenerate/0 transform.)
    const double cyclePx = (spec.warpToPixelX(1.0) - spec.warpToPixelX(0.0)) * spec.cycleWarp;
    if (cyclePx < 1.0) {
        drawPass(0.0);
        return;
    }

    // Walk forward from the first cycle whose right edge has reached the left of
    // the area until the cycle's left edge passes the right of the area. At >= 1px
    // per cycle this is self-bounding -- at most one iteration per visible pixel,
    // so no arbitrary cap is needed (or wanted).
    const double frontWarp = markers.front().warpTime;
    const double backWarp = markers.back().warpTime;
    const int kStart = (int)std::floor((leftX - spec.warpToPixelX(backWarp)) / cyclePx);
    for (int k = kStart; spec.warpToPixelX(frontWarp + k * spec.cycleWarp) <= rightX; ++k)
        drawPass(k * spec.cycleWarp);
}

}  // namespace magda::daw::ui
