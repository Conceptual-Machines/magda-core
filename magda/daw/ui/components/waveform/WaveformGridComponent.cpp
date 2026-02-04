#include "WaveformGridComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../timeline/TimeRuler.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipOperations.hpp"

namespace magda::daw::ui {

WaveformGridComponent::WaveformGridComponent() {
    setName("WaveformGrid");
}

void WaveformGridComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    // Background
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = getClip();
        if (clip && clip->type == magda::ClipType::Audio) {
            paintWaveform(g, *clip);
            paintClipBoundaries(g);
        } else {
            paintNoClipMessage(g);
        }
    } else {
        paintNoClipMessage(g);
    }
}

void WaveformGridComponent::paintWaveform(juce::Graphics& g, const magda::ClipInfo& clip) {
    if (clip.audioFilePath.isEmpty())
        return;

    auto layout = computeWaveformLayout(clip);
    if (layout.rect.isEmpty())
        return;

    paintWaveformBackground(g, clip, layout);
    paintWaveformThumbnail(g, clip, layout);
    paintWaveformOverlays(g, clip, layout);
}

WaveformGridComponent::WaveformLayout WaveformGridComponent::computeWaveformLayout(
    const magda::ClipInfo& clip) const {
    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return {};

    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int positionPixels = timeToPixel(displayStartTime);

    // Use source extent as the visual boundary (user's selection of source audio).
    // In loop mode with ghost disabled, show only up to the loop end within that extent.
    double sourceExtent = displayInfo_.sourceExtentSeconds;
    if (sourceExtent <= 0.0) {
        sourceExtent = clip.length;  // Fallback if not computed
    }

    double displayLength = (displayInfo_.loopEnabled && !showLoopGhost_)
                               ? std::min(sourceExtent, displayInfo_.loopEndPositionSeconds)
                               : sourceExtent;

    int widthPixels = static_cast<int>(displayLength * horizontalZoom_);
    if (widthPixels <= 0)
        return {};

    auto rect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    // clipEndPixel marks where the active content ends (same as rect end in this simplified model)
    int clipEndPixel =
        relativeMode_ ? timeToPixel(displayLength) : timeToPixel(clipStartTime_ + displayLength);

    return {rect, clipEndPixel};
}

void WaveformGridComponent::paintWaveformBackground(juce::Graphics& g, const magda::ClipInfo& clip,
                                                    const WaveformLayout& layout) {
    auto waveformRect = layout.rect;
    int clipStartPixel = relativeMode_ ? timeToPixel(0.0) : timeToPixel(clipStartTime_);
    int clipEndPixel = layout.clipEndPixel;

    auto outOfBoundsColour = clip.colour.darker(0.7f);

    // Left out-of-bounds region
    if (waveformRect.getX() < clipStartPixel) {
        int outOfBoundsWidth =
            juce::jmin(clipStartPixel - waveformRect.getX(), waveformRect.getWidth());
        auto leftOutOfBounds = waveformRect.removeFromLeft(outOfBoundsWidth);
        g.setColour(outOfBoundsColour);
        g.fillRoundedRectangle(leftOutOfBounds.toFloat(), 3.0f);
    }

    // Right out-of-bounds region
    if (waveformRect.getRight() > clipEndPixel && !waveformRect.isEmpty()) {
        int inBoundsWidth = juce::jmax(0, clipEndPixel - waveformRect.getX());
        auto inBoundsRect = waveformRect.removeFromLeft(inBoundsWidth);

        g.setColour(clip.colour.darker(0.4f));
        if (!inBoundsRect.isEmpty()) {
            g.fillRoundedRectangle(inBoundsRect.toFloat(), 3.0f);
        }

        // Draw the region beyond loop end:
        // - If ghost is enabled: show darker background (waveform drawn later)
        // - If ghost is disabled: show very dark background (no waveform)
        if (!waveformRect.isEmpty()) {
            if (showLoopGhost_) {
                g.setColour(outOfBoundsColour);
            } else {
                // Even darker for "inactive" region beyond loop end
                g.setColour(clip.colour.darker(0.85f));
            }
            g.fillRoundedRectangle(waveformRect.toFloat(), 3.0f);
        }
    } else {
        // All in bounds
        g.setColour(clip.colour.darker(0.4f));
        g.fillRoundedRectangle(waveformRect.toFloat(), 3.0f);
    }
}

void WaveformGridComponent::paintWaveformThumbnail(juce::Graphics& g, const magda::ClipInfo& clip,
                                                   const WaveformLayout& layout) {
    auto waveformRect = layout.rect;

    auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
    auto* thumbnail = thumbnailManager.getThumbnail(clip.audioFilePath);
    double fileDuration = thumbnail ? thumbnail->getTotalLength() : 0.0;
    auto waveColour = clip.colour.brighter(0.2f);
    auto vertZoom = static_cast<float>(verticalZoom_);

    g.saveState();
    if (g.reduceClipRegion(waveformRect)) {
        if (warpMode_ && !warpMarkers_.empty()) {
            paintWarpedWaveform(g, clip, waveformRect, waveColour, vertZoom);
        } else if (displayInfo_.isLooped()) {
            double loopCycle = displayInfo_.loopLengthSeconds;
            double fileStart = displayInfo_.sourceFileStart;
            double fileEnd = displayInfo_.sourceFileEnd;

            // DEBUG: Log loop drawing parameters
            static int loopLogCount = 0;
            if (++loopLogCount % 60 == 1) {
                DBG("=== LOOP WAVEFORM DRAWING ===");
                DBG("  displayInfo_.sourceFileStart=" << fileStart << "s");
                DBG("  displayInfo_.sourceFileEnd=" << fileEnd << "s");
                DBG("  displayInfo_.loopOffset=" << displayInfo_.loopOffset << "s");
                DBG("  displayInfo_.loopLengthSeconds=" << loopCycle << "s");
                DBG("  displayInfo_.sourceExtentSeconds=" << displayInfo_.sourceExtentSeconds
                                                          << "s");
                DBG("  clip.offset=" << clip.offset << "s");
                DBG("  clip.speedRatio=" << clip.speedRatio);
                DBG("  Expected ghost at loopEnd: offset + loopEnd*speed = "
                    << (clip.offset + displayInfo_.loopEndPositionSeconds * clip.speedRatio)
                    << "s");
                DBG("==============================");
            }
            bool fileClamped = false;
            if (fileDuration > 0.0 && fileEnd > fileDuration) {
                fileEnd = fileDuration;
                fileClamped = true;
            }

            // If the source audio is shorter than the loop cycle, reduce the
            // draw width to match so the thumbnail isn't stretched beyond the file.
            double actualDisplayCycle =
                fileClamped ? (fileEnd - fileStart) / clip.speedRatio : loopCycle;

            // When loop ghost is hidden, only tile up to the first loop cycle
            double tileLimit = showLoopGhost_ ? clip.length : displayInfo_.loopEndPositionSeconds;

            double timePos = 0.0;
            while (timePos < tileLimit) {
                double cycleEnd = std::min(timePos + actualDisplayCycle, tileLimit);
                int drawX = waveformRect.getX() + static_cast<int>(timePos * horizontalZoom_);
                int drawRight = waveformRect.getX() + static_cast<int>(cycleEnd * horizontalZoom_);
                auto cycleRect = juce::Rectangle<int>(drawX, waveformRect.getY(), drawRight - drawX,
                                                      waveformRect.getHeight());
                auto drawRect = cycleRect.reduced(0, 4);
                if (drawRect.getWidth() > 0 && drawRect.getHeight() > 0) {
                    // For partial tiles (last tile cut off by clip end), reduce
                    // the source range proportionally to avoid compressing the
                    // full loop cycle's audio into a shorter pixel rect.
                    double tileDuration = cycleEnd - timePos;
                    double tileFileEnd = fileEnd;
                    if (tileDuration < actualDisplayCycle - 0.0001) {
                        double fraction = tileDuration / actualDisplayCycle;
                        tileFileEnd = fileStart + (fileEnd - fileStart) * fraction;
                    }
                    thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath, fileStart,
                                                  tileFileEnd, waveColour, vertZoom);
                }
                timePos += loopCycle;
            }

        } else {
            double displayStart = displayInfo_.sourceFileStart;
            double displayEnd = displayInfo_.sourceFileEnd;
            if (fileDuration > 0.0 && displayEnd > fileDuration)
                displayEnd = fileDuration;

            double clampedDuration = (displayEnd - displayStart) / clip.speedRatio;
            int audioWidthPixels = static_cast<int>(clampedDuration * horizontalZoom_);
            auto audioRect = juce::Rectangle<int>(
                waveformRect.getX(), waveformRect.getY(),
                juce::jmin(audioWidthPixels, waveformRect.getWidth()), waveformRect.getHeight());
            auto drawRect = audioRect.reduced(0, 4);
            if (drawRect.getWidth() > 0 && drawRect.getHeight() > 0) {
                thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath, displayStart,
                                              displayEnd, waveColour, vertZoom);
            }
        }
    }
    g.restoreState();

    // When ghost is off (waveform editor) and looped, draw the remaining source audio
    // beyond the loop end so user can see and extend the loop range.
    // This must be OUTSIDE the clipped region above.
    if (displayInfo_.isLooped() && !showLoopGhost_ &&
        displayInfo_.sourceExtentSeconds > displayInfo_.loopEndPositionSeconds) {
        double remainingStart = displayInfo_.loopEndPositionSeconds;
        double remainingEnd = displayInfo_.sourceExtentSeconds;
        // Source file range for the remaining audio
        double remainingFileStart = clip.offset + remainingStart * clip.speedRatio;
        double remainingFileEnd = clip.offset + remainingEnd * clip.speedRatio;

        // DEBUG: Log ghost/remaining drawing parameters
        static int ghostLogCount = 0;
        if (++ghostLogCount % 60 == 1) {
            DBG("=== GHOST WAVEFORM DRAWING ===");
            DBG("  remainingStart (timeline)=" << remainingStart << "s");
            DBG("  remainingFileStart=" << remainingFileStart
                                        << "s (sourceStart + remainingStart/stretch)");
            DBG("  Loop sourceFileEnd was=" << displayInfo_.sourceFileEnd << "s");
            DBG("  MISMATCH? Loop ends at file " << displayInfo_.sourceFileEnd
                                                 << "s, ghost starts at file " << remainingFileStart
                                                 << "s");
            DBG("==============================");
        }
        if (fileDuration > 0.0 && remainingFileEnd > fileDuration)
            remainingFileEnd = fileDuration;

        int startX = waveformRect.getX() + static_cast<int>(remainingStart * horizontalZoom_);
        int endX = waveformRect.getX() + static_cast<int>(remainingEnd * horizontalZoom_);
        auto remainingRect = juce::Rectangle<int>(startX, waveformRect.getY(), endX - startX,
                                                  waveformRect.getHeight());
        auto drawRect = remainingRect.reduced(0, 4);
        if (drawRect.getWidth() > 0 && drawRect.getHeight() > 0) {
            // Draw dimmer to indicate it's outside the loop
            auto dimColour = waveColour.withAlpha(0.4f);
            thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath, remainingFileStart,
                                          remainingFileEnd, dimColour, vertZoom);
        }
    }
}

void WaveformGridComponent::paintWaveformOverlays(juce::Graphics& g, const magda::ClipInfo& clip,
                                                  const WaveformLayout& layout) {
    auto waveformRect = layout.rect;

    // Beat grid overlay (after waveform, before markers)
    paintBeatGrid(g, clip);

    // Transient or warp markers
    if (warpMode_ && !warpMarkers_.empty()) {
        paintWarpMarkers(g, clip);
    } else if (!warpMode_ && !transientTimes_.isEmpty()) {
        paintTransientMarkers(g, clip);
    }

    // Center line
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(waveformRect.getCentreY(), waveformRect.getX(), waveformRect.getRight());

    // Clip boundary indicator line at clip end
    if (layout.clipEndPixel > waveformRect.getX() &&
        layout.clipEndPixel < waveformRect.getRight()) {
        g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
        g.fillRect(layout.clipEndPixel - 1, waveformRect.getY(), 2, waveformRect.getHeight());
    }

    // Clip info overlay
    g.setColour(clip.colour);
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText(clip.name, waveformRect.reduced(8, 4), juce::Justification::topLeft, true);

    // Border around source block
    g.setColour(clip.colour.withAlpha(0.5f));
    g.drawRoundedRectangle(waveformRect.toFloat(), 3.0f, 1.0f);

    // Trim handles
    g.setColour(clip.colour.brighter(0.4f));
    g.fillRect(waveformRect.getX(), waveformRect.getY(), 3, waveformRect.getHeight());
    g.fillRect(waveformRect.getRight() - 3, waveformRect.getY(), 3, waveformRect.getHeight());
}

void WaveformGridComponent::paintBeatGrid(juce::Graphics& g, const magda::ClipInfo& clip) {
    if (gridResolution_ == GridResolution::Off || !timeRuler_)
        return;

    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(clip.length * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    double gridBeats = getGridResolutionBeats();
    if (gridBeats <= 0.0)
        return;

    double bpm = timeRuler_->getTempo();
    if (bpm <= 0.0)
        return;
    double secondsPerBeat = 60.0 / bpm;
    double secondsPerGrid = gridBeats * secondsPerBeat;
    double beatsPerBar = static_cast<double>(timeRuler_->getTimeSigNumerator());

    // Iterate grid lines across the clip length
    int visibleLeft = 0;
    int visibleRight = getWidth();

    for (double t = 0.0; t < clip.length + secondsPerGrid; t += secondsPerGrid) {
        double displayTime = t + displayStartTime;
        int px = timeToPixel(displayTime);

        if (px < visibleLeft || px > visibleRight)
            continue;
        if (px < waveformRect.getX() || px > waveformRect.getRight())
            continue;

        // Determine line type based on beat position
        double beatPos = t / secondsPerBeat;
        bool isBar = (std::fmod(beatPos, beatsPerBar) < 0.001);
        bool isBeat = (std::fmod(beatPos, 1.0) < 0.001);

        if (isBar) {
            g.setColour(juce::Colour(0xFF707070));
        } else if (isBeat) {
            g.setColour(juce::Colour(0xFF585858));
        } else {
            g.setColour(juce::Colour(0xFF454545));
        }

        g.drawVerticalLine(px, static_cast<float>(waveformRect.getY()),
                           static_cast<float>(waveformRect.getBottom()));
    }
}

void WaveformGridComponent::paintWarpedWaveform(juce::Graphics& g, const magda::ClipInfo& clip,
                                                juce::Rectangle<int> waveformRect,
                                                juce::Colour waveColour, float vertZoom) {
    auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
    auto* thumbnail = thumbnailManager.getThumbnail(clip.audioFilePath);
    double fileDuration = thumbnail ? thumbnail->getTotalLength() : 0.0;

    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;

    // TE's warp markers include boundary markers at (0,0) and (sourceLen,sourceLen) in source
    // file coordinates. But when a clip has sourceStart (trimmed start), the visible region
    // starts at sourceStart, not 0. We must clamp warp points to the visible source range
    // BEFORE converting to display coordinates to avoid negative display positions.

    struct WarpPoint {
        double sourceTime;
        double warpTime;
    };

    // Use pre-computed visible source range from ClipDisplayInfo
    double visibleStart = displayInfo_.sourceFileStart;
    double visibleEnd = displayInfo_.sourceFileEnd;

    // First, collect and sort all markers by warpTime
    std::vector<WarpPoint> allMarkers;
    allMarkers.reserve(warpMarkers_.size());
    for (const auto& m : warpMarkers_) {
        allMarkers.push_back({m.sourceTime, m.warpTime});
    }

    if (allMarkers.size() < 2) {
        return;
    }

    std::sort(allMarkers.begin(), allMarkers.end(),
              [](const WarpPoint& a, const WarpPoint& b) { return a.warpTime < b.warpTime; });

    // Build points list clamped to visible range, with interpolated boundaries
    std::vector<WarpPoint> points;
    points.reserve(allMarkers.size() + 2);

    // Helper lambda to interpolate a point at a given warpTime between two markers
    auto interpolateAt = [](const WarpPoint& before, const WarpPoint& after,
                            double targetWarpTime) -> WarpPoint {
        double warpDuration = after.warpTime - before.warpTime;
        if (warpDuration <= 0.0) {
            return {before.sourceTime, targetWarpTime};
        }
        double ratio = (targetWarpTime - before.warpTime) / warpDuration;
        double interpSource = before.sourceTime + ratio * (after.sourceTime - before.sourceTime);
        return {interpSource, targetWarpTime};
    };

    // Check if we need to interpolate a start boundary
    if (allMarkers.front().warpTime < visibleStart) {
        // Find the two markers that span visibleStart
        for (size_t i = 0; i + 1 < allMarkers.size(); ++i) {
            if (allMarkers[i].warpTime <= visibleStart &&
                allMarkers[i + 1].warpTime >= visibleStart) {
                if (allMarkers[i].warpTime == visibleStart) {
                    points.push_back(allMarkers[i]);
                } else {
                    points.push_back(interpolateAt(allMarkers[i], allMarkers[i + 1], visibleStart));
                }
                break;
            }
        }
    }

    // Add all markers within the visible range
    for (const auto& m : allMarkers) {
        if (m.warpTime >= visibleStart && m.warpTime <= visibleEnd) {
            // Avoid duplicating the start boundary if we just added it
            if (points.empty() || m.warpTime > points.back().warpTime) {
                points.push_back(m);
            }
        }
    }

    // Check if we need to interpolate an end boundary
    if (allMarkers.back().warpTime > visibleEnd) {
        // Find the two markers that span visibleEnd
        for (size_t i = 0; i + 1 < allMarkers.size(); ++i) {
            if (allMarkers[i].warpTime <= visibleEnd && allMarkers[i + 1].warpTime >= visibleEnd) {
                if (allMarkers[i + 1].warpTime == visibleEnd) {
                    if (points.empty() || points.back().warpTime < visibleEnd) {
                        points.push_back(allMarkers[i + 1]);
                    }
                } else {
                    auto interpPoint = interpolateAt(allMarkers[i], allMarkers[i + 1], visibleEnd);
                    if (points.empty() || points.back().warpTime < visibleEnd) {
                        points.push_back(interpPoint);
                    }
                }
                break;
            }
        }
    }

    // Need at least 2 points to draw segments
    if (points.size() < 2) {
        return;
    }

    // Draw each segment between consecutive warp points
    // Now all warpTimes are within [visibleStart, visibleEnd], so display coords will be valid
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        double srcStart = points[i].sourceTime;
        double srcEnd = points[i + 1].sourceTime;
        double warpStart = points[i].warpTime;
        double warpEnd = points[i + 1].warpTime;

        // Convert warp times to clip-relative display times
        // Since warpTimes are now >= offset, dispStart will be >= displayStartTime
        double dispStart = (warpStart - clip.offset) + displayStartTime;
        double dispEnd = (warpEnd - clip.offset) + displayStartTime;

        int pixStart = timeToPixel(dispStart);
        int pixEnd = timeToPixel(dispEnd);
        int segWidth = pixEnd - pixStart;
        if (segWidth <= 0)
            continue;

        auto segRect =
            juce::Rectangle<int>(pixStart, waveformRect.getY(), segWidth, waveformRect.getHeight());

        // Clip to waveform bounds (for edge cases at display boundaries)
        auto clippedRect = segRect.getIntersection(waveformRect);
        if (clippedRect.isEmpty())
            continue;

        // Adjust source range if clipping occurred
        double srcDuration = srcEnd - srcStart;
        double clippedSrcStart = srcStart;
        double clippedSrcEnd = srcEnd;

        if (clippedRect != segRect && segWidth > 0 && srcDuration > 0.0) {
            int clippedFromLeft = clippedRect.getX() - segRect.getX();
            int clippedFromRight = segRect.getRight() - clippedRect.getRight();

            double leftRatio = static_cast<double>(clippedFromLeft) / segWidth;
            double rightRatio = static_cast<double>(clippedFromRight) / segWidth;

            clippedSrcStart = srcStart + srcDuration * leftRatio;
            clippedSrcEnd = srcEnd - srcDuration * rightRatio;
        }

        auto drawRect = clippedRect.reduced(0, 4);
        if (drawRect.getWidth() > 0 && drawRect.getHeight() > 0) {
            // Clamp source range to file duration
            double finalSrcStart = juce::jmax(0.0, clippedSrcStart);
            double finalSrcEnd =
                fileDuration > 0.0 ? juce::jmin(clippedSrcEnd, fileDuration) : clippedSrcEnd;
            if (finalSrcEnd > finalSrcStart) {
                thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath, finalSrcStart,
                                              finalSrcEnd, waveColour, vertZoom);
            }
        }
    }
}

void WaveformGridComponent::paintClipBoundaries(juce::Graphics& g) {
    if (clipLength_ <= 0.0) {
        return;
    }

    auto bounds = getLocalBounds();
    bool isLooped = displayInfo_.isLooped();

    // Use theme's loop marker colour (green)
    auto loopColour = DarkTheme::getColour(DarkTheme::LOOP_MARKER);

    if (!relativeMode_) {
        // Absolute mode
        // Only show clip boundaries if NOT in loop mode
        if (!isLooped) {
            int clipStartX = timeToPixel(clipStartTime_);
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
            g.fillRect(clipStartX - 1, 0, 2, bounds.getHeight());

            int clipEndX = timeToPixel(clipStartTime_ + clipLength_);
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
            g.fillRect(clipEndX - 1, 0, 3, bounds.getHeight());
        }

        // Loop boundaries (green) - show loop start and end
        if (isLooped) {
            // Loop start marker at display position 0 (where loop begins)
            int loopStartX = timeToPixel(clipStartTime_);
            g.setColour(loopColour.withAlpha(0.8f));
            g.fillRect(loopStartX - 1, 0, 2, bounds.getHeight());

            // Loop end marker
            int loopEndX = timeToPixel(clipStartTime_ + displayInfo_.loopEndPositionSeconds);
            g.setColour(loopColour.withAlpha(0.8f));
            g.fillRect(loopEndX - 1, 0, 3, bounds.getHeight());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText("L", loopEndX + 3, 2, 12, 12, juce::Justification::centredLeft, false);
        }
    } else {
        // Relative mode
        // Only show clip boundaries if NOT in loop mode
        if (!isLooped) {
            int clipStartX = timeToPixel(0.0);
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
            g.fillRect(clipStartX - 1, 0, 2, bounds.getHeight());

            int clipEndX = timeToPixel(clipLength_);
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
            g.fillRect(clipEndX - 1, 0, 3, bounds.getHeight());
        }

        // Loop boundaries (green) - show loop start and end
        if (isLooped) {
            // Loop start marker at 0
            int loopStartX = timeToPixel(0.0);
            g.setColour(loopColour.withAlpha(0.8f));
            g.fillRect(loopStartX - 1, 0, 2, bounds.getHeight());

            // Loop end marker
            int loopEndX = timeToPixel(displayInfo_.loopEndPositionSeconds);
            g.setColour(loopColour.withAlpha(0.8f));
            g.fillRect(loopEndX - 1, 0, 3, bounds.getHeight());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText("L", loopEndX + 3, 2, 12, 12, juce::Justification::centredLeft, false);
        }
    }
}

void WaveformGridComponent::paintTransientMarkers(juce::Graphics& g, const magda::ClipInfo& clip) {
    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(clip.length * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    g.setColour(juce::Colours::white.withAlpha(0.25f));

    bool isLooped = displayInfo_.isLooped();

    // Visible pixel range for culling
    int visibleLeft = 0;
    int visibleRight = getWidth();

    auto drawMarkersForCycle = [&](double cycleOffset, double sourceStart, double sourceEnd) {
        for (double t : transientTimes_) {
            if (t < sourceStart || t >= sourceEnd)
                continue;

            // Convert source time to display time
            double displayTime = (t - sourceStart) / clip.speedRatio + cycleOffset;
            double absDisplayTime = displayTime + displayStartTime;
            int px = timeToPixel(absDisplayTime);

            // Cull outside visible bounds
            if (px < visibleLeft || px > visibleRight)
                continue;

            // Cull outside waveform rect
            if (px < waveformRect.getX() || px > waveformRect.getRight())
                continue;

            g.drawVerticalLine(px, static_cast<float>(waveformRect.getY()),
                               static_cast<float>(waveformRect.getBottom()));
        }
    };

    if (isLooped) {
        double loopCycle = displayInfo_.loopLengthSeconds;
        double fileStart = displayInfo_.sourceFileStart;
        double fileEnd = displayInfo_.sourceFileEnd;

        double markerLimit = showLoopGhost_ ? clip.length : displayInfo_.loopEndPositionSeconds;
        double timePos = 0.0;
        while (timePos < markerLimit) {
            drawMarkersForCycle(timePos, fileStart, fileEnd);
            timePos += loopCycle;
        }
    } else {
        double sourceStart = displayInfo_.sourceFileStart;
        double sourceEnd = displayInfo_.sourceFileEnd;
        drawMarkersForCycle(0.0, sourceStart, sourceEnd);
    }
}

void WaveformGridComponent::paintNoClipMessage(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(14.0f));
    g.drawText("No audio clip selected", bounds, juce::Justification::centred, false);
}

void WaveformGridComponent::resized() {
    // Grid size is managed by updateGridSize()
}

// ============================================================================
// Configuration
// ============================================================================

void WaveformGridComponent::setClip(magda::ClipId clipId) {
    editingClipId_ = clipId;
    transientTimes_.clear();

    // Always update clip info (even if same clip, properties may have changed)
    const auto* clip = getClip();
    if (clip) {
        clipStartTime_ = clip->startTime;
        clipLength_ = clip->length;
    } else {
        clipStartTime_ = 0.0;
        clipLength_ = 0.0;
    }

    updateGridSize();
    repaint();
}

void WaveformGridComponent::setRelativeMode(bool relative) {
    if (relativeMode_ != relative) {
        relativeMode_ = relative;
        updateGridSize();
        repaint();
    }
}

void WaveformGridComponent::setHorizontalZoom(double pixelsPerSecond) {
    if (horizontalZoom_ != pixelsPerSecond) {
        horizontalZoom_ = pixelsPerSecond;
        updateGridSize();
        repaint();
    }
}

void WaveformGridComponent::setVerticalZoom(double zoom) {
    if (verticalZoom_ != zoom) {
        verticalZoom_ = zoom;
        repaint();
    }
}

void WaveformGridComponent::updateClipPosition(double startTime, double length) {
    // Don't update cached values during a drag — they serve as the stable
    // reference for delta calculations.  Updating mid-drag causes a feedback
    // loop where each drag step compounds on the previous one.
    if (dragMode_ != DragMode::None)
        return;

    clipStartTime_ = startTime;
    clipLength_ = length;
    updateGridSize();
    repaint();
}

void WaveformGridComponent::setDisplayInfo(const magda::ClipDisplayInfo& info) {
    displayInfo_ = info;
    repaint();
}

void WaveformGridComponent::setTransientTimes(const juce::Array<double>& times) {
    transientTimes_ = times;
    repaint();
}

void WaveformGridComponent::setGridResolution(GridResolution resolution) {
    if (gridResolution_ != resolution) {
        gridResolution_ = resolution;
        repaint();
    }
}

GridResolution WaveformGridComponent::getGridResolution() const {
    return gridResolution_;
}

void WaveformGridComponent::setTimeRuler(magda::TimeRuler* ruler) {
    timeRuler_ = ruler;
    repaint();
}

double WaveformGridComponent::getGridResolutionBeats() const {
    switch (gridResolution_) {
        case GridResolution::Bar:
            return timeRuler_ ? static_cast<double>(timeRuler_->getTimeSigNumerator()) : 4.0;
        case GridResolution::Beat:
            return 1.0;
        case GridResolution::Eighth:
            return 0.5;
        case GridResolution::Sixteenth:
            return 0.25;
        case GridResolution::ThirtySecond:
            return 0.125;
        case GridResolution::Off:
        default:
            return 0.0;
    }
}

double WaveformGridComponent::snapTimeToGrid(double time) const {
    double beatsPerGrid = getGridResolutionBeats();
    double bpm = timeRuler_ ? timeRuler_->getTempo() : 0.0;
    if (beatsPerGrid <= 0.0 || bpm <= 0.0) {
        return time;
    }
    double secondsPerGrid = beatsPerGrid * 60.0 / bpm;
    double snapped = std::round(time / secondsPerGrid) * secondsPerGrid;
    return snapped;
}

void WaveformGridComponent::setShowLoopGhost(bool show) {
    if (showLoopGhost_ != show) {
        showLoopGhost_ = show;
        updateGridSize();
        repaint();
    }
}

void WaveformGridComponent::setWarpMode(bool enabled) {
    if (warpMode_ != enabled) {
        warpMode_ = enabled;
        hoveredMarkerIndex_ = -1;
        draggingMarkerIndex_ = -1;
        if (!enabled) {
            warpMarkers_.clear();
        }
        repaint();
    }
}

void WaveformGridComponent::setWarpMarkers(
    const std::vector<magda::AudioBridge::WarpMarkerInfo>& markers) {
    warpMarkers_ = markers;
    repaint();
}

void WaveformGridComponent::setScrollOffset(int x, int y) {
    scrollOffsetX_ = x;
    scrollOffsetY_ = y;
}

void WaveformGridComponent::setMinimumHeight(int height) {
    if (minimumHeight_ != height) {
        minimumHeight_ = juce::jmax(100, height);
        updateGridSize();
    }
}

void WaveformGridComponent::updateGridSize() {
    const auto* clip = getClip();
    if (!clip) {
        setSize(800, 400);  // Default size when no clip
        return;
    }

    // Use source extent as the visual boundary (user's selection of source audio).
    // In loop mode with ghost disabled, show only up to the loop end within that extent.
    double sourceExtent = displayInfo_.sourceExtentSeconds;
    if (sourceExtent <= 0.0) {
        sourceExtent = clipLength_;  // Fallback if not computed
    }

    double displayClipLength = (displayInfo_.loopEnabled && !showLoopGhost_)
                                   ? std::min(sourceExtent, displayInfo_.loopEndPositionSeconds)
                                   : sourceExtent;

    // Calculate required width based on mode
    double totalTime = 0.0;
    if (relativeMode_) {
        // In relative mode, show clip length + right padding
        totalTime = displayClipLength + 10.0;  // 10 seconds right padding
    } else {
        // In absolute mode, show from 0 to clip end + both left and right padding
        // Add left padding so we can scroll before clip start
        double leftPaddingTime =
            std::max(10.0, clipStartTime_ * 0.5);  // At least 10s or half the clip start time
        totalTime = clipStartTime_ + displayClipLength + 10.0 + leftPaddingTime;
    }

    int requiredWidth =
        static_cast<int>(totalTime * horizontalZoom_) + LEFT_PADDING + RIGHT_PADDING;
    int requiredHeight = minimumHeight_;

    setSize(requiredWidth, requiredHeight);
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

int WaveformGridComponent::timeToPixel(double time) const {
    return static_cast<int>(time * horizontalZoom_) + LEFT_PADDING;
}

double WaveformGridComponent::pixelToTime(int x) const {
    return (x - LEFT_PADDING) / horizontalZoom_;
}

// ============================================================================
// Mouse Interaction
// ============================================================================

void WaveformGridComponent::mouseDown(const juce::MouseEvent& event) {
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        return;
    }

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->type != magda::ClipType::Audio || clip->audioFilePath.isEmpty()) {
        return;
    }

    int x = event.x;
    bool shiftHeld = event.mods.isShiftDown();

    // Warp mode interaction
    if (warpMode_) {
        // Right-click on marker: remove it
        if (event.mods.isPopupMenu()) {
            int markerIndex = findMarkerAtPixel(x);
            if (markerIndex >= 0 && onWarpMarkerRemove) {
                onWarpMarkerRemove(markerIndex);
            }
            return;
        }

        // Check if clicking on an existing marker to drag it
        int markerIndex = findMarkerAtPixel(x);
        if (markerIndex >= 0) {
            dragMode_ = DragMode::MoveWarpMarker;
            draggingMarkerIndex_ = markerIndex;
            dragStartWarpTime_ = warpMarkers_[static_cast<size_t>(markerIndex)].warpTime;
            dragStartX_ = x;
            return;
        }

        // Click on waveform in warp mode: add new marker
        // Markers are placed exactly where clicked (at transient positions).
        // Grid snapping only applies when MOVING markers, not when placing them.
        if (isInsideWaveform(x, *clip)) {
            double clickTime = pixelToTime(x);
            // Convert from display time to clip-relative time
            double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
            double clipRelativeTime = clickTime - displayStartTime;

            // Convert to absolute warp time (no snapping on placement)
            double warpTime = clipRelativeTime + clip->offset;

            // Find the corresponding sourceTime by interpolating from existing markers.
            // The warp curve maps warpTime -> sourceTime, so we need to find what
            // source position is currently playing at this warp time.
            double sourceTime = warpTime;  // Default to identity if no markers

            if (warpMarkers_.size() >= 2) {
                // Sort markers by warpTime to find the segment containing our click
                std::vector<std::pair<double, double>> sorted;  // (warpTime, sourceTime)
                for (const auto& m : warpMarkers_) {
                    sorted.push_back({m.warpTime, m.sourceTime});
                }
                std::sort(sorted.begin(), sorted.end());

                // Find the two markers that span our warpTime
                for (size_t i = 0; i + 1 < sorted.size(); ++i) {
                    if (sorted[i].first <= warpTime && sorted[i + 1].first >= warpTime) {
                        double warpDuration = sorted[i + 1].first - sorted[i].first;
                        if (warpDuration > 0.0) {
                            double ratio = (warpTime - sorted[i].first) / warpDuration;
                            sourceTime = sorted[i].second +
                                         ratio * (sorted[i + 1].second - sorted[i].second);
                        } else {
                            sourceTime = sorted[i].second;
                        }
                        break;
                    }
                }
            }

            if (onWarpMarkerAdd) {
                onWarpMarkerAdd(sourceTime, warpTime);
            }
        }
        return;
    }

    // Non-warp mode: standard trim/stretch interaction
    if (isNearLeftEdge(x, *clip)) {
        dragMode_ = shiftHeld ? DragMode::StretchLeft : DragMode::ResizeLeft;
    } else if (isNearRightEdge(x, *clip)) {
        dragMode_ = shiftHeld ? DragMode::StretchRight : DragMode::ResizeRight;
    } else if (isInsideWaveform(x, *clip)) {
        // Inside waveform but not near edges — no drag (removed Move mode)
        dragMode_ = DragMode::None;
        return;
    } else {
        dragMode_ = DragMode::None;
        return;
    }

    dragStartX_ = x;
    dragStartAudioOffset_ = clip->offset;
    dragStartStartTime_ = clip->startTime;
    dragStartSpeedRatio_ = clip->speedRatio;

    // Use source extent for resize operations (visual boundary in waveform editor)
    // This may differ from clip.length in loop mode
    dragStartLength_ = displayInfo_.sourceExtentSeconds;
    if (dragStartLength_ <= 0.0) {
        dragStartLength_ = clip->length;  // Fallback
    }

    // Cache file duration for trim clamping
    dragStartFileDuration_ = 0.0;
    auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(clip->audioFilePath);
    if (thumbnail) {
        dragStartFileDuration_ = thumbnail->getTotalLength();
    }
}

void WaveformGridComponent::mouseDrag(const juce::MouseEvent& event) {
    if (dragMode_ == DragMode::None) {
        return;
    }
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        return;
    }

    // Warp marker drag
    if (dragMode_ == DragMode::MoveWarpMarker) {
        auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip)
            return;

        // Pixel delta → warp-time delta (no speedRatio — warp owns the timing)
        double timeDelta = (event.x - dragStartX_) / horizontalZoom_;
        double newWarpTime = dragStartWarpTime_ + timeDelta;
        if (newWarpTime < clip->offset)
            newWarpTime = clip->offset;

        // Snap to grid unless Alt is held
        // Must snap in clip-relative coordinates to align with visual grid lines
        if (!event.mods.isAltDown() && gridResolution_ != GridResolution::Off) {
            double clipRelTime = newWarpTime - clip->offset;
            clipRelTime = snapTimeToGrid(clipRelTime);
            newWarpTime = clipRelTime + clip->offset;
        }

        if (draggingMarkerIndex_ >= 0 && onWarpMarkerMove) {
            onWarpMarkerMove(draggingMarkerIndex_, newWarpTime);
        }
        return;
    }

    // Get clip for direct modification during drag (performance optimization)
    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->audioFilePath.isEmpty())
        return;

    double deltaSeconds = (event.x - dragStartX_) / horizontalZoom_;

    // Calculate absolute values from original drag start values
    switch (dragMode_) {
        case DragMode::ResizeLeft: {
            // Content-level trim: change offset (file start point) and clamp length
            // to available audio content.
            double fileDelta = deltaSeconds * dragStartSpeedRatio_;
            double newOffset = dragStartAudioOffset_ + fileDelta;

            // Constrain to file bounds
            if (dragStartFileDuration_ > 0.0) {
                newOffset = juce::jmin(newOffset, dragStartFileDuration_);
            }
            newOffset = juce::jmax(0.0, newOffset);

            DBG("=== RESIZE LEFT (Editor) ===");
            DBG("  BEFORE: offset=" << clip->offset << ", loopStart=" << clip->loopStart);
            DBG("  fileDelta=" << fileDelta << ", newOffset=" << newOffset);

            clip->offset = newOffset;

            DBG("  AFTER: offset=" << clip->offset << ", loopStart=" << clip->loopStart);
            DBG("============================");

            // Clamp clip length to available audio (for non-looped clips)
            if (dragStartFileDuration_ > 0.0 && !clip->loopEnabled) {
                double maxLength = (dragStartFileDuration_ - newOffset) / clip->speedRatio;
                if (clip->length > maxLength) {
                    clip->length = juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, maxLength);
                }
            }
            break;
        }
        case DragMode::ResizeRight: {
            // Calculate new source extent (in timeline seconds)
            double newExtent = dragStartLength_ + deltaSeconds;
            newExtent = juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, newExtent);

            // Constrain to file bounds
            if (dragStartFileDuration_ > 0.0) {
                double maxExtent =
                    (dragStartFileDuration_ - dragStartAudioOffset_) / dragStartSpeedRatio_;
                newExtent = juce::jmin(newExtent, maxExtent);
            }

            DBG("=== RESIZE RIGHT (Editor) ===");
            DBG("  dragStartLength_=" << dragStartLength_ << ", deltaSeconds=" << deltaSeconds);
            DBG("  newExtent=" << newExtent << " (timeline seconds)");
            DBG("  BEFORE: loopLength=" << clip->loopLength << ", clip->length=" << clip->length);

            // Set loopLength (in source-file seconds, not timeline seconds)
            clip->loopLength = newExtent * clip->speedRatio;

            // For non-looped clips, also update clip.length to match
            if (!clip->loopEnabled) {
                clip->length = newExtent;
            }

            DBG("  AFTER: loopLength=" << clip->loopLength << ", clip->length=" << clip->length);
            DBG("=============================");
            break;
        }
        case DragMode::StretchRight: {
            double newLength = dragStartLength_ + deltaSeconds;
            magda::ClipOperations::stretchAudioFromRight(*clip, newLength, dragStartLength_,
                                                         dragStartSpeedRatio_);
            break;
        }
        case DragMode::StretchLeft: {
            double newLength = dragStartLength_ - deltaSeconds;
            magda::ClipOperations::stretchAudioFromLeft(*clip, newLength, dragStartLength_,
                                                        dragStartSpeedRatio_);
            break;
        }
        default:
            break;
    }

    // Rebuild displayInfo_ immediately so paint uses consistent values
    // (the throttled notification from WaveformEditorContent would otherwise
    // leave displayInfo_ stale relative to the clip we just modified).
    {
        double bpm = timeRuler_ ? timeRuler_->getTempo() : 120.0;
        displayInfo_ = magda::ClipDisplayInfo::from(*clip, bpm);
        clipLength_ = clip->length;
        clipStartTime_ = clip->startTime;
    }

    // Repaint locally for immediate feedback
    repaint();

    // Throttled notification to update arrangement view (every 50ms)
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    if (currentTime - lastDragUpdateTime_ >= DRAG_UPDATE_INTERVAL_MS) {
        lastDragUpdateTime_ = currentTime;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    }

    if (onWaveformChanged) {
        onWaveformChanged();
    }
}

void WaveformGridComponent::mouseUp(const juce::MouseEvent& /*event*/) {
    if (dragMode_ == DragMode::MoveWarpMarker) {
        draggingMarkerIndex_ = -1;
        dragMode_ = DragMode::None;
        return;
    }

    if (dragMode_ != DragMode::None && editingClipId_ != magda::INVALID_CLIP_ID) {
        // Clear drag mode BEFORE notifying so that updateClipPosition() can
        // update the cached values with the final clip state.
        dragMode_ = DragMode::None;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    } else {
        dragMode_ = DragMode::None;
    }
}

void WaveformGridComponent::mouseMove(const juce::MouseEvent& event) {
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto* clip = getClip();
    if (!clip || clip->audioFilePath.isEmpty()) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    int x = event.x;

    // Warp mode: update hover state
    if (warpMode_) {
        int newHovered = findMarkerAtPixel(x);
        if (newHovered != hoveredMarkerIndex_) {
            hoveredMarkerIndex_ = newHovered;
            repaint();
        }

        if (newHovered >= 0) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        } else if (isInsideWaveform(x, *clip)) {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
        return;
    }

    if (isNearLeftEdge(x, *clip) || isNearRightEdge(x, *clip)) {
        if (event.mods.isShiftDown()) {
            setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        } else {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// ============================================================================
// Hit Testing Helpers
// ============================================================================

bool WaveformGridComponent::isNearLeftEdge(int x, const magda::ClipInfo& clip) const {
    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int leftEdgeX = timeToPixel(displayStartTime);
    juce::ignoreUnused(clip);
    return std::abs(x - leftEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isNearRightEdge(int x, const magda::ClipInfo& clip) const {
    juce::ignoreUnused(clip);
    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;

    // Use source extent as the visual boundary
    double sourceExtent = displayInfo_.sourceExtentSeconds;
    if (sourceExtent <= 0.0) {
        sourceExtent = clip.length;  // Fallback
    }

    // In loop mode with ghost disabled, use loop end; otherwise source extent
    double rightEdgeTime = (displayInfo_.loopEnabled && !showLoopGhost_)
                               ? std::min(sourceExtent, displayInfo_.loopEndPositionSeconds)
                               : sourceExtent;
    int rightEdgeX = timeToPixel(displayStartTime + rightEdgeTime);
    return std::abs(x - rightEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isInsideWaveform(int x, const magda::ClipInfo& clip) const {
    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int leftEdgeX = timeToPixel(displayStartTime);

    // Use source extent as the visual boundary
    double sourceExtent = displayInfo_.sourceExtentSeconds;
    if (sourceExtent <= 0.0) {
        sourceExtent = clip.length;  // Fallback
    }

    // In loop mode with ghost disabled, use loop end; otherwise source extent
    double rightEdgeTime = (displayInfo_.loopEnabled && !showLoopGhost_)
                               ? std::min(sourceExtent, displayInfo_.loopEndPositionSeconds)
                               : sourceExtent;
    int rightEdgeX = timeToPixel(displayStartTime + rightEdgeTime);
    return x > leftEdgeX + EDGE_GRAB_DISTANCE && x < rightEdgeX - EDGE_GRAB_DISTANCE;
}

// ============================================================================
// Private Helpers
// ============================================================================

const magda::ClipInfo* WaveformGridComponent::getClip() const {
    return magda::ClipManager::getInstance().getClip(editingClipId_);
}

// ============================================================================
// Warp Marker Painting
// ============================================================================

void WaveformGridComponent::paintWarpMarkers(juce::Graphics& g, const magda::ClipInfo& clip) {
    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(clip.length * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    int visibleLeft = 0;
    int visibleRight = getWidth();

    // Skip first and last markers (TE's boundary markers at 0 and sourceLen)
    // Only draw user-created markers
    int numMarkers = static_cast<int>(warpMarkers_.size());
    for (int i = 1; i < numMarkers - 1; ++i) {
        const auto& marker = warpMarkers_[static_cast<size_t>(i)];

        // Warp time is in the playback coordinate space — no speedRatio needed.
        // Subtract offset to get clip-relative time.
        double clipRelativeTime = marker.warpTime - clip.offset;
        if (clipRelativeTime < 0.0 || clipRelativeTime > clip.length * 2.0)
            continue;

        double displayTime = clipRelativeTime + displayStartTime;
        int px = timeToPixel(displayTime);

        // Cull outside visible bounds and waveform rect
        if (px < visibleLeft || px > visibleRight)
            continue;
        if (px < waveformRect.getX() || px > waveformRect.getRight())
            continue;

        // Determine colour: hovered marker is brighter
        bool isHovered = (i == hoveredMarkerIndex_);
        bool isDragging = (i == draggingMarkerIndex_);
        auto markerColour = juce::Colours::yellow;

        if (isDragging) {
            markerColour = markerColour.brighter(0.3f);
        } else if (isHovered) {
            markerColour = markerColour.brighter(0.15f);
        } else {
            markerColour = markerColour.withAlpha(0.7f);
        }

        // Draw vertical line (2px wide)
        g.setColour(markerColour);
        g.fillRect(px - 1, waveformRect.getY(), 2, waveformRect.getHeight());

        // Draw small triangle handle at top
        juce::Path triangle;
        float fx = static_cast<float>(px);
        float fy = static_cast<float>(waveformRect.getY());
        triangle.addTriangle(fx - 4.0f, fy, fx + 4.0f, fy, fx, fy + 6.0f);
        g.fillPath(triangle);
    }
}

// ============================================================================
// Warp Marker Helpers
// ============================================================================

int WaveformGridComponent::findMarkerAtPixel(int x) const {
    const auto* clip = getClip();
    if (!clip)
        return -1;

    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;

    // Skip first and last markers (TE's boundary markers)
    // Only allow interaction with user-created markers
    int numMarkers = static_cast<int>(warpMarkers_.size());
    for (int i = 1; i < numMarkers - 1; ++i) {
        const auto& marker = warpMarkers_[static_cast<size_t>(i)];
        double clipRelativeTime = marker.warpTime - clip->offset;
        double displayTime = clipRelativeTime + displayStartTime;
        int px = timeToPixel(displayTime);
        if (std::abs(x - px) <= WARP_MARKER_HIT_DISTANCE)
            return i;
    }
    return -1;
}

double WaveformGridComponent::snapToNearestTransient(double time) const {
    static constexpr double SNAP_THRESHOLD = 0.05;  // 50ms snap distance
    double closest = time;
    double closestDist = SNAP_THRESHOLD;

    for (double t : transientTimes_) {
        double dist = std::abs(t - time);
        if (dist < closestDist) {
            closestDist = dist;
            closest = t;
        }
    }
    return closest;
}

void WaveformGridComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    if (!warpMode_ || editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    int markerIndex = findMarkerAtPixel(event.x);
    if (markerIndex >= 0 && onWarpMarkerRemove) {
        onWarpMarkerRemove(markerIndex);
    }
}

}  // namespace magda::daw::ui
