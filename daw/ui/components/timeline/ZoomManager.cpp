#include "ZoomManager.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "Config.hpp"

namespace magica {

ZoomManager::ZoomManager() {
    // Load configuration values
    auto& config = magica::Config::getInstance();
    minZoom = config.getMinZoomLevel();
    maxZoom = config.getMaxZoomLevel();
    timelineLength = config.getDefaultTimelineLength();

    std::cout << "🎯 ZOOM MANAGER: initialized with minZoom=" << minZoom << ", maxZoom=" << maxZoom
              << ", timelineLength=" << timelineLength << std::endl;
}

void ZoomManager::setZoom(double newZoom) {
    currentZoom = juce::jlimit(minZoom, maxZoom, newZoom);
    notifyZoomChanged();
    notifyContentSizeChanged();
}

void ZoomManager::setZoomCentered(double newZoom, double timePosition) {
    currentZoom = juce::jlimit(minZoom, maxZoom, newZoom);

    // Ensure content is large enough to allow centering
    int contentWidth = calculateContentWidth();
    int viewportCenter = viewportWidth / 2;

    // Calculate where this time position appears in content coordinates (with LEFT_PADDING)
    int timeContentX = static_cast<int>(timePosition * currentZoom) + 18;

    // Calculate scroll position needed to center this position in viewport
    int idealScrollX = timeContentX - viewportCenter;

    // Clamp scroll position to valid range
    int maxScrollX = juce::jmax(0, contentWidth - viewportWidth);
    int newScrollX = juce::jlimit(0, maxScrollX, idealScrollX);

    // Update internal state
    currentScrollX = newScrollX;

    // Notify all listeners
    notifyZoomChanged();
    notifyContentSizeChanged();
    notifyScrollChanged(newScrollX);
}

void ZoomManager::setZoomFromMouseDrag(double newZoom, int mouseX, int viewportWidth) {
    // Calculate the time position under the mouse cursor BEFORE zoom change
    int absoluteMouseX = mouseX + currentScrollX;
    double timeUnderCursor = pixelToTime(absoluteMouseX);

    std::cout << "🎯 ZOOM DRAG: mouseX=" << mouseX << ", scrollX=" << currentScrollX
              << ", absMouseX=" << absoluteMouseX << ", timeUnderCursor=" << timeUnderCursor
              << std::endl;

    // Apply the new zoom
    double oldZoom = currentZoom;
    currentZoom = juce::jlimit(minZoom, maxZoom, newZoom);

    // Calculate where that time position should be after zoom to keep it under mouse cursor
    int desiredPixelPos = timeToPixel(timeUnderCursor);
    int newScrollX = desiredPixelPos - mouseX;

    // Clamp scroll position to valid range
    int contentWidth = calculateContentWidth();
    int maxScrollX = juce::jmax(0, contentWidth - viewportWidth);
    newScrollX = juce::jlimit(0, maxScrollX, newScrollX);

    // Update internal state
    currentScrollX = newScrollX;

    // Notify all listeners
    notifyZoomChanged();
    notifyContentSizeChanged();
    notifyScrollChanged(newScrollX);

    std::cout << "🎯 ZOOM RESULT: oldZoom=" << oldZoom << ", newZoom=" << currentZoom
              << ", desiredPixel=" << desiredPixelPos << ", newScrollX=" << newScrollX << std::endl;
}

void ZoomManager::setTimelineLength(double lengthInSeconds) {
    timelineLength = lengthInSeconds;
    notifyContentSizeChanged();
}

void ZoomManager::setViewportWidth(int width) {
    viewportWidth = width;
}

void ZoomManager::setCurrentScrollPosition(int scrollX) {
    currentScrollX = scrollX;
}

void ZoomManager::setZoomBounds(double minZoom, double maxZoom) {
    this->minZoom = minZoom;
    this->maxZoom = maxZoom;

    // Ensure current zoom is within bounds
    if (currentZoom < minZoom || currentZoom > maxZoom) {
        setZoom(juce::jlimit(minZoom, maxZoom, currentZoom));
    }
}

void ZoomManager::notifyZoomChanged() {
    if (onZoomChanged) {
        onZoomChanged(currentZoom);
    }
}

void ZoomManager::notifyScrollChanged(int newScrollX) {
    currentScrollX = newScrollX;
    if (onScrollChanged) {
        onScrollChanged(newScrollX);
    }
}

void ZoomManager::notifyContentSizeChanged() {
    int contentWidth = calculateContentWidth();
    if (onContentSizeChanged) {
        onContentSizeChanged(contentWidth);
    }
}

int ZoomManager::calculateContentWidth() const {
    // Base content width from timeline
    int baseWidth = static_cast<int>(timelineLength * currentZoom);

    // Ensure content is at least viewport width + extra padding for centering
    int minWidth = viewportWidth + (viewportWidth / 2);  // 1.5x viewport width

    return juce::jmax(baseWidth, minWidth);
}

double ZoomManager::pixelToTime(int pixel) const {
    if (currentZoom > 0) {
        // Account for LEFT_PADDING (18 pixels) used by TimelineComponent
        return (pixel - 18) / currentZoom;
    }
    return 0.0;
}

int ZoomManager::timeToPixel(double time) const {
    // TimelineComponent adds LEFT_PADDING when drawing, not in conversion
    return static_cast<int>(time * currentZoom);
}

void ZoomManager::notifyCursorChanged(juce::MouseCursor::StandardCursorType cursor) {
    if (onCursorChanged) {
        onCursorChanged(cursor);
    }
}

}  // namespace magica
