#include "WaveformGridComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
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
    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);

    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0) {
        return;
    }

    if (clip.audioFilePath.isEmpty()) {
        return;
    }

    // Flat model: audio always starts at clip start (position = 0)
    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(clip.length * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    // Calculate clip boundaries for highlighting out-of-bounds regions
    // When looping is active, treat the loop end as the effective boundary
    double effectiveLength =
        (loopEndSeconds_ > 0.0) ? std::min(clipLength_, loopEndSeconds_) : clipLength_;
    int clipStartPixel = relativeMode_ ? timeToPixel(0.0) : timeToPixel(clipStartTime_);
    int clipEndPixel = relativeMode_ ? timeToPixel(effectiveLength)
                                     : timeToPixel(clipStartTime_ + effectiveLength);

    // Draw out-of-bounds background (darker) for parts beyond clip boundaries
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

        // Draw in-bounds background (normal)
        g.setColour(clip.colour.darker(0.4f));
        if (!inBoundsRect.isEmpty()) {
            g.fillRoundedRectangle(inBoundsRect.toFloat(), 3.0f);
        }

        // Draw out-of-bounds background (darker) for remaining part
        if (!waveformRect.isEmpty()) {
            g.setColour(outOfBoundsColour);
            g.fillRoundedRectangle(waveformRect.toFloat(), 3.0f);
        }

        // Restore waveformRect for waveform drawing
        waveformRect = inBoundsRect.getUnion(waveformRect);
    } else {
        // All in bounds - draw normal background
        g.setColour(clip.colour.darker(0.4f));
        g.fillRoundedRectangle(waveformRect.toFloat(), 3.0f);
    }

    // Recalculate full waveform rect for drawing (we modified it above)
    waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    // Draw real waveform from audio thumbnail (scaled by vertical zoom)
    auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
    auto* thumbnail = thumbnailManager.getThumbnail(clip.audioFilePath);
    double fileDuration = thumbnail ? thumbnail->getTotalLength() : 0.0;
    auto waveColour = clip.colour.brighter(0.2f);
    auto vertZoom = static_cast<float>(verticalZoom_);

    bool isLooped = loopEndSeconds_ > 0.0 && loopEndSeconds_ < clip.length;

    g.saveState();
    if (g.reduceClipRegion(waveformRect)) {
        if (isLooped) {
            // Looped: tile waveform across the full clip length
            double loopCycle = loopEndSeconds_;
            double fileStart = clip.audioOffset;
            double fileEnd = clip.audioOffset + loopCycle / clip.audioStretchFactor;
            bool fileClamped = false;
            if (fileDuration > 0.0 && fileEnd > fileDuration) {
                fileEnd = fileDuration;
                fileClamped = true;
            }

            // If the source audio is shorter than the loop cycle, reduce the
            // draw width to match so the thumbnail isn't stretched beyond the file.
            double actualDisplayCycle =
                fileClamped ? (fileEnd - fileStart) * clip.audioStretchFactor : loopCycle;

            double timePos = 0.0;
            while (timePos < clip.length) {
                double cycleEnd = std::min(timePos + actualDisplayCycle, clip.length);
                int drawX = waveformRect.getX() + static_cast<int>(timePos * horizontalZoom_);
                int drawRight = waveformRect.getX() + static_cast<int>(cycleEnd * horizontalZoom_);
                auto cycleRect = juce::Rectangle<int>(drawX, waveformRect.getY(), drawRight - drawX,
                                                      waveformRect.getHeight());
                auto drawRect = cycleRect.reduced(0, 4);
                if (drawRect.getWidth() > 0 && drawRect.getHeight() > 0) {
                    thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath, fileStart,
                                                  fileEnd, waveColour, vertZoom);
                }
                timePos += loopCycle;
            }
        } else {
            // Non-looped: single draw, clamped to file duration
            double displayStart = clip.audioOffset;
            double displayEnd = clip.audioOffset + clip.length / clip.audioStretchFactor;
            if (fileDuration > 0.0 && displayEnd > fileDuration)
                displayEnd = fileDuration;

            double clampedDuration = (displayEnd - displayStart) * clip.audioStretchFactor;
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

    // Draw transient or warp markers
    if (warpMode_ && !warpMarkers_.empty()) {
        paintWarpMarkers(g, clip);
    } else if (!warpMode_ && !transientTimes_.isEmpty()) {
        paintTransientMarkers(g, clip);
    }

    // Draw center line
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(waveformRect.getCentreY(), waveformRect.getX(), waveformRect.getRight());

    // Draw clip boundary indicator line at clip end
    if (clipEndPixel > waveformRect.getX() && clipEndPixel < waveformRect.getRight()) {
        g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
        g.fillRect(clipEndPixel - 1, waveformRect.getY(), 2, waveformRect.getHeight());
    }

    // Clip info overlay
    g.setColour(clip.colour);
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText(clip.name, waveformRect.reduced(8, 4), juce::Justification::topLeft, true);

    // Border around source block
    g.setColour(clip.colour.withAlpha(0.5f));
    g.drawRoundedRectangle(waveformRect.toFloat(), 3.0f, 1.0f);

    // Draw trim handles
    g.setColour(clip.colour.brighter(0.4f));
    g.fillRect(waveformRect.getX(), waveformRect.getY(), 3, waveformRect.getHeight());
    g.fillRect(waveformRect.getRight() - 3, waveformRect.getY(), 3, waveformRect.getHeight());
}

void WaveformGridComponent::paintClipBoundaries(juce::Graphics& g) {
    if (clipLength_ <= 0.0) {
        return;
    }

    auto bounds = getLocalBounds();

    if (!relativeMode_) {
        // Absolute mode: show both start and end boundaries at absolute timeline positions
        int clipStartX = timeToPixel(clipStartTime_);
        g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
        g.fillRect(clipStartX - 1, 0, 2, bounds.getHeight());

        int clipEndX = timeToPixel(clipStartTime_ + clipLength_);
        g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
        g.fillRect(clipEndX - 1, 0, 3, bounds.getHeight());

        // Loop boundary (distinct from clip end)
        if (loopEndSeconds_ > 0.0 && loopEndSeconds_ < clipLength_) {
            int loopEndX = timeToPixel(clipStartTime_ + loopEndSeconds_);
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.5f));
            // Draw dashed-style loop marker: thinner line with label
            g.fillRect(loopEndX - 1, 0, 2, bounds.getHeight());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText("L", loopEndX + 3, 2, 12, 12, juce::Justification::centredLeft, false);
        }
    } else {
        // Relative mode: show both start (at 0) and end boundaries
        // Start boundary at time 0
        int clipStartX = timeToPixel(0.0);
        g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
        g.fillRect(clipStartX - 1, 0, 2, bounds.getHeight());

        // End boundary at clip length
        int clipEndX = timeToPixel(clipLength_);
        g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
        g.fillRect(clipEndX - 1, 0, 3, bounds.getHeight());

        // Loop boundary (distinct from clip end)
        if (loopEndSeconds_ > 0.0 && loopEndSeconds_ < clipLength_) {
            int loopEndX = timeToPixel(loopEndSeconds_);
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.5f));
            g.fillRect(loopEndX - 1, 0, 2, bounds.getHeight());
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

    bool isLooped = loopEndSeconds_ > 0.0 && loopEndSeconds_ < clip.length;

    // Visible pixel range for culling
    int visibleLeft = 0;
    int visibleRight = getWidth();

    auto drawMarkersForCycle = [&](double cycleOffset, double sourceStart, double sourceEnd) {
        for (double t : transientTimes_) {
            if (t < sourceStart || t >= sourceEnd)
                continue;

            // Convert source time to display time
            double displayTime = (t - sourceStart) * clip.audioStretchFactor + cycleOffset;
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
        double loopCycle = loopEndSeconds_;
        double fileStart = clip.audioOffset;
        double fileEnd = clip.audioOffset + loopCycle / clip.audioStretchFactor;

        double timePos = 0.0;
        while (timePos < clip.length) {
            drawMarkersForCycle(timePos, fileStart, fileEnd);
            timePos += loopCycle;
        }
    } else {
        double sourceStart = clip.audioOffset;
        double sourceEnd = clip.audioOffset + clip.length / clip.audioStretchFactor;
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

void WaveformGridComponent::setLoopEndSeconds(double loopEndSeconds) {
    double val = loopEndSeconds > 0.0 ? loopEndSeconds : 0.0;
    if (loopEndSeconds_ != val) {
        loopEndSeconds_ = val;
        repaint();
    }
}

void WaveformGridComponent::setTransientTimes(const juce::Array<double>& times) {
    transientTimes_ = times;
    repaint();
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

    // Calculate required width based on mode
    double totalTime = 0.0;
    if (relativeMode_) {
        // In relative mode, show clip length + right padding
        totalTime = clipLength_ + 10.0;  // 10 seconds right padding
    } else {
        // In absolute mode, show from 0 to clip end + both left and right padding
        // Add left padding so we can scroll before clip start
        double leftPaddingTime =
            std::max(10.0, clipStartTime_ * 0.5);  // At least 10s or half the clip start time
        totalTime = clipStartTime_ + clipLength_ + 10.0 + leftPaddingTime;
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
        if (isInsideWaveform(x, *clip)) {
            double clickTime = pixelToTime(x);
            // Convert from display time to clip-relative time
            double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
            double clipRelativeTime = clickTime - displayStartTime;

            // Convert clip-relative display time to source-file time
            double sourceTime = clipRelativeTime / clip->audioStretchFactor + clip->audioOffset;

            // Snap to nearest transient (in source-file time) unless Alt is held
            if (!event.mods.isAltDown()) {
                sourceTime = snapToNearestTransient(sourceTime);
            }

            if (onWarpMarkerAdd) {
                // Identity mapping: sourceTime == warpTime for a new marker
                onWarpMarkerAdd(sourceTime, sourceTime);
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
    dragStartAudioOffset_ = clip->audioOffset;
    dragStartLength_ = clip->length;
    dragStartStartTime_ = clip->startTime;
    dragStartStretchFactor_ = clip->audioStretchFactor;

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

        // Pixel delta → display-time delta → source-file-time delta
        double displayDelta = (event.x - dragStartX_) / horizontalZoom_;
        double sourceDelta = displayDelta / clip->audioStretchFactor;
        double newWarpTime = dragStartWarpTime_ + sourceDelta;
        if (newWarpTime < 0.0)
            newWarpTime = 0.0;

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
            // Content-level trim: only change audioOffset (file start point).
            // The clip stays at the same position/length on the timeline.
            double fileDelta = deltaSeconds / dragStartStretchFactor_;
            double newOffset = dragStartAudioOffset_ + fileDelta;

            // Constrain to file bounds
            if (dragStartFileDuration_ > 0.0) {
                newOffset = juce::jmin(newOffset, dragStartFileDuration_);
            }
            newOffset = juce::jmax(0.0, newOffset);

            clip->audioOffset = newOffset;
            break;
        }
        case DragMode::ResizeRight: {
            // Calculate absolute new length from original
            double newLength = dragStartLength_ + deltaSeconds;

            // Constrain to file bounds (only for non-looped clips)
            if (dragStartFileDuration_ > 0.0 && !clip->internalLoopEnabled) {
                double maxLength =
                    (dragStartFileDuration_ - dragStartAudioOffset_) * dragStartStretchFactor_;
                newLength = juce::jmin(newLength, maxLength);
            }

            // Set absolute value
            clip->length = juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, newLength);
            break;
        }
        case DragMode::StretchRight: {
            double newLength = dragStartLength_ + deltaSeconds;
            magda::ClipOperations::stretchAudioFromRight(*clip, newLength, dragStartLength_,
                                                         dragStartStretchFactor_);
            break;
        }
        case DragMode::StretchLeft: {
            double newLength = dragStartLength_ - deltaSeconds;
            magda::ClipOperations::stretchAudioFromLeft(*clip, newLength, dragStartLength_,
                                                        dragStartStretchFactor_);
            break;
        }
        default:
            break;
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
    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int rightEdgeX = timeToPixel(displayStartTime + clip.length);
    return std::abs(x - rightEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isInsideWaveform(int x, const magda::ClipInfo& clip) const {
    double displayStartTime = relativeMode_ ? 0.0 : clipStartTime_;
    int leftEdgeX = timeToPixel(displayStartTime);
    int rightEdgeX = timeToPixel(displayStartTime + clip.length);
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

    for (int i = 0; i < static_cast<int>(warpMarkers_.size()); ++i) {
        const auto& marker = warpMarkers_[static_cast<size_t>(i)];

        // Convert warp time (source-file seconds) to clip-relative display time
        // Same conversion as transient markers: account for audioOffset and stretchFactor
        double clipRelativeTime = (marker.warpTime - clip.audioOffset) * clip.audioStretchFactor;
        if (clipRelativeTime < 0.0 || clipRelativeTime > clip.length)
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

    for (int i = 0; i < static_cast<int>(warpMarkers_.size()); ++i) {
        const auto& marker = warpMarkers_[static_cast<size_t>(i)];
        double clipRelativeTime = (marker.warpTime - clip->audioOffset) * clip->audioStretchFactor;
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
