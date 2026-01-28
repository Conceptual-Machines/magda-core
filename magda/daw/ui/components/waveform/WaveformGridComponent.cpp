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

    if (clip.audioSources.empty()) {
        return;
    }

    const auto& source = clip.audioSources[0];

    // Calculate display position accounting for mode
    double displayStartTime = relativeMode_ ? source.position : (clipStartTime_ + source.position);
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(source.length * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    // Calculate clip boundaries for highlighting out-of-bounds regions
    int clipStartPixel = relativeMode_ ? timeToPixel(0.0) : timeToPixel(clipStartTime_);
    int clipEndPixel =
        relativeMode_ ? timeToPixel(clipLength_) : timeToPixel(clipStartTime_ + clipLength_);

    // Draw out-of-bounds background (darker) for parts beyond clip boundaries
    auto outOfBoundsColour = clip.colour.darker(0.7f);

    // Left out-of-bounds region (source starts before clip start)
    if (waveformRect.getX() < clipStartPixel) {
        int outOfBoundsWidth =
            juce::jmin(clipStartPixel - waveformRect.getX(), waveformRect.getWidth());
        auto leftOutOfBounds = waveformRect.removeFromLeft(outOfBoundsWidth);
        g.setColour(outOfBoundsColour);
        g.fillRoundedRectangle(leftOutOfBounds.toFloat(), 3.0f);
    }

    // Right out-of-bounds region (source extends past clip end)
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
    if (source.filePath.isNotEmpty()) {
        auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
        double fileWindow = source.length / source.stretchFactor;
        double displayStart = source.offset;
        double displayEnd = source.offset + fileWindow;

        // Clip drawing to waveform bounds, pass verticalZoom as amplitude gain
        auto waveDrawRect = waveformRect.reduced(0, 4);
        if (waveDrawRect.getWidth() > 0 && waveDrawRect.getHeight() > 0) {
            g.saveState();
            if (g.reduceClipRegion(waveformRect)) {
                thumbnailManager.drawWaveform(g, waveDrawRect, source.filePath, displayStart,
                                              displayEnd, clip.colour.brighter(0.2f),
                                              static_cast<float>(verticalZoom_));
            }
            g.restoreState();
        }
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
    clipStartTime_ = startTime;
    clipLength_ = length;
    updateGridSize();
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
    if (!clip || clip->type != magda::ClipType::Audio || clip->audioSources.empty()) {
        return;
    }

    const auto& source = clip->audioSources[0];
    int x = event.x;
    bool shiftHeld = event.mods.isShiftDown();

    if (isNearLeftEdge(x, source)) {
        dragMode_ = shiftHeld ? DragMode::StretchLeft : DragMode::ResizeLeft;
    } else if (isNearRightEdge(x, source)) {
        dragMode_ = shiftHeld ? DragMode::StretchRight : DragMode::ResizeRight;
    } else if (isInsideWaveform(x, source)) {
        dragMode_ = DragMode::Move;
    } else {
        dragMode_ = DragMode::None;
        return;
    }

    dragStartX_ = x;
    dragStartPosition_ = source.position;
    dragStartAudioOffset_ = source.offset;
    dragStartLength_ = source.length;
    dragStartStretchFactor_ = source.stretchFactor;

    // Cache file duration for trim clamping
    dragStartFileDuration_ = 0.0;
    auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(source.filePath);
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

    // DBG removed - too expensive in hot path (called 60+ times/sec)

    // Get clip and source for direct modification during drag (performance optimization)
    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->audioSources.empty())
        return;

    auto& source = clip->audioSources[0];
    double deltaSeconds = (event.x - dragStartX_) / horizontalZoom_;

    // Calculate absolute values from original drag start values
    // (ClipOperations trim functions are incremental, so we calculate absolute here)
    switch (dragMode_) {
        case DragMode::ResizeLeft: {
            // Calculate absolute new offset and position from original values
            double fileDelta = deltaSeconds / dragStartStretchFactor_;
            double newOffset = dragStartAudioOffset_ + fileDelta;

            // Constrain to file bounds
            if (dragStartFileDuration_ > 0.0) {
                newOffset = juce::jmin(newOffset, dragStartFileDuration_);
            }
            newOffset = juce::jmax(0.0, newOffset);

            // Calculate actual timeline delta achieved
            double actualFileDelta = newOffset - dragStartAudioOffset_;
            double timelineDelta = actualFileDelta * dragStartStretchFactor_;

            // Set absolute values
            source.offset = newOffset;
            source.position = juce::jmax(0.0, dragStartPosition_ + timelineDelta);
            source.length = juce::jmax(magda::ClipOperations::MIN_SOURCE_LENGTH,
                                       dragStartLength_ - timelineDelta);
            break;
        }
        case DragMode::ResizeRight: {
            // Calculate absolute new length from original
            double newLength = dragStartLength_ + deltaSeconds;

            // Constrain to file bounds
            if (dragStartFileDuration_ > 0.0) {
                double maxLength =
                    (dragStartFileDuration_ - dragStartAudioOffset_) * dragStartStretchFactor_;
                newLength = juce::jmin(newLength, maxLength);
            }

            // Constrain to clip bounds
            double clipMaxLength = clip->length - dragStartPosition_;
            newLength = juce::jmin(newLength, clipMaxLength);

            // Set absolute value
            source.length = juce::jmax(magda::ClipOperations::MIN_SOURCE_LENGTH, newLength);
            break;
        }
        case DragMode::Move: {
            // Calculate absolute position from original
            double newPosition = dragStartPosition_ + deltaSeconds;
            source.position = juce::jmax(0.0, newPosition);
            break;
        }
        case DragMode::StretchRight: {
            // Stretch operations already handle absolute values correctly
            double newLength = dragStartLength_ + deltaSeconds;
            magda::ClipOperations::stretchSourceFromRight(source, newLength, dragStartLength_,
                                                          dragStartStretchFactor_, clip->length);
            break;
        }
        case DragMode::StretchLeft: {
            // Stretch operations already handle absolute values correctly
            double newLength = dragStartLength_ - deltaSeconds;
            magda::ClipOperations::stretchSourceFromLeft(
                source, newLength, dragStartLength_, dragStartPosition_, dragStartStretchFactor_);
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
    // Notify ClipManager once at the end of the drag operation
    if (dragMode_ != DragMode::None && editingClipId_ != magda::INVALID_CLIP_ID) {
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    }
    dragMode_ = DragMode::None;
}

void WaveformGridComponent::mouseMove(const juce::MouseEvent& event) {
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto* clip = getClip();
    if (!clip || clip->audioSources.empty()) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto& source = clip->audioSources[0];
    int x = event.x;

    if (isNearLeftEdge(x, source) || isNearRightEdge(x, source)) {
        if (event.mods.isShiftDown()) {
            setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        } else {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }
    } else if (isInsideWaveform(x, source)) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// ============================================================================
// Hit Testing Helpers
// ============================================================================

bool WaveformGridComponent::isNearLeftEdge(int x, const magda::AudioSource& source) const {
    double displayStartTime = relativeMode_ ? source.position : (clipStartTime_ + source.position);
    int leftEdgeX = timeToPixel(displayStartTime);
    return std::abs(x - leftEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isNearRightEdge(int x, const magda::AudioSource& source) const {
    double displayStartTime = relativeMode_ ? source.position : (clipStartTime_ + source.position);
    int rightEdgeX = timeToPixel(displayStartTime + source.length);
    return std::abs(x - rightEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isInsideWaveform(int x, const magda::AudioSource& source) const {
    double displayStartTime = relativeMode_ ? source.position : (clipStartTime_ + source.position);
    int leftEdgeX = timeToPixel(displayStartTime);
    int rightEdgeX = timeToPixel(displayStartTime + source.length);
    return x > leftEdgeX + EDGE_GRAB_DISTANCE && x < rightEdgeX - EDGE_GRAB_DISTANCE;
}

// ============================================================================
// Private Helpers
// ============================================================================

const magda::ClipInfo* WaveformGridComponent::getClip() const {
    return magda::ClipManager::getInstance().getClip(editingClipId_);
}

}  // namespace magda::daw::ui
