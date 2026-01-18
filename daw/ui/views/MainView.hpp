#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../components/common/GridOverlayComponent.hpp"
#include "../components/common/SvgButton.hpp"
#include "../components/timeline/TimelineComponent.hpp"
#include "../components/timeline/ZoomManager.hpp"
#include "../components/timeline/ZoomScrollBar.hpp"
#include "../components/tracks/TrackContentPanel.hpp"
#include "../components/tracks/TrackHeadersPanel.hpp"
#include "../layout/LayoutConfig.hpp"
#include "../state/TimelineController.hpp"

namespace magica {

class MainView : public juce::Component,
                 public juce::ScrollBar::Listener,
                 public TimelineStateListener {
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

    // Loop controls
    void setLoopEnabled(bool enabled);

    // Zoom accessors
    double getHorizontalZoom() const {
        return horizontalZoom;
    }

    // Callbacks for external components
    std::function<void(double, double, bool)>
        onLoopRegionChanged;                                // (startTime, endTime, loopEnabled)
    std::function<void(double)> onPlayheadPositionChanged;  // (positionInSeconds)
    std::function<void(double, double, bool)>
        onTimeSelectionChanged;  // (startTime, endTime, hasSelection)

    // ScrollBar::Listener implementation
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    // TimelineStateListener implementation
    void timelineStateChanged(const TimelineState& state) override;
    void zoomStateChanged(const TimelineState& state) override;
    void playheadStateChanged(const TimelineState& state) override;
    void selectionStateChanged(const TimelineState& state) override;
    void loopStateChanged(const TimelineState& state) override;

    // Access to the timeline controller (for child components)
    TimelineController& getTimelineController() {
        return *timelineController;
    }
    const TimelineController& getTimelineController() const {
        return *timelineController;
    }

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

    // Grid overlay component (vertical time grid lines)
    std::unique_ptr<GridOverlayComponent> gridOverlay;

    // Selection overlay component (for time selection and loop region in track area)
    class SelectionOverlayComponent;
    std::unique_ptr<SelectionOverlayComponent> selectionOverlay;

    // Timeline state management (single source of truth)
    std::unique_ptr<TimelineController> timelineController;

    // Legacy zoom management (to be removed after full migration)
    std::unique_ptr<ZoomManager> zoomManager;
    std::unique_ptr<ZoomScrollBar> horizontalZoomScrollBar;
    std::unique_ptr<ZoomScrollBar> verticalZoomScrollBar;

    // Fixed master track row at bottom (matching track panel style)
    class MasterHeaderPanel;
    class MasterContentPanel;
    std::unique_ptr<MasterHeaderPanel> masterHeaderPanel;
    std::unique_ptr<MasterContentPanel> masterContentPanel;
    int masterStripHeight = 60;
    static constexpr int MIN_MASTER_STRIP_HEIGHT = 40;
    static constexpr int MAX_MASTER_STRIP_HEIGHT = 150;

    // Cached state from controller for quick access
    // These are updated when TimelineStateListener callbacks are called
    double horizontalZoom = 1.0;  // Pixels per second
    double verticalZoom = 1.0;    // Track height multiplier
    double timelineLength = 0.0;  // Total timeline length in seconds
    double playheadPosition = 0.0;

    // Synchronization guards to prevent infinite recursion
    bool isUpdatingTrackSelection = false;
    bool isUpdatingLoopRegion = false;
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

    // Resize handle state (horizontal - track header width)
    bool isResizingHeaders = false;
    int resizeStartX = 0;
    int resizeStartWidth = 0;
    static constexpr int RESIZE_HANDLE_WIDTH = 4;
    int lastMouseX = 0;

    // Resize handle state (vertical - master strip height)
    bool isResizingMasterStrip = false;
    int resizeStartY = 0;
    int resizeStartHeight = 0;
    static constexpr int MASTER_RESIZE_HANDLE_HEIGHT = 8;

    // Time selection and loop region are now managed by TimelineController
    // Local caches for quick access (updated via listener callbacks)
    magica::TimeSelection timeSelection;
    magica::LoopRegion loopRegion;

    // Helper methods
    void updateContentSizes();
    void syncHorizontalScrolling();
    void syncTrackHeights();
    void setupTrackSynchronization();
    void setupTimelineController();
    void setupZoomManagerCallbacks();
    void setupZoomManager();
    void setupComponents();
    void setupCallbacks();
    void resetZoomToFitTimeline();
    void syncStateFromController();

    // Resize handle helper methods
    juce::Rectangle<int> getResizeHandleArea() const;
    juce::Rectangle<int> getMasterResizeHandleArea() const;
    void paintResizeHandle(juce::Graphics& g);
    void paintMasterResizeHandle(juce::Graphics& g);

    // Selection and loop helper methods
    void setupSelectionCallbacks();
    void clearTimeSelection();
    void createLoopFromSelection();

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

// Selection overlay component that draws time selection and loop region
class MainView::SelectionOverlayComponent : public juce::Component {
  public:
    SelectionOverlayComponent(MainView& owner);
    ~SelectionOverlayComponent() override;

    void paint(juce::Graphics& g) override;

    // Hit testing - transparent to mouse events
    bool hitTest(int x, int y) override {
        return false;
    }

  private:
    MainView& owner;

    void drawTimeSelection(juce::Graphics& g);
    void drawLoopRegion(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelectionOverlayComponent)
};

// Master header panel - matches track header style with controls
class MainView::MasterHeaderPanel : public juce::Component, public TrackManagerListener {
  public:
    MasterHeaderPanel();
    ~MasterHeaderPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TrackManagerListener
    void tracksChanged() override {}
    void masterChannelChanged() override;

  private:
    std::unique_ptr<juce::Label> nameLabel;
    std::unique_ptr<juce::TextButton> muteButton;
    std::unique_ptr<juce::TextButton> soloButton;
    std::unique_ptr<juce::Slider> volumeSlider;
    std::unique_ptr<juce::Slider> panSlider;

    void setupControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterHeaderPanel)
};

// Master content panel - empty for now, will show waveform later
class MainView::MasterContentPanel : public juce::Component {
  public:
    MasterContentPanel();
    ~MasterContentPanel() override = default;

    void paint(juce::Graphics& g) override;

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterContentPanel)
};

}  // namespace magica
