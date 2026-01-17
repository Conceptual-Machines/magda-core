#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../components/common/LayoutDebugPanel.hpp"
#include "../components/common/SvgButton.hpp"
#include "../components/timeline/TimelineComponent.hpp"
#include "../components/timeline/ZoomManager.hpp"
#include "../components/timeline/ZoomScrollBar.hpp"
#include "../components/tracks/TrackContentPanel.hpp"
#include "../components/tracks/TrackHeadersPanel.hpp"
#include "../layout/LayoutConfig.hpp"

namespace magica {

class MainView : public juce::Component, public juce::ScrollBar::Listener {
  public:
    MainView();
    ~MainView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Zoom and scroll controls
    void setHorizontalZoom(double zoomFactor);
    void setVerticalZoom(double zoomFactor);
    void scrollToPosition(double timePosition);
    void scrollToTrack(int trackIndex);

    // Track management
    void addTrack();
    void removeTrack(int trackIndex);
    void selectTrack(int trackIndex);

    // Timeline controls
    void setTimelineLength(double lengthInSeconds);
    void setPlayheadPosition(double position);

    // Arrangement controls
    void toggleArrangementLock();
    bool isArrangementLocked() const;

    // Zoom accessors
    double getHorizontalZoom() const {
        return horizontalZoom;
    }

    // ScrollBar::Listener implementation
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;

    // Mouse handling for zoom
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

  private:
    // Timeline viewport (horizontal scroll only)
    std::unique_ptr<juce::Viewport> timelineViewport;
    std::unique_ptr<TimelineComponent> timeline;

    // Track headers panel (fixed, no scrolling)
    std::unique_ptr<TrackHeadersPanel> trackHeadersPanel;

    // Arrangement lock button
    std::unique_ptr<SvgButton> arrangementLockButton;

    // Time display mode toggle button
    std::unique_ptr<juce::TextButton> timeDisplayToggleButton;

    // Track content viewport (both horizontal and vertical scroll)
    std::unique_ptr<juce::Viewport> trackContentViewport;
    std::unique_ptr<TrackContentPanel> trackContentPanel;

    // Playhead component (always on top)
    class PlayheadComponent;
    std::unique_ptr<PlayheadComponent> playheadComponent;

    // Zoom management
    std::unique_ptr<ZoomManager> zoomManager;
    std::unique_ptr<ZoomScrollBar> horizontalZoomScrollBar;
    std::unique_ptr<ZoomScrollBar> verticalZoomScrollBar;

    // Layout debug panel (F11 to toggle)
    std::unique_ptr<LayoutDebugPanel> layoutDebugPanel;

    // Zoom and scroll state
    double horizontalZoom = 1.0;  // Pixels per second
    double verticalZoom = 1.0;    // Track height multiplier
    double timelineLength = 0.0;  // Total timeline length in seconds (loaded from config)
    double playheadPosition = 0.0;

    // Synchronization guard to prevent infinite recursion
    bool isUpdatingTrackSelection = false;
    // Note: Zoom/scroll race conditions are prevented by temporarily removing scroll bar listeners

    // Initial zoom setup flag
    bool initialZoomSet = false;

    // Zoom anchor tracking (for smooth zoom centering)
    bool isZoomActive = false;
    int zoomAnchorViewportX = 0;  // Viewport-relative position to keep stable

    // Layout - uses LayoutConfig for centralized configuration
    int getTimelineHeight() const {
        return LayoutConfig::getInstance().getTimelineHeight();
    }
    int trackHeaderWidth = LayoutConfig::getInstance().defaultTrackHeaderWidth;

    // Resize handle state
    bool isResizingHeaders = false;
    int resizeStartX = 0;
    int resizeStartWidth = 0;
    static constexpr int RESIZE_HANDLE_WIDTH = 4;
    int lastMouseX = 0;

    // Helper methods
    void updateContentSizes();
    void syncHorizontalScrolling();
    void syncTrackHeights();
    void setupTrackSynchronization();
    void setupZoomManagerCallbacks();
    void setupZoomManager();
    void setupComponents();
    void setupCallbacks();
    void resetZoomToFitTimeline();

    // Resize handle helper methods
    juce::Rectangle<int> getResizeHandleArea() const;
    void paintResizeHandle(juce::Graphics& g);

    // Zoom scroll bar synchronization
    void updateHorizontalZoomScrollBar();
    void updateVerticalZoomScrollBar();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainView)
};

// Dedicated playhead component that always stays on top
class MainView::PlayheadComponent : public juce::Component {
  public:
    PlayheadComponent(MainView& owner);
    ~PlayheadComponent() override;

    void paint(juce::Graphics& g) override;
    void setPlayheadPosition(double position);

    // Hit testing to only intercept clicks near the playhead
    bool hitTest(int x, int y) override;

    // Mouse handling for dragging playhead
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

  private:
    MainView& owner;
    double playheadPosition = 0.0;
    bool isDragging = false;
    int dragStartX = 0;
    double dragStartPosition = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlayheadComponent)
};

}  // namespace magica
