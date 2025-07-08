#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace magica {

/**
 * @brief Pure zoom and scroll coordinator for DAW components
 * 
 * Handles zoom calculations and scroll coordination through callbacks.
 * No direct references to UI components - communicates entirely through callbacks.
 */
class ZoomManager {
public:
    ZoomManager();
    ~ZoomManager() = default;

    // Core zoom operations
    void setZoom(double newZoom);
    void setZoomCentered(double newZoom, double timePosition);
    void setZoomFromMouseDrag(double newZoom, int mouseX, int viewportWidth);
    
    // Configuration
    void setTimelineLength(double lengthInSeconds);
    void setViewportWidth(int width);
    void setCurrentScrollPosition(int scrollX);
    
    // Zoom properties
    double getCurrentZoom() const { return currentZoom; }
    double getMinZoom() const { return minZoom; }
    double getMaxZoom() const { return maxZoom; }
    double getTimelineLength() const { return timelineLength; }
    int getCurrentScrollPosition() const { return currentScrollX; }
    
    // Zoom bounds
    void setZoomBounds(double minZoom, double maxZoom);
    
    // Callbacks - MainView registers these to update UI components
    std::function<void(double newZoom)> onZoomChanged;
    std::function<void(int scrollX)> onScrollChanged;
    std::function<void(int contentWidth)> onContentSizeChanged;

private:
    // Zoom and scroll state
    double currentZoom = 1.0;
    double minZoom = 0.1;
    double maxZoom = 100000.0;
    double timelineLength = 120.0;
    int viewportWidth = 800;
    int currentScrollX = 0;
    
    // Helper methods
    void notifyZoomChanged();
    void notifyScrollChanged(int newScrollX);
    void notifyContentSizeChanged();
    int calculateContentWidth() const;
    double pixelToTime(int pixel) const;
    int timeToPixel(double time) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomManager)
};

} // namespace magica 