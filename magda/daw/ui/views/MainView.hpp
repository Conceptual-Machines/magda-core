#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../components/common/DraggableValueLabel.hpp"
#include "../components/common/GridOverlayComponent.hpp"
#include "../components/common/SvgButton.hpp"
#include "../components/timeline/TimelineComponent.hpp"
#include "../components/timeline/ZoomScrollBar.hpp"
#include "../components/tracks/TrackContentPanel.hpp"
#include "../components/tracks/TrackHeadersPanel.hpp"
#include "../layout/LayoutConfig.hpp"
#include "../state/TimelineController.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// Forward declaration
class AudioEngine;

class MainView : public juce::Component,
                 public juce::ScrollBar::Listener,
                 public juce::Timer,
                 public TimelineStateListener,
                 public TrackManagerListener,
                 public ViewModeListener {
  public:
    MainView(AudioEngine* audioEngine = nullptr);
    ~MainView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Zoom and scroll controls
    void setHorizontalZoom(double zoomFactor);
    void setVerticalZoom(double zoomFactor);
    void scrollToPosition(double timePosition);
    void scrollToTrack(int trackIndex);

    // Track management
    void selectTrack(int trackIndex);

    // Timeline controls
    void setTimelineLength(double lengthInSeconds);
    void setPlayheadPosition(double position);

    // Arrangement controls
    void toggleArrangementLock();
    bool isArrangementLocked() const;

    // Loop controls
    void setLoopEnabled(bool enabled);

    // Snap controls
    void syncSnapState();

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
    std::function<void(double, double, bool, bool)>
        onPunchRegionChanged;  // (startTime, endTime, punchInEnabled, punchOutEnabled)
    std::function<void(double)> onEditCursorChanged;  // (positionInSeconds)
    std::function<void(bool, int, int, bool)>
        onGridQuantizeChanged;  // (autoGrid, numerator, denominator, isBars)
    std::function<void(ClipId)> onClipRenderRequested;     // Render clip to new file
    std::function<void()> onRenderTimeSelectionRequested;  // Render time selection

    // ScrollBar::Listener implementation
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    // TimelineStateListener implementation
    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

    // TrackManagerListener implementation
    void tracksChanged() override;
    void masterChannelChanged() override;

    // ViewModeListener implementation
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // Timer implementation (for metering updates)
    void timerCallback() override;

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
    // Timeline state management (single source of truth)
    // IMPORTANT: Must be declared before all components that register as listeners,
    // so it is destroyed AFTER them (C++ reverse destruction order).
    std::unique_ptr<TimelineController> timelineController;

    // Timeline viewport (horizontal scroll only)
    std::unique_ptr<juce::Viewport> timelineViewport;
    std::unique_ptr<TimelineComponent> timeline;

    // Track headers viewport (vertical scroll synced with track content)
    std::unique_ptr<juce::Viewport> trackHeadersViewport;
    std::unique_ptr<TrackHeadersPanel> trackHeadersPanel;

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

    // Zoom scroll bars
    std::unique_ptr<ZoomScrollBar> horizontalZoomScrollBar;
    std::unique_ptr<ZoomScrollBar> verticalZoomScrollBar;

    // Fixed master track row at bottom (matching track panel style)
    class MasterHeaderPanel;
    class MasterContentPanel;
    std::unique_ptr<MasterHeaderPanel> masterHeaderPanel;
    std::unique_ptr<MasterContentPanel> masterContentPanel;
    int masterStripHeight = 60;
    ViewMode currentViewMode_ = ViewMode::Arrange;
    bool masterVisible_ = true;

    // Fixed aux track section above master (one row per aux track)
    class AuxHeadersPanel;
    class AuxContentPanel;
    std::unique_ptr<AuxHeadersPanel> auxHeadersPanel;
    std::unique_ptr<AuxContentPanel> auxContentPanel;
    int auxSectionHeight = 0;
    bool auxVisible_ = false;
    static constexpr int AUX_ROW_HEIGHT = 30;
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
    bool isUpdatingFromVerticalZoomScrollBar = false;

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
    static constexpr int MASTER_RESIZE_HANDLE_HEIGHT = 4;

    // Time selection and loop region are now managed by TimelineController
    // Local caches for quick access (updated via listener callbacks)
    magda::TimeSelection timeSelection;
    magda::LoopRegion loopRegion;

    // Helper methods
    void updateContentSizes();
    void syncHorizontalScrolling();
    void syncTrackHeights();
    void setupTrackSynchronization();
    void setupTimelineController();
    void setupTimelineCallbacks();
    void setupComponents();
    void setupCallbacks();
    void resetZoomToFitTimeline();
    void zoomToSelection();
    void setAllTrackHeights(int height);
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

    // Grid division display (shown on horizontal zoom scroll bar)
    void updateGridDivisionDisplay();
    juce::String calculateGridDivisionString() const;
    void calculateSmartGridNumeratorDenominator(int& outNum, int& outDen, bool& outIsBars) const;

    // Audio engine reference for metering
    AudioEngine* audioEngine_ = nullptr;

    // Corner toolbar buttons (above track headers)
    std::unique_ptr<SvgButton> zoomFitButton;
    std::unique_ptr<SvgButton> zoomSelButton;
    std::unique_ptr<SvgButton> trackCompactButton;
    std::unique_ptr<SvgButton> trackExpandButton;

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
    void drawRecordingRegion(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelectionOverlayComponent)
};

// Master header panel - matches track header style with controls
class MainView::MasterHeaderPanel : public juce::Component, public TrackManagerListener {
  public:
    MasterHeaderPanel();
    ~MasterHeaderPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // TrackManagerListener
    void tracksChanged() override {}
    void masterChannelChanged() override;

    // Meter level updates (for audio engine integration)
    void setPeakLevels(float leftPeak, float rightPeak);
    void setVuLevels(float leftVu, float rightVu);

  private:
    std::unique_ptr<juce::Label> nameLabel;
    std::unique_ptr<juce::DrawableButton> speakerButton;  // Speaker on/off toggle
    std::unique_ptr<DraggableValueLabel> volumeLabel;     // Volume as draggable dB label
    std::unique_ptr<DraggableValueLabel> panLabel;        // Pan as draggable L/C/R label

    // Horizontal stereo meter component (used for both peak and VU)
    class HorizontalStereoMeter;
    std::unique_ptr<HorizontalStereoMeter> peakMeter;  // Fast peak meter
    std::unique_ptr<HorizontalStereoMeter> vuMeter;    // Slow VU meter
    std::unique_ptr<juce::Label> peakLabel;            // "Peak" label
    std::unique_ptr<juce::Label> vuLabel;              // "VU" label
    std::unique_ptr<juce::Label> peakValueLabel;       // Peak dB value
    std::unique_ptr<juce::Label> vuValueLabel;         // VU dB value

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

// Aux headers panel - one row per aux track with name, volume, pan, mute/solo
class MainView::AuxHeadersPanel : public juce::Component, public TrackManagerListener {
  public:
    AuxHeadersPanel();
    ~AuxHeadersPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // TrackManagerListener
    void tracksChanged() override;

    // Metering
    void updateMetering(AudioEngine* engine);

    // Get number of aux tracks
    int getAuxTrackCount() const {
        return static_cast<int>(auxRows_.size());
    }

  private:
    struct AuxRow {
        TrackId trackId = INVALID_TRACK_ID;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<DraggableValueLabel> volumeLabel;
        std::unique_ptr<DraggableValueLabel> panLabel;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
    };

    std::vector<std::unique_ptr<AuxRow>> auxRows_;
    void rebuildAuxRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuxHeadersPanel)
};

// Aux content panel - empty background (aux tracks don't have timeline clips)
class MainView::AuxContentPanel : public juce::Component {
  public:
    AuxContentPanel() = default;
    ~AuxContentPanel() override = default;

    void paint(juce::Graphics& g) override;

    void setAuxTrackCount(int count) {
        auxTrackCount_ = count;
        repaint();
    }

  private:
    int auxTrackCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuxContentPanel)
};

}  // namespace magda
