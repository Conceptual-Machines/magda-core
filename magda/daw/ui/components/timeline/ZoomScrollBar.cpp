#include "ZoomScrollBar.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

ZoomScrollBar::ZoomScrollBar(Orientation orientation, InteractionMode interactionMode)
    : orientation(orientation), interactionMode(interactionMode) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    setOpaque(false);
}

void ZoomScrollBar::paint(juce::Graphics& g) {
    if (getWidth() < 8 || getHeight() < 8)
        return;

    auto trackBounds = getTrackBounds();
    auto thumbBounds = getThumbBounds();

    // Draw track background. The horizontal bar sits in a parent-painted row
    // matching the master/content backgrounds, so avoid adding a dark gutter fill.
    if (orientation == Orientation::Vertical) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(trackBounds.toFloat(), 3.0f);
    }

    // Draw track border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(trackBounds.toFloat(), 3.0f, 1.0f);

    // Draw thumb
    auto thumbColour = DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY);
    if (dragMode != DragMode::None) {
        thumbColour = thumbColour.brighter(0.2f);
    }
    g.setColour(thumbColour.withAlpha(0.6f));
    g.fillRoundedRectangle(thumbBounds.toFloat(), 3.0f);

    // Draw thumb border
    g.setColour(thumbColour);
    g.drawRoundedRectangle(thumbBounds.toFloat(), 3.0f, 1.0f);

    // Draw label if set (fixed position on right/bottom)
    if (label.isNotEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));

        if (orientation == Orientation::Horizontal) {
            // Draw label fixed on the right side
            auto labelBounds = trackBounds.removeFromRight(50);
            g.drawText(label, labelBounds, juce::Justification::centred);
        } else {
            // For vertical, draw fixed at bottom
            auto labelBounds = trackBounds.removeFromBottom(20);
            g.drawText(label, labelBounds, juce::Justification::centred);
        }
    }
}

void ZoomScrollBar::setLabel(const juce::String& text) {
    if (label != text) {
        label = text;
        repaint();
    }
}

void ZoomScrollBar::resized() {
    // Nothing specific needed
}

void ZoomScrollBar::mouseDown(const juce::MouseEvent& event) {
    reveal();
    int pos = getPrimaryCoord(event);
    dragMode = getDragModeForPosition(pos);
    dragStartPos = pos;
    dragStartVisibleStart = visibleStart;
    dragStartVisibleEnd = visibleEnd;

    // If clicking outside thumb, jump to that position
    if (dragMode == DragMode::None) {
        auto trackBounds = getTrackBounds();
        int trackPrimaryPos = getPrimaryPos(trackBounds);
        int trackPrimarySize = getPrimarySize(trackBounds);

        double clickPos = static_cast<double>(pos - trackPrimaryPos) / trackPrimarySize;
        clickPos = juce::jlimit(0.0, 1.0, clickPos);

        double rangeWidth = visibleEnd - visibleStart;
        double newStart = clickPos - rangeWidth / 2.0;
        double newEnd = newStart + rangeWidth;

        // Clamp to valid range
        if (newStart < 0.0) {
            newStart = 0.0;
            newEnd = rangeWidth;
        }
        if (newEnd > 1.0) {
            newEnd = 1.0;
            newStart = 1.0 - rangeWidth;
        }

        visibleStart = juce::jmax(0.0, newStart);
        visibleEnd = juce::jmin(1.0, newEnd);

        if (onRangeChanged) {
            onRangeChanged(visibleStart, visibleEnd);
        }

        // Now start scrolling from new position
        dragMode = DragMode::Scroll;
        dragStartPos = pos;
        dragStartVisibleStart = visibleStart;
        dragStartVisibleEnd = visibleEnd;

        repaint();
    }
}

void ZoomScrollBar::mouseDrag(const juce::MouseEvent& event) {
    reveal();
    auto trackBounds = getTrackBounds();
    auto trackPrimarySize = static_cast<double>(getPrimarySize(trackBounds));

    if (trackPrimarySize <= 0)
        return;

    int pos = getPrimaryCoord(event);
    double delta = static_cast<double>(pos - dragStartPos) / trackPrimarySize;

    switch (dragMode) {
        case DragMode::Scroll: {
            // Move the entire visible range
            double rangeWidth = dragStartVisibleEnd - dragStartVisibleStart;
            double newStart = dragStartVisibleStart + delta;
            double newEnd = newStart + rangeWidth;

            // Clamp to valid range
            if (newStart < 0.0) {
                newStart = 0.0;
                newEnd = rangeWidth;
            }
            if (newEnd > 1.0) {
                newEnd = 1.0;
                newStart = 1.0 - rangeWidth;
            }

            visibleStart = newStart;
            visibleEnd = newEnd;
            break;
        }

        case DragMode::ResizeStart: {
            double newStart = dragStartVisibleStart + delta;
            newStart = juce::jlimit(0.0, visibleEnd - 0.01, newStart);
            visibleStart = newStart;
            break;
        }

        case DragMode::ResizeEnd: {
            double newEnd = dragStartVisibleEnd + delta;
            newEnd = juce::jlimit(visibleStart + 0.01, 1.0, newEnd);
            visibleEnd = newEnd;
            break;
        }

        case DragMode::None:
            return;
    }

    if (onRangeChanged) {
        onRangeChanged(visibleStart, visibleEnd);
    }

    repaint();
}

void ZoomScrollBar::mouseUp(const juce::MouseEvent&) {
    dragMode = DragMode::None;
    reveal();
    repaint();
}

void ZoomScrollBar::mouseEnter(const juce::MouseEvent&) {
    reveal();
}

void ZoomScrollBar::mouseMove(const juce::MouseEvent& event) {
    reveal();
    updateCursor(getPrimaryCoord(event));
}

void ZoomScrollBar::mouseWheelMove(const juce::MouseEvent& event,
                                   const juce::MouseWheelDetails& wheel) {
    reveal();
    if (onWheelMoved) {
        onWheelMoved(event, wheel);
        return;
    }
    juce::Component::mouseWheelMove(event, wheel);
}

void ZoomScrollBar::setVisibleRange(double start, double end) {
    // Ignore external updates while user is dragging to prevent feedback loops
    if (dragMode != DragMode::None)
        return;

    visibleStart = juce::jlimit(0.0, 1.0, start);
    visibleEnd = juce::jlimit(0.0, 1.0, end);

    if (visibleEnd <= visibleStart) {
        visibleEnd = visibleStart + 0.01;
    }

    repaint();
}

void ZoomScrollBar::setAutoHideEnabled(bool enabled) {
    if (autoHideEnabled == enabled)
        return;

    autoHideEnabled = enabled;
    revealHoldFrames = 0;
    if (autoHideEnabled) {
        setAlpha(0.0f);
        startTimerHz(60);
    } else {
        stopTimer();
        setAlpha(1.0f);
    }
}

void ZoomScrollBar::reveal() {
    if (!autoHideEnabled)
        return;

    revealHoldFrames = REVEAL_HOLD_FRAMES;
    setAlpha(juce::jmax(getAlpha(), FADE_IN_STEP));
}

void ZoomScrollBar::timerCallback() {
    if (!autoHideEnabled)
        return;

    if (isMouseOverOrDragging() || isDragging())
        revealHoldFrames = REVEAL_HOLD_FRAMES;
    else if (revealHoldFrames > 0)
        --revealHoldFrames;

    const float alpha = getAlpha();
    const float nextAlpha = revealHoldFrames > 0 ? juce::jmin(1.0f, alpha + FADE_IN_STEP)
                                                 : juce::jmax(0.0f, alpha - FADE_OUT_STEP);
    if (nextAlpha != alpha)
        setAlpha(nextAlpha);
}

juce::Rectangle<int> ZoomScrollBar::getTrackBounds() const {
    auto bounds = getLocalBounds();

    if (orientation == Orientation::Horizontal) {
        int height = bounds.getHeight() - 8;
        int yOffset = (bounds.getHeight() - height) / 2;
        return {bounds.getX() + 2, bounds.getY() + yOffset, bounds.getWidth() - 4, height};
    } else {
        int width = bounds.getWidth() - 8;
        int xOffset = (bounds.getWidth() - width) / 2;
        return {bounds.getX() + xOffset, bounds.getY() + 2, width, bounds.getHeight() - 4};
    }
}

juce::Rectangle<int> ZoomScrollBar::getThumbBounds() const {
    auto trackBounds = getTrackBounds();

    if (orientation == Orientation::Horizontal) {
        int thumbX = trackBounds.getX() + static_cast<int>(visibleStart * trackBounds.getWidth());
        int thumbWidth = static_cast<int>((visibleEnd - visibleStart) * trackBounds.getWidth());
        thumbWidth = juce::jmax(thumbWidth, MIN_THUMB_SIZE);

        return {thumbX, trackBounds.getY(), thumbWidth, trackBounds.getHeight()};
    } else {
        int thumbY = trackBounds.getY() + static_cast<int>(visibleStart * trackBounds.getHeight());
        int thumbHeight = static_cast<int>((visibleEnd - visibleStart) * trackBounds.getHeight());
        thumbHeight = juce::jmax(thumbHeight, MIN_THUMB_SIZE);

        return {trackBounds.getX(), thumbY, trackBounds.getWidth(), thumbHeight};
    }
}

ZoomScrollBar::DragMode ZoomScrollBar::getDragModeForPosition(int pos) const {
    auto thumbBounds = getThumbBounds();

    // Check if position is within thumb bounds (using the perpendicular center for hit test)
    bool inThumb;
    if (orientation == Orientation::Horizontal) {
        inThumb = thumbBounds.contains(pos, thumbBounds.getCentreY());
    } else {
        inThumb = thumbBounds.contains(thumbBounds.getCentreX(), pos);
    }

    if (!inThumb) {
        return DragMode::None;
    }

    int thumbStart = getPrimaryPos(thumbBounds);
    int thumbEnd = thumbStart + getPrimarySize(thumbBounds);

    if (interactionMode == InteractionMode::ScrollAndZoom) {
        // Check if near start edge
        if (pos < thumbStart + EDGE_HANDLE_SIZE) {
            return DragMode::ResizeStart;
        }

        // Check if near end edge
        if (pos > thumbEnd - EDGE_HANDLE_SIZE) {
            return DragMode::ResizeEnd;
        }
    }

    return DragMode::Scroll;
}

void ZoomScrollBar::updateCursor(int pos) {
    auto mode = getDragModeForPosition(pos);

    switch (mode) {
        case DragMode::ResizeStart:
        case DragMode::ResizeEnd:
            if (orientation == Orientation::Horizontal) {
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            } else {
                setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            }
            break;
        case DragMode::Scroll:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case DragMode::None:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

int ZoomScrollBar::getPrimaryCoord(const juce::MouseEvent& event) const {
    return orientation == Orientation::Horizontal ? event.x : event.y;
}

int ZoomScrollBar::getPrimarySize(const juce::Rectangle<int>& rect) const {
    return orientation == Orientation::Horizontal ? rect.getWidth() : rect.getHeight();
}

int ZoomScrollBar::getPrimaryPos(const juce::Rectangle<int>& rect) const {
    return orientation == Orientation::Horizontal ? rect.getX() : rect.getY();
}

}  // namespace magda
