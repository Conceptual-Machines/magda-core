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
    // Returns the pass's leftmost mapped x so the tiler knows when it has run off
    // the right edge.
    auto drawPass = [&](double warpShift) -> double {
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
        return spec.warpToPixelX(markers.front().warpTime + warpShift);
    };

    if (!spec.looped || spec.cycleWarp <= 0.0) {
        drawPass(0.0);
        return;
    }

    // Affine transform => compute the tile index range that overlaps the area
    // directly instead of scanning from zero (cheap, and bounded).
    const double xAt0 = spec.warpToPixelX(0.0);
    const double xAt1 = spec.warpToPixelX(1.0);
    const double pxPerWarp = xAt1 - xAt0;  // pixels per warp-second
    const double cyclePx = pxPerWarp * spec.cycleWarp;
    if (std::abs(cyclePx) < 1.0e-6) {
        drawPass(0.0);
        return;
    }

    const double passX0 = spec.warpToPixelX(markers.front().warpTime);
    const double passX1 = spec.warpToPixelX(markers.back().warpTime);
    // First tile whose right edge is past leftX, last whose left edge is before rightX.
    int kStart = (int)std::floor((leftX - passX1) / cyclePx) - 1;
    int kEnd = (int)std::ceil((rightX - passX0) / cyclePx) + 1;
    if (kEnd - kStart > 100000)  // pathological zoom guard
        kEnd = kStart + 100000;

    for (int k = kStart; k <= kEnd; ++k)
        drawPass(k * spec.cycleWarp);
}

}  // namespace magda::daw::ui
