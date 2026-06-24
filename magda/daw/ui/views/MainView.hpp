#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../components/common/DraggableValueLabel.hpp"
#include "../components/common/GridOverlayComponent.hpp"
#include "../components/common/SvgButton.hpp"
#include "../components/timeline/MarkerLaneComponent.hpp"
#include "../components/timeline/TimelineComponent.hpp"
#include "../components/timeline/ZoomScrollBar.hpp"
#include "../components/tracks/TrackContentPanel.hpp"
#include "../components/tracks/TrackHeadersPanel.hpp"
#include "../layout/LayoutConfig.hpp"
#include "../state/TimelineController.hpp"
#include "core/AutomationManager.hpp"
#include "core/GestureRouter.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// Forward declaration
class AudioEngine;
class SongNavigatorPanel;
class MasterAutomationHeaderPanel;
class MasterAutomationContentPanel;
class ClickableLabel;
class LevelMeter;

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
        onGridQuantizeChanged;                   // (autoGrid, numerator, denominator, isBars)
    std::function<void(double)> onTempoChanged;  // (bpm)
    std::function<void(int, int)> onTimeSignatureChanged;     // (numerator, denominator)
    std::function<void(ClipId)> onClipRenderRequested;        // Render clip to new file
    std::function<void()> onRenderTimeSelectionRequested;     // Render time selection
    std::function<void(ClipId)> onBounceInPlaceRequested;     // Bounce MIDI clip in place
    std::function<void(ClipId)> onBounceToNewTrackRequested;  // Bounce clip to new track

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
    std::unique_ptr<juce::Viewport> markerLaneViewport;
    std::unique_ptr<MarkerLaneComponent> markerLane;
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
    class MasterContentPanel;  // legacy "Master Output" placeholder (kept for easy revert)
    std::unique_ptr<MasterHeaderPanel> masterHeaderPanel;
    // Song navigator / minimap occupying the master content strip (issue #1474).
    std::unique_ptr<SongNavigatorPanel> masterContentPanel;
    int masterStripHeight = 84;

    // Master automation band: a pinned strip directly above the master strip
    // hosting the master channel's automation lanes (issue #1482). Fixed-width
    // header column + a horizontally scroll-synced content viewport.
    std::unique_ptr<MasterAutomationHeaderPanel> masterAutomationHeaderPanel;
    std::unique_ptr<juce::Viewport> masterAutomationViewport;
    std::unique_ptr<MasterAutomationContentPanel> masterAutomationContentPanel;
    int masterAutomationHeight = 0;  // computed band height; 0 collapses the band
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
    static constexpr int MIN_MASTER_STRIP_HEIGHT = 84;
    static constexpr int MAX_MASTER_STRIP_HEIGHT = 150;

    // Cached state from controller for quick access
    // These are updated when TimelineStateListener callbacks are called
    double horizontalZoom = 1.0;  // Pixels per beat
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
    int getMarkerLaneHeight() const {
        return markerLaneVisible_ ? LayoutConfig::getInstance().markerLaneHeight : 0;
    }
    // The ruler height is fixed; the seconds row is one of its internal rows, so
    // toggling it never changes the total height.
    int getTimelineHeight() const {
        return getMarkerLaneHeight() + LayoutConfig::getInstance().getTimelineBodyHeight();
    }
    int trackHeaderWidth = LayoutConfig::getInstance().defaultTrackHeaderWidth;
    bool markerLaneVisible_ = true;
    bool secondsRulerVisible_ = false;
    static constexpr int ARRANGEMENT_SCROLLBAR_SIZE = 20;

    void dispatchUserPlayheadPositionBeats(double positionBeats, bool bypassSnap);

    struct ArrangementLayout {
        bool swapped = false;
        juce::Rectangle<int> cornerArea;
        juce::Rectangle<int> markerLaneArea;
        juce::Rectangle<int> timelineArea;
        juce::Rectangle<int> trackHeadersArea;
        juce::Rectangle<int> trackContentArea;
        juce::Rectangle<int> overlayArea;
        juce::Rectangle<int> playheadArea;
        juce::Rectangle<int> horizontalScrollBarArea;
        juce::Rectangle<int> verticalScrollBarArea;
        juce::Rectangle<int> horizontalScrollBarRowArea;
        juce::Rectangle<int> horizontalScrollBarHitArea;
        juce::Rectangle<int> verticalScrollBarHitArea;
        juce::Rectangle<int> masterHeaderArea;
        juce::Rectangle<int> masterContentArea;
        juce::Rectangle<int> masterAutomationHeaderArea;
        juce::Rectangle<int> masterAutomationContentArea;
        juce::Rectangle<int> auxHeadersArea;
        juce::Rectangle<int> auxContentArea;
    };
    ArrangementLayout computeArrangementLayout() const;

    float horizontalScrollbarRevealProgress = 0.0f;
    float verticalScrollbarRevealProgress = 0.0f;
    int horizontalScrollbarRevealFrames = 0;
    int verticalScrollbarRevealFrames = 0;
    int horizontalHoverDwellFrames = 0;
    int verticalHoverDwellFrames = 0;
    bool isHorizontalScrollbarHovered = false;
    bool isVerticalScrollbarHovered = false;
    bool isUpdatingArrangementScrollbarLayout = false;
    juce::Rectangle<int> horizontalScrollbarHitArea;
    juce::Rectangle<int> verticalScrollbarHitArea;
    // While the window is being resized the content's visible fraction changes,
    // which fires the scrollbars' onRangeChanged and would pop them open. Track
    // the last laid-out size so resized() can detect a genuine size change and
    // arm a short window during which reveals are ignored.
    int previousArrangementWidth = 0;
    int previousArrangementHeight = 0;
    int arrangementScrollbarResizeSuppressFrames = 0;
    // Fade timings target 60Hz timer (~16ms/frame). Steps chosen so:
    //   fade-in   ~12 frames (~200ms), fade-out ~30 frames (~500ms).
    static constexpr float ARRANGEMENT_SCROLLBAR_FADE_IN_STEP = 0.08f;
    static constexpr float HORIZONTAL_SCROLLBAR_FADE_OUT_STEP = 0.028f;
    static constexpr float VERTICAL_SCROLLBAR_FADE_OUT_STEP = 0.04f;
    // Hold frames keep the scrollbar fully visible briefly after the trigger
    // ends (mouse exit, last scroll/zoom event) so small mouse movements or
    // pauses between scrolls don't restart the fade cycle. ~300ms at 60Hz.
    static constexpr int ARRANGEMENT_SCROLLBAR_REVEAL_HOLD_FRAMES = 18;
    // Reveal hit strip is intentionally narrower than the scrollbar's visible
    // width — the cursor has to be pushed right against the panel edge to
    // reveal it. Avoids accidental triggers when editing clips near bar 1.
    // (.reduced(1, 0) below trims to ~6px effective.)
    static constexpr int ARRANGEMENT_SCROLLBAR_HIT_EDGE = 8;
    // Dwell time before edge-hover triggers a reveal — filters out quick
    // grazes through the hit strip in transit. ~80ms at 60Hz. Bypassed
    // while the bar is already visible so the user can still re-grab it.
    static constexpr int ARRANGEMENT_SCROLLBAR_HOVER_DWELL_FRAMES = 5;
    // Reveals are ignored for this many frames after a window resize so the
    // bars don't flash while dragging the window edge. Re-armed on every resize
    // event, so it stays suppressed for the whole drag and ~150ms after. 60Hz.
    static constexpr int ARRANGEMENT_SCROLLBAR_RESIZE_SUPPRESS_FRAMES = 10;

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
    // Translate a resolved arrangement mouse-gesture (#21 GestureRouter) into
    // the corresponding TimelineController / viewport action.
    void dispatchArrangementGesture(const ResolvedGesture& gesture);
    // Apply a new vertical track-height zoom multiplier and resync the panels.
    void applyVerticalZoom(double newVerticalZoom);
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
    void revealHorizontalArrangementScrollbar();
    void revealVerticalArrangementScrollbar();
    void updateArrangementScrollbarVisibility();
    void updateArrangementScrollbarHover(const juce::MouseEvent& event);

    // Grid division display (shown on horizontal zoom scroll bar)
    void updateGridDivisionDisplay();
    juce::String calculateGridDivisionString() const;
    void calculateSmartGridNumeratorDenominator(int& outNum, int& outDen, bool& outIsBars) const;

    // Audio engine reference for metering
    AudioEngine* audioEngine_ = nullptr;

    // Corner toolbar buttons (above track headers)
    std::unique_ptr<SvgButton> zoomFitButton;
    std::unique_ptr<SvgButton> zoomSelButton;
    std::unique_ptr<SvgButton> markerLaneToggleButton;
    std::unique_ptr<SvgButton> trackSmallButton;
    std::unique_ptr<SvgButton> trackMediumButton;
    std::unique_ptr<SvgButton> trackLargeButton;
    std::unique_ptr<SvgButton> zoomLoopButton;
    std::unique_ptr<SvgButton> secondsRulerToggleButton;
    std::unique_ptr<SvgButton> ioToggleButton;
    std::unique_ptr<SvgButton> addTrackButton;
    std::unique_ptr<SvgButton> showMasterButton;
    std::unique_ptr<SvgButton> hAxisIcon;
    std::unique_ptr<SvgButton> vAxisIcon;

    // Separator line positions in the corner toolbar (set during resized())
    juce::Rectangle<int> markerLaneSeparatorLine;
    juce::Rectangle<int> cornerSeparatorLine;
    juce::Rectangle<int> cornerBottomBorderLine;
    // Vertical border on the marker-lane row, separating the corner gutter
    // from the marker-lane content to its side.
    juce::Rectangle<int> markerCornerRightBorderLine;

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
    bool hitTest(int /*x*/, int /*y*/) override {
        return false;
    }

  private:
    MainView& owner;

    void drawTimeSelection(juce::Graphics& g);
    void drawLoopRegion(juce::Graphics& g);
    void drawRecordingRegion(juce::Graphics& g);

    // Paints `bandRect` (overlay-local coords) by snapshotting the matching
    // region of trackContentPanel, inverting RGB with reduced saturation, and
    // drawing it back. The colour-inversion (rather than a simple blue tint)
    // is what makes the band readable on a black-painted waveform without
    // making the whole clip look like a different colour clip.
    void paintTimeSelectionBand(juce::Graphics& g, juce::Rectangle<int> bandRect);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelectionOverlayComponent)
};

// Master header panel - matches track header style with controls
class MainView::MasterHeaderPanel : public juce::Component,
                                    public TrackManagerListener,
                                    public AutomationManagerListener {
  public:
    MasterHeaderPanel();
    ~MasterHeaderPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // TrackManagerListener
    void tracksChanged() override {}
    void masterChannelChanged() override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;

    // Meter level updates (for audio engine integration)
    void setPeakLevels(float leftPeak, float rightPeak);

  private:
    // Opens the master automation menu (mirrors the per-track automation
    // button). Shared by the icon button and the header right-click.
    void showMasterAutomationMenu(juce::Component* anchor);

    std::unique_ptr<SvgButton> speakerButton;          // Speaker on/off toggle
    std::unique_ptr<SvgButton> automationButton;       // Show master automation lane
    std::unique_ptr<SvgButton> hideButton;             // Hide master row in this view
    std::unique_ptr<DraggableValueLabel> volumeLabel;  // Volume as draggable dB label
    std::unique_ptr<ClickableLabel> peakValueLabel;    // Click to reset held peak
    float peakValue_ = 0.0f;

    std::unique_ptr<LevelMeter> peakMeter;  // Horizontal stereo peak meter

    void setupControls();
    void updateAutomationButtonState();

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
