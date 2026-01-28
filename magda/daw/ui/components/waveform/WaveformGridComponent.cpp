#include "WaveformGridComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "audio/AudioThumbnailManager.hpp"

namespace magda::daw::ui {

WaveformGridComponent::WaveformGridComponent() {
    setName("WaveformGrid");
}

void WaveformGridComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

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

    if (clip.audioSources.empty()) {
        return;
    }

    const auto& source = clip.audioSources[0];

    // Calculate display position accounting for mode
    double displayStartTime = relativeMode_ ? source.position : (clipStartTime_ + source.position);
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(source.length * horizontalZoom_);

    DBG("paintWaveform - mode=" << (relativeMode_ ? "REL" : "ABS") << ", clipStart="
                                << clipStartTime_ << ", source.position=" << source.position
                                << ", displayStartTime=" << displayStartTime
                                << ", positionPixels=" << positionPixels);

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    // Draw waveform background (source block)
    g.setColour(clip.colour.darker(0.4f));
    g.fillRoundedRectangle(waveformRect.toFloat(), 3.0f);

    // Draw real waveform from audio thumbnail
    if (source.filePath.isNotEmpty()) {
        auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
        double fileWindow = source.length / source.stretchFactor;
        double displayStart = source.offset;
        double displayEnd = source.offset + fileWindow;

        thumbnailManager.drawWaveform(g, waveformRect.reduced(0, 4), source.filePath, displayStart,
                                      displayEnd, clip.colour.brighter(0.2f));
    }

    // Draw center line
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(waveformRect.getCentreY(), waveformRect.getX(), waveformRect.getRight());

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
        DBG("WaveformGrid::setClip - clipId=" << clipId << ", startTime=" << clipStartTime_
                                              << ", length=" << clipLength_);
    } else {
        clipStartTime_ = 0.0;
        clipLength_ = 0.0;
        DBG("WaveformGrid::setClip - no clip found for id=" << clipId);
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

void WaveformGridComponent::setScrollOffset(int x, int y) {
    scrollOffsetX_ = x;
    scrollOffsetY_ = y;
}

void WaveformGridComponent::updateGridSize() {
    const auto* clip = getClip();
    if (!clip) {
        setSize(800, 400);  // Default size when no clip
        DBG("WaveformGrid::updateGridSize - no clip, using default 800x400");
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
    int requiredHeight = 400;  // Fixed height for now

    DBG("WaveformGrid::updateGridSize - mode="
        << (relativeMode_ ? "REL" : "ABS") << ", clipStart=" << clipStartTime_
        << ", clipLength=" << clipLength_ << ", totalTime=" << totalTime
        << ", zoom=" << horizontalZoom_ << ", size=" << requiredWidth << "x" << requiredHeight);

    setSize(requiredWidth, requiredHeight);
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

int WaveformGridComponent::timeToPixel(double time) const {
    double offsetTime = relativeMode_ ? time : time;  // In ABS mode, time is already absolute
    return static_cast<int>(offsetTime * horizontalZoom_) + LEFT_PADDING;
}

double WaveformGridComponent::pixelToTime(int x) const {
    double time = (x - LEFT_PADDING) / horizontalZoom_;
    return relativeMode_ ? time : time;  // In ABS mode, return absolute time
}

// ============================================================================
// Mouse Interaction
// ============================================================================

void WaveformGridComponent::mouseDown(const juce::MouseEvent& event) {
    DBG("WaveformGrid::mouseDown at x=" << event.x << ", y=" << event.y);

    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        DBG("  No clip editing");
        return;
    }

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->type != magda::ClipType::Audio || clip->audioSources.empty()) {
        DBG("  No valid audio clip");
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
        DBG("WaveformGrid::mouseDrag - dragMode is None");
        return;
    }
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        DBG("WaveformGrid::mouseDrag - no clip editing");
        return;
    }

    DBG("WaveformGrid::mouseDrag - mode=" << static_cast<int>(dragMode_) << ", x=" << event.x);

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->audioSources.empty())
        return;

    auto& source = clip->audioSources[0];
    double deltaSeconds = (event.x - dragStartX_) / horizontalZoom_;

    switch (dragMode_) {
        case DragMode::ResizeLeft: {
            double sf = dragStartStretchFactor_;
            double fileDelta = deltaSeconds / sf;
            double newOffset = std::max(0.0, dragStartAudioOffset_ + fileDelta);
            if (dragStartFileDuration_ > 0.0) {
                newOffset = std::min(newOffset, dragStartFileDuration_);
            }
            double actualFileDelta = newOffset - dragStartAudioOffset_;
            double timelineDelta = actualFileDelta * sf;
            source.offset = newOffset;
            source.position = std::max(0.0, dragStartPosition_ + timelineDelta);
            source.length = std::max(0.1, dragStartLength_ - timelineDelta);
            break;
        }
        case DragMode::ResizeRight: {
            double newLength = std::max(0.1, dragStartLength_ + deltaSeconds);
            if (dragStartFileDuration_ > 0.0) {
                double maxLength = (dragStartFileDuration_ - source.offset) * source.stretchFactor;
                newLength = std::min(newLength, maxLength);
            }
            double clipMaxLength = clip->length - source.position;
            newLength = std::min(newLength, clipMaxLength);
            source.length = newLength;
            break;
        }
        case DragMode::Move: {
            source.position = std::max(0.0, dragStartPosition_ + deltaSeconds);
            break;
        }
        case DragMode::StretchRight: {
            double maxLength = clip->length - source.position;
            double newLength = std::max(0.1, dragStartLength_ + deltaSeconds);
            newLength = std::min(newLength, maxLength);
            double stretchRatio = newLength / dragStartLength_;
            double newStretchFactor = dragStartStretchFactor_ * stretchRatio;
            newStretchFactor = juce::jlimit(0.25, 4.0, newStretchFactor);
            newLength = dragStartLength_ * (newStretchFactor / dragStartStretchFactor_);
            newLength = std::min(newLength, maxLength);
            source.length = newLength;
            source.stretchFactor = newStretchFactor;
            break;
        }
        case DragMode::StretchLeft: {
            double rightEdge = dragStartPosition_ + dragStartLength_;
            double newLength = std::max(0.1, dragStartLength_ - deltaSeconds);
            newLength = std::min(newLength, rightEdge);
            double stretchRatio = newLength / dragStartLength_;
            double newStretchFactor = dragStartStretchFactor_ * stretchRatio;
            newStretchFactor = juce::jlimit(0.25, 4.0, newStretchFactor);
            newLength = dragStartLength_ * (newStretchFactor / dragStartStretchFactor_);
            newLength = std::min(newLength, rightEdge);
            source.length = newLength;
            source.position = rightEdge - newLength;
            source.stretchFactor = newStretchFactor;
            break;
        }
        default:
            break;
    }

    magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    repaint();

    if (onWaveformChanged) {
        onWaveformChanged();
    }
}

void WaveformGridComponent::mouseUp(const juce::MouseEvent& /*event*/) {
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
