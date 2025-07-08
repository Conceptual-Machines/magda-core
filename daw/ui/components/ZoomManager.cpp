#include "ZoomManager.hpp"
#include <iostream>

namespace magica {

ZoomManager::ZoomManager() {
    // Set default zoom bounds
    setZoomBounds(0.1, 100000.0);
}

void ZoomManager::setZoom(double newZoom) {
    currentZoom = juce::jlimit(minZoom, maxZoom, newZoom);
    notifyZoomChanged();
    notifyContentSizeChanged();
}

void ZoomManager::setZoomCentered(double newZoom, double timePosition) {
    double oldZoom = currentZoom;
    currentZoom = juce::jlimit(minZoom, maxZoom, newZoom);
    
    // Calculate where this time position should be after zoom
    int desiredPixelPos = timeToPixel(timePosition);
    int newScrollX = desiredPixelPos - (viewportWidth / 2);
    
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
    
    std::cout << "🎯 ZOOM CENTERED: oldZoom=" << oldZoom << ", newZoom=" << currentZoom 
              << ", timePos=" << timePosition << ", newScrollX=" << newScrollX << std::endl;
}

void ZoomManager::setZoomFromMouseDrag(double newZoom, int mouseX, int viewportWidth) {
    // Calculate the time position under the mouse cursor BEFORE zoom change
    int absoluteMouseX = mouseX + currentScrollX;
    double timeUnderCursor = pixelToTime(absoluteMouseX);
    
    std::cout << "🎯 ZOOM DRAG: mouseX=" << mouseX << ", scrollX=" << currentScrollX 
              << ", absMouseX=" << absoluteMouseX << ", timeUnderCursor=" << timeUnderCursor << std::endl;
    
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
    return static_cast<int>(timelineLength * currentZoom);
}

double ZoomManager::pixelToTime(int pixel) const {
    if (currentZoom > 0) {
        return pixel / currentZoom;
    }
    return 0.0;
}

int ZoomManager::timeToPixel(double time) const {
    return static_cast<int>(time * currentZoom);
}

} // namespace magica 