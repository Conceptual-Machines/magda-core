#include "MainView.hpp"

#include <BinaryData.h>

#include <cmath>
#include <iostream>
#include <set>

#include "../components/common/SideColumn.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "Config.hpp"
#include "audio/AudioBridge.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda {

// dB conversion helpers for meters
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;

float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

// Convert dB to normalized meter position (0-1) with power curve
// Matches the track meter scaling in TrackHeadersPanel
float dbToMeterPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    // Normalize to 0-1 range
    float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);

    // Apply power curve: y = x^3
    // -12 dB → ~38%, 0 dB → ~75%, +6 dB → 100%
    return std::pow(normalized, 3.0f);
}

}  // namespace

MainView::MainView(AudioEngine* audioEngine)
    : audioEngine_(audioEngine),
      playheadPosition(0.0),
      horizontalZoom(10.0),
      initialZoomSet(false) {
    // Load configuration
    auto& config = magda::Config::getInstance();
    config.loadFromFile("magda_config.txt");  // Load from file if it exists
    timelineLength = config.getDefaultTimelineLength();

    std::cout << "🎯 CONFIG: Timeline length=" << timelineLength << " seconds" << std::endl;
    std::cout << "🎯 CONFIG: Default zoom view=" << config.getDefaultZoomViewDuration()
              << " seconds" << std::endl;

    // Make this component focusable to receive keyboard events
    setWantsKeyboardFocus(true);

    // Set up the centralized timeline controller
    setupTimelineController();

    // Set up UI components
    setupComponents();

    // Set up callbacks
    setupCallbacks();

    // Set up timeline zoom/scroll callbacks
    setupTimelineCallbacks();

    // Register as TrackManager and ViewModeController listener
    TrackManager::getInstance().addListener(this);
    ViewModeController::getInstance().addListener(this);
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Initialize master visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    masterVisible_ = master.isVisibleIn(currentViewMode_);

    // Start timer for metering updates (30 FPS)
    startTimerHz(30);
}

void MainView::setupTimelineController() {
    timelineController = std::make_unique<TimelineController>();
    timelineController->addListener(this);

    // Sync initial state from controller
    syncStateFromController();
}

void MainView::syncStateFromController() {
    const auto& state = timelineController->getState();

    // Update cached values
    horizontalZoom = state.zoom.horizontalZoom;
    verticalZoom = state.zoom.verticalZoom;
    timelineLength = state.timelineLength;
    playheadPosition = state.playhead.getPosition();

    // Update selection and loop caches
    timeSelection = state.selection;
    loopRegion = state.loop;
}

void MainView::setupComponents() {
    // Create timeline viewport
    timelineViewport = std::make_unique<juce::Viewport>();
    timeline = std::make_unique<TimelineComponent>();
    timeline->setController(timelineController.get());  // Connect to centralized state
    timelineViewport->setViewedComponent(timeline.get(), false);
    timelineViewport->setScrollBarsShown(false, false);
    addAndMakeVisible(*timelineViewport);

    // Create track headers viewport and panel (vertical scroll synced with content viewport)
    trackHeadersViewport = std::make_unique<juce::Viewport>();
    trackHeadersViewport->setScrollBarsShown(false, false);  // No scrollbars - synced externally
    trackHeadersPanel = std::make_unique<TrackHeadersPanel>(audioEngine_);
    trackHeadersViewport->setViewedComponent(trackHeadersPanel.get(),
                                             false);  // false = don't delete
    addAndMakeVisible(*trackHeadersViewport);

    // Create track content viewport
    trackContentViewport = std::make_unique<juce::Viewport>();
    trackContentViewport->setWantsKeyboardFocus(
        false);  // Let TrackContentPanel handle keyboard focus
    trackContentPanel = std::make_unique<TrackContentPanel>();
    trackContentPanel->setController(timelineController.get());  // Connect to centralized state
    trackContentPanel->setAudioEngine(audioEngine_);
    trackContentViewport->setViewedComponent(trackContentPanel.get(), false);
    trackContentViewport->setScrollBarsShown(false, false, true, true);
    addAndMakeVisible(*trackContentViewport);

    // Create grid overlay component (vertical time grid lines - below selection and playhead)
    gridOverlay = std::make_unique<GridOverlayComponent>();
    gridOverlay->setController(timelineController.get());
    addAndMakeVisible(*gridOverlay);

    // Create selection overlay component (below playhead)
    selectionOverlay = std::make_unique<SelectionOverlayComponent>(*this);
    addAndMakeVisible(*selectionOverlay);

    // Create playhead component (always on top)
    playheadComponent = std::make_unique<PlayheadComponent>(*this);
    addAndMakeVisible(*playheadComponent);
    playheadComponent->toFront(false);

    // Create fixed aux track section (above master)
    auxHeadersPanel = std::make_unique<AuxHeadersPanel>();
    addAndMakeVisible(*auxHeadersPanel);
    auxHeadersPanel->setVisible(false);
    auxContentPanel = std::make_unique<AuxContentPanel>();
    addAndMakeVisible(*auxContentPanel);
    auxContentPanel->setVisible(false);

    // Create fixed master track row at bottom (matching track panel style)
    masterHeaderPanel = std::make_unique<MasterHeaderPanel>();
    addAndMakeVisible(*masterHeaderPanel);
    masterContentPanel = std::make_unique<MasterContentPanel>();
    addAndMakeVisible(*masterContentPanel);

    // Create horizontal zoom scroll bar (at bottom)
    horizontalZoomScrollBar =
        std::make_unique<ZoomScrollBar>(ZoomScrollBar::Orientation::Horizontal);
    horizontalZoomScrollBar->onRangeChanged = [this](double start, double end) {
        // Convert range to zoom and scroll
        double rangeWidth = end - start;
        if (rangeWidth > 0 && timelineLength > 0) {
            // Calculate zoom: smaller range = higher zoom
            // horizontalZoom is ppb: convert timelineLength to beats
            const auto& st = timelineController->getState();
            double totalBeats = st.secondsToBeats(timelineLength);
            int viewportWidth = trackContentViewport->getWidth();
            double newZoom = static_cast<double>(viewportWidth) / (rangeWidth * totalBeats);

            // Calculate scroll position (in beats, then to pixels)
            double scrollBeats = start * totalBeats;
            int scrollX = static_cast<int>(scrollBeats * newZoom);

            // Dispatch to TimelineController
            timelineController->dispatch(SetZoomEvent{newZoom});
            timelineController->dispatch(
                SetScrollPositionEvent{scrollX, trackContentViewport->getViewPositionY()});
        }
    };
    addAndMakeVisible(*horizontalZoomScrollBar);

    // Create vertical zoom scroll bar (on left)
    verticalZoomScrollBar = std::make_unique<ZoomScrollBar>(ZoomScrollBar::Orientation::Vertical);
    verticalZoomScrollBar->onRangeChanged = [this](double start, double end) {
        double rangeHeight = end - start;
        if (rangeHeight > 0) {
            // Guard to prevent feedback loop: setViewPosition triggers scrollBarMoved
            // which would call updateVerticalZoomScrollBar and fight the user's drag
            isUpdatingFromVerticalZoomScrollBar = true;

            // Calculate vertical zoom: bigger range = higher zoom (taller tracks)
            // rangeHeight 0->1 maps to zoom 0.5->3.0
            double newVerticalZoom = juce::jlimit(0.5, 3.0, 0.5 + rangeHeight * 2.5);
            verticalZoom = newVerticalZoom;

            // Calculate scroll position based on start position
            int totalContentHeight = trackHeadersPanel->getTotalTracksHeight();
            int scaledHeight = static_cast<int>(totalContentHeight * verticalZoom);
            int scrollY = static_cast<int>(start * scaledHeight);

            // Update track heights and viewport position directly
            trackContentPanel->setVerticalZoom(verticalZoom);
            trackHeadersPanel->setVerticalZoom(verticalZoom);

            int contentWidth = trackContentPanel->getWidth();
            int contentHeight = juce::jmax(scaledHeight, trackContentViewport->getHeight());
            trackContentPanel->setSize(contentWidth, contentHeight);
            trackHeadersPanel->setSize(trackHeaderWidth, contentHeight);

            trackContentViewport->setViewPosition(trackContentViewport->getViewPositionX(),
                                                  scrollY);
            trackHeadersViewport->setViewPosition(0, scrollY);
            playheadComponent->repaint();

            isUpdatingFromVerticalZoomScrollBar = false;
        }
    };
    addAndMakeVisible(*verticalZoomScrollBar);

    // Corner toolbar buttons (above track headers)
    // Zoom icon buttons
    auto setupCornerButton = [this](std::unique_ptr<SvgButton>& btn, const juce::String& name,
                                    const char* svgData, size_t svgSize) {
        btn = std::make_unique<SvgButton>(name, svgData, svgSize);
        btn->setOriginalColor(juce::Colour(0xFFB3B3B3));
        btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setPressedColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
        btn->setBorderThickness(1.0f);
        btn->setWantsKeyboardFocus(false);
        addAndMakeVisible(*btn);
    };

    setupCornerButton(zoomFitButton, "ZoomFit", BinaryData::zoom_out_map_svg,
                      BinaryData::zoom_out_map_svgSize);
    zoomFitButton->onClick = [this]() { resetZoomToFitTimeline(); };

    setupCornerButton(zoomSelButton, "ZoomSel", BinaryData::fit_width_svg,
                      BinaryData::fit_width_svgSize);
    zoomSelButton->onClick = [this]() { zoomToSelection(); };

    setupCornerButton(trackCompactButton, "TrackCompact", BinaryData::compact_svg,
                      BinaryData::compact_svgSize);
    trackCompactButton->onClick = [this]() { setAllTrackHeights(60); };

    setupCornerButton(trackExpandButton, "TrackExpand", BinaryData::large_svg,
                      BinaryData::large_svgSize);
    trackExpandButton->onClick = [this]() { setAllTrackHeights(200); };

    // Set up scroll synchronization
    trackContentViewport->getHorizontalScrollBar().addListener(this);
    trackContentViewport->getVerticalScrollBar().addListener(this);

    // Set up track synchronization between headers and content
    setupTrackSynchronization();

    // Set initial timeline length and zoom
    setTimelineLength(300.0);
}

void MainView::setupCallbacks() {
    // Set up timeline callbacks
    timeline->onPlayheadPositionChanged = [this](double position) {
        timelineController->dispatch(SetPlayheadPositionEvent{position});
    };

    // Handle scroll requests from timeline (for trackpad scrolling over ruler)
    timeline->onScrollRequested = [this](float deltaX, float deltaY) {
        // Calculate scroll amount (scale delta for smooth scrolling)
        const float scrollSpeed = 50.0f;
        int scrollDeltaX = static_cast<int>(-deltaX * scrollSpeed);
        int scrollDeltaY = static_cast<int>(-deltaY * scrollSpeed);

        // Dispatch to controller
        timelineController->dispatch(ScrollByDeltaEvent{scrollDeltaX, scrollDeltaY});
    };

    // Handle time selection from timeline ruler
    timeline->onTimeSelectionChanged = [this](double start, double end) {
        if (start < 0 || end < 0) {
            timelineController->dispatch(ClearTimeSelectionEvent{});
        } else {
            timelineController->dispatch(SetTimeSelectionEvent{start, end});
            // Move playhead to follow the left side of selection
            timelineController->dispatch(SetPlayheadPositionEvent{start});
        }
    };

    // Set up selection and loop callbacks
    setupSelectionCallbacks();
}

MainView::~MainView() {
    // Stop metering timer
    stopTimer();

    // Remove listener before destruction
    if (timelineController) {
        timelineController->removeListener(this);
    }

    // Unregister from TrackManager and ViewModeController
    TrackManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);

    // Save configuration on shutdown
    auto& config = magda::Config::getInstance();
    config.saveToFile("magda_config.txt");
    std::cout << "🎯 CONFIG: Saved configuration on shutdown" << std::endl;
}

// ===== Timer Implementation (for metering) =====

void MainView::timerCallback() {
    // Update master metering from audio engine
    if (!audioEngine_ || !masterHeaderPanel)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    // Update master header panel with real levels
    float masterPeakL = bridge->getMasterPeakL();
    float masterPeakR = bridge->getMasterPeakR();
    masterHeaderPanel->setPeakLevels(masterPeakL, masterPeakR);

    // Update aux section metering
    if (auxHeadersPanel && auxVisible_) {
        auxHeadersPanel->updateMetering(audioEngine_);
    }
}

// ===== TimelineStateListener Implementation =====

void MainView::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    // Zoom/scroll changes
    if (hasFlag(changes, ChangeFlags::Zoom) || hasFlag(changes, ChangeFlags::Scroll)) {
        horizontalZoom = state.zoom.horizontalZoom;
        verticalZoom = state.zoom.verticalZoom;

        timeline->setZoom(horizontalZoom);
        trackContentPanel->setZoom(horizontalZoom);
        trackContentPanel->setVerticalZoom(verticalZoom);

        timelineViewport->setViewPosition(state.zoom.scrollX, 0);
        trackContentViewport->setViewPosition(state.zoom.scrollX, state.zoom.scrollY);

        updateContentSizes();
        updateHorizontalZoomScrollBar();
        updateVerticalZoomScrollBar();
        updateGridDivisionDisplay();

        playheadComponent->repaint();
        selectionOverlay->repaint();
        repaint();
    }

    // Playhead changes
    if (hasFlag(changes, ChangeFlags::Playhead)) {
        playheadPosition = state.playhead.getPosition();
        playheadComponent->setPlayheadPosition(playheadPosition);
        playheadComponent->repaint();

        // Repaint recording overlay when playhead moves during recording
        if (state.playhead.isRecording) {
            selectionOverlay->repaint();
        }

        if (onPlayheadPositionChanged) {
            onPlayheadPositionChanged(playheadPosition);
        }
    }

    // Selection changes
    if (hasFlag(changes, ChangeFlags::Selection)) {
        timeSelection = state.selection;

        if (timeSelection.isVisuallyActive()) {
            timeline->setTimeSelection(timeSelection.startTime, timeSelection.endTime);
        } else {
            timeline->clearTimeSelection();
        }

        if (selectionOverlay) {
            selectionOverlay->repaint();
        }

        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(timeSelection.startTime, timeSelection.endTime,
                                   timeSelection.isActive());
        }

        if (onEditCursorChanged) {
            onEditCursorChanged(state.editCursorPosition);
        }
    }

    // Loop changes
    if (hasFlag(changes, ChangeFlags::Loop)) {
        loopRegion = state.loop;

        isUpdatingLoopRegion = true;

        if (loopRegion.isValid()) {
            timeline->setLoopRegion(loopRegion.startTime, loopRegion.endTime);
            timeline->setLoopEnabled(loopRegion.enabled);
        } else {
            timeline->clearLoopRegion();
        }

        isUpdatingLoopRegion = false;

        if (selectionOverlay) {
            selectionOverlay->repaint();
        }

        if (onLoopRegionChanged) {
            if (loopRegion.isValid()) {
                onLoopRegionChanged(loopRegion.startTime, loopRegion.endTime, loopRegion.enabled);
            } else {
                onLoopRegionChanged(-1.0, -1.0, false);
            }
        }
    }

    // Display config changes
    if (hasFlag(changes, ChangeFlags::Display)) {
        updateGridDivisionDisplay();

        if (onGridQuantizeChanged) {
            const auto& gq = state.display.gridQuantize;
            onGridQuantizeChanged(gq.autoGrid, gq.numerator, gq.denominator, false);
        }
    }

    // Punch changes
    if (hasFlag(changes, ChangeFlags::Punch)) {
        if (onPunchRegionChanged) {
            if (state.punch.isValid()) {
                onPunchRegionChanged(state.punch.startTime, state.punch.endTime,
                                     state.punch.punchInEnabled, state.punch.punchOutEnabled);
            } else {
                onPunchRegionChanged(-1.0, -1.0, false, false);
            }
        }
    }

    // Always sync cached state
    syncStateFromController();
}

// ===== TrackManagerListener Implementation =====

void MainView::masterChannelChanged() {
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool newVisible = master.isVisibleIn(currentViewMode_);

    if (newVisible != masterVisible_) {
        masterVisible_ = newVisible;
        masterHeaderPanel->setVisible(masterVisible_);
        masterContentPanel->setVisible(masterVisible_);
        resized();
    }
}

// ===== ViewModeListener Implementation =====

void MainView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;

    // Update master visibility for new view mode
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool newVisible = master.isVisibleIn(currentViewMode_);

    if (newVisible != masterVisible_) {
        masterVisible_ = newVisible;
        masterHeaderPanel->setVisible(masterVisible_);
        masterContentPanel->setVisible(masterVisible_);
    }

    // Update aux visibility for new view mode
    tracksChanged();

    resized();
}

void MainView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Draw top border for visual separation from transport above
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(0, 0, getWidth(), 1);

    // Draw resize handles
    paintResizeHandle(g);
    paintMasterResizeHandle(g);
}

void MainView::resized() {
    auto bounds = getLocalBounds();

    static constexpr int ZOOM_SCROLLBAR_SIZE = 20;
    auto& layout = LayoutConfig::getInstance();

    // Side columns: headers default to left, zoom scrollbar default to right
    // When swapped: headers on right, zoom scrollbar on left
    bool swapped = Config::getInstance().getScrollbarOnLeft();
    SideColumn headerColumn(!swapped);  // left by default
    SideColumn zoomColumn(swapped);     // right by default (opposite of header)

    // Vertical zoom scroll bar
    auto verticalScrollBarArea = zoomColumn.removeFrom(bounds, ZOOM_SCROLLBAR_SIZE);

    // Horizontal zoom scroll bar at the bottom
    auto horizontalScrollBarArea = bounds.removeFromBottom(ZOOM_SCROLLBAR_SIZE);
    // Leave space in corner for track headers
    headerColumn.removeSpacing(horizontalScrollBarArea, trackHeaderWidth + layout.componentSpacing);
    horizontalZoomScrollBar->setBounds(horizontalScrollBarArea);

    // Fixed master track row at the bottom (above horizontal scroll bar) - only if visible
    int effectiveMasterHeight = masterVisible_ ? masterStripHeight : 0;
    int effectiveResizeHandleHeight = masterVisible_ ? MASTER_RESIZE_HANDLE_HEIGHT : 0;

    if (masterVisible_) {
        auto masterRowArea = bounds.removeFromBottom(masterStripHeight);
        // Master header in the header column
        masterHeaderPanel->setBounds(headerColumn.removeFrom(masterRowArea, trackHeaderWidth));
        headerColumn.removeSpacing(masterRowArea, layout.componentSpacing);
        // Master content takes the rest
        masterContentPanel->setBounds(masterRowArea);
    }

    // Fixed aux track section (directly above master, no extra gap)
    if (auxVisible_) {
        auto auxRowArea = bounds.removeFromBottom(auxSectionHeight);
        auxHeadersPanel->setBounds(headerColumn.removeFrom(auxRowArea, trackHeaderWidth));
        headerColumn.removeSpacing(auxRowArea, layout.componentSpacing);
        auxContentPanel->setBounds(auxRowArea);
    }

    // Resize handle ABOVE the entire fixed bottom section (aux + master)
    if (masterVisible_) {
        bounds.removeFromBottom(MASTER_RESIZE_HANDLE_HEIGHT);
    }

    // Now position vertical scroll bar (after bottom areas removed)
    int effectiveAuxHeight = auxVisible_ ? auxSectionHeight : 0;
    verticalScrollBarArea.removeFromBottom(ZOOM_SCROLLBAR_SIZE + effectiveMasterHeight +
                                           effectiveResizeHandleHeight + effectiveAuxHeight);
    verticalScrollBarArea.removeFromTop(getTimelineHeight());  // Start below timeline
    verticalZoomScrollBar->setBounds(verticalScrollBarArea);

    // Timeline viewport at the top - offset by track header width
    auto timelineArea = bounds.removeFromTop(getTimelineHeight());

    // Corner toolbar area above track headers — 2x2 icon grid (left-aligned, half-size)
    auto cornerArea = headerColumn.removeFrom(timelineArea, trackHeaderWidth);
    {
        int btnSize = 22;
        int gap = 2;
        int gridW = btnSize * 2 + gap;
        int gridH = btnSize * 2 + gap;
        // Left-aligned, vertically centred
        auto grid = juce::Rectangle<int>(cornerArea.getX() + 4, cornerArea.getCentreY() - gridH / 2,
                                         gridW, gridH);

        auto topRow = grid.removeFromTop(btnSize);
        grid.removeFromTop(gap);
        auto botRow = grid.removeFromTop(btnSize);

        zoomFitButton->setBounds(topRow.removeFromLeft(btnSize));
        topRow.removeFromLeft(gap);
        zoomSelButton->setBounds(topRow.removeFromLeft(btnSize));

        trackCompactButton->setBounds(botRow.removeFromLeft(btnSize));
        botRow.removeFromLeft(gap);
        trackExpandButton->setBounds(botRow.removeFromLeft(btnSize));
    }

    // Add padding space for the resize handle
    headerColumn.removeSpacing(timelineArea, layout.componentSpacing);

    // Timeline takes the remaining width
    timelineViewport->setBounds(timelineArea);

    // Track headers viewport in the header column
    auto trackHeadersArea = headerColumn.removeFrom(bounds, trackHeaderWidth);
    trackHeadersViewport->setBounds(trackHeadersArea);

    // Remove padding space between headers and content
    headerColumn.removeSpacing(bounds, layout.componentSpacing);

    // Track content viewport gets the remaining space
    trackContentViewport->setBounds(bounds);

    // Grid and selection overlays cover the track content area
    auto overlayArea = bounds;

    // Grid overlay (bottom layer - draws vertical time grid lines)
    gridOverlay->setBounds(overlayArea);
    gridOverlay->setScrollOffset(trackContentViewport->getViewPositionX());

    // Selection overlay (above grid)
    selectionOverlay->setBounds(overlayArea);

    // Playhead component extends from above timeline down to track content
    // This allows the triangle to be drawn in the timeline area
    auto playheadArea = bounds;
    playheadArea =
        playheadArea.withTop(getTimelineHeight() - 20);  // Start 20px above timeline border
    // No trim needed — internal viewport scrollbars are hidden
    playheadComponent->setBounds(playheadArea);

    // Notify controller about viewport resize
    auto viewportWidth = timelineViewport->getWidth();
    auto viewportHeight = trackContentViewport->getHeight();
    if (viewportWidth > 0) {
        // Dispatch viewport resize event to controller
        timelineController->dispatch(ViewportResizedEvent{viewportWidth, viewportHeight});
        timeline->setViewportWidth(viewportWidth);

        // Set initial zoom to show configurable duration on first resize
        if (!initialZoomSet) {
            int availableWidth = viewportWidth - 18;  // Account for LEFT_PADDING

            if (availableWidth > 0) {
                auto& config = magda::Config::getInstance();
                double zoomViewDuration = config.getDefaultZoomViewDuration();
                // horizontalZoom is ppb: convert duration to beats
                const auto& st = timelineController->getState();
                double viewBeats = st.secondsToBeats(zoomViewDuration);
                double zoomForDefaultView =
                    (viewBeats > 0) ? static_cast<double>(availableWidth) / viewBeats : 10.0;

                // Ensure minimum zoom level for usability
                zoomForDefaultView = juce::jmax(zoomForDefaultView, 0.5);

                // Dispatch initial zoom via controller
                timelineController->dispatch(SetZoomCenteredEvent{zoomForDefaultView, 0.0});

                std::cout << "🎯 INITIAL ZOOM: showing " << zoomViewDuration
                          << " seconds, availableWidth=" << availableWidth
                          << ", zoomForDefaultView=" << zoomForDefaultView << std::endl;

                initialZoomSet = true;
            }
        }
    }

    updateContentSizes();
}

void MainView::setHorizontalZoom(double zoomFactor) {
    // Dispatch to controller
    timelineController->dispatch(SetZoomEvent{zoomFactor});
}

void MainView::setVerticalZoom(double zoomFactor) {
    // Vertical zoom is still managed locally for now
    // TODO: Move to TimelineController when vertical zoom events are added
    verticalZoom = juce::jmax(0.5, juce::jmin(3.0, zoomFactor));
    updateContentSizes();
}

void MainView::scrollToPosition(double timePosition) {
    // horizontalZoom is ppb, convert time to beats
    const auto& state = timelineController->getState();
    double beats = state.secondsToBeats(timePosition);
    auto pixelPosition = static_cast<int>(beats * horizontalZoom);
    timelineViewport->setViewPosition(pixelPosition, 0);
    trackContentViewport->setViewPosition(pixelPosition, trackContentViewport->getViewPositionY());
}

void MainView::scrollToTrack(int trackIndex) {
    if (trackIndex >= 0 && trackIndex < trackHeadersPanel->getNumTracks()) {
        int yPosition = trackHeadersPanel->getTrackYPosition(trackIndex);
        trackContentViewport->setViewPosition(trackContentViewport->getViewPositionX(), yPosition);
        trackHeadersViewport->setViewPosition(0, yPosition);
    }
}

void MainView::selectTrack(int trackIndex) {
    trackHeadersPanel->selectTrack(trackIndex);
    trackContentPanel->selectTrack(trackIndex);
}

void MainView::setTimelineLength(double lengthInSeconds) {
    // Dispatch to controller
    timelineController->dispatch(SetTimelineLengthEvent{lengthInSeconds});

    // Update child components directly (will eventually be handled by listener)
    timeline->setTimelineLength(lengthInSeconds);
    trackContentPanel->setTimelineLength(lengthInSeconds);
}

void MainView::setPlayheadPosition(double position) {
    // Dispatch to controller
    timelineController->dispatch(SetPlayheadPositionEvent{position});
}

void MainView::toggleArrangementLock() {
    // Toggle via controller
    bool newLockedState = !timelineController->getState().display.arrangementLocked;
    timelineController->dispatch(SetArrangementLockedEvent{newLockedState});

    // Also update timeline component directly for now
    timeline->setArrangementLocked(newLockedState);
    timeline->repaint();
}

bool MainView::isArrangementLocked() const {
    return timelineController->getState().display.arrangementLocked;
}

void MainView::setLoopEnabled(bool enabled) {
    // If enabling loop and there's an active time selection, create loop from it
    if (enabled && timelineController->getState().selection.isActive()) {
        timelineController->dispatch(CreateLoopFromSelectionEvent{});
        return;
    }

    // Dispatch to controller
    timelineController->dispatch(SetLoopEnabledEvent{enabled});
}

void MainView::syncSnapState() {
    const auto& state = timelineController->getState();
    timeline->setSnapEnabled(state.display.snapEnabled);
}

// Add keyboard event handler for zoom reset shortcut
bool MainView::keyPressed(const juce::KeyPress& key) {
    // Check for Ctrl+0 (or Cmd+0 on Mac) to reset zoom to fit timeline
    if (key == juce::KeyPress('0', juce::ModifierKeys::commandModifier, 0)) {
        timelineController->dispatch(ResetZoomEvent{});
        return true;
    }

    // Check for F4 to toggle arrangement lock
    if (key == juce::KeyPress::F4Key) {
        toggleArrangementLock();
        return true;
    }

    // Check for 'L' to create loop from selection
    if (key == juce::KeyPress('l') || key == juce::KeyPress('L')) {
        if (timelineController->getState().selection.isActive()) {
            timelineController->dispatch(CreateLoopFromSelectionEvent{});
        }
        return true;
    }

    // Note: 'S' key is now used for split in TrackContentPanel
    // Snap toggle is available via the toolbar button

    // Check for Ctrl+Z / Cmd+Z for undo
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
        if (timelineController->undo()) {
            std::cout << "🎯 UNDO: State restored" << std::endl;
        }
        return true;
    }

    // Check for Ctrl+Shift+Z / Cmd+Shift+Z for redo
    if (key ==
        juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier,
                       0)) {
        if (timelineController->redo()) {
            std::cout << "🎯 REDO: State restored" << std::endl;
        }
        return true;
    }

    // Check for Escape to clear time selection and clip selection
    if (key == juce::KeyPress::escapeKey) {
        timelineController->dispatch(ClearTimeSelectionEvent{});
        SelectionManager::getInstance().clearSelection();
        return true;
    }

    // ===== Clip Shortcuts =====

    auto& clipManager = ClipManager::getInstance();
    auto& selectionManager = SelectionManager::getInstance();

    // Delete/Backspace: Delete selected clips
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        auto selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            // Use compound operation for multiple deletes
            if (selectedClips.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Delete Clips");
            }

            for (auto clipId : selectedClips) {
                auto cmd = std::make_unique<DeleteClipCommand>(clipId);
                UndoManager::getInstance().executeCommand(std::move(cmd));
            }

            if (selectedClips.size() > 1) {
                UndoManager::getInstance().endCompoundOperation();
            }

            selectionManager.clearSelection();
            std::cout << "🎵 CLIP: Deleted " << selectedClips.size() << " clip(s)" << std::endl;
            return true;
        }
    }

    // NOTE: Cmd+C, Cmd+V, Cmd+X, Cmd+D are now handled by ApplicationCommandManager in MainWindow
    // These old handlers have been removed to prevent double-handling

    // NOTE: Split (Cmd+E) and Trim (Cmd+E with time selection) are handled by
    // ApplicationCommandManager in MainWindow

    // Forward unhandled keys to parent for command manager processing
    if (auto* parent = getParentComponent()) {
        return parent->keyPressed(key);
    }

    return false;
}

void MainView::updateContentSizes() {
    // Use the same content width calculation as ZoomManager for consistency
    // horizontalZoom is ppb, convert timeline length to beats
    const auto& st = timelineController->getState();
    double beats = st.secondsToBeats(timelineLength);
    auto baseWidth = static_cast<int>(beats * horizontalZoom);
    auto viewportWidth = timelineViewport->getWidth();
    auto minWidth = viewportWidth + (viewportWidth / 2);  // 1.5x viewport width for centering
    auto contentWidth = juce::jmax(baseWidth, minWidth);

    // Calculate track content height with vertical zoom
    auto baseTrackHeight = trackHeadersPanel->getTotalTracksHeight();
    auto scaledTrackHeight = static_cast<int>(baseTrackHeight * verticalZoom);

    // Update timeline size with enhanced content width
    timeline->setSize(contentWidth, getTimelineHeight());

    // Update track content and headers with same height
    int contentHeight = juce::jmax(scaledTrackHeight, trackContentViewport->getHeight());
    trackContentPanel->setSize(contentWidth, contentHeight);
    trackContentPanel->setVerticalZoom(verticalZoom);
    trackHeadersPanel->setSize(trackHeaderWidth, contentHeight);
    trackHeadersPanel->setVerticalZoom(verticalZoom);

    // Repaint playhead after content size changes
    playheadComponent->repaint();

    // Update both zoom scroll bars
    updateVerticalZoomScrollBar();
}

void MainView::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) {
    // Sync timeline viewport when track content viewport scrolls horizontally
    if (scrollBarThatHasMoved == &trackContentViewport->getHorizontalScrollBar()) {
        int scrollX = static_cast<int>(newRangeStart);
        int scrollY = trackContentViewport->getViewPositionY();

        // Update controller state
        timelineController->dispatch(SetScrollPositionEvent{scrollX, scrollY});

        // Sync timeline viewport
        timelineViewport->setViewPosition(scrollX, 0);

        // Update zoom scroll bar
        updateHorizontalZoomScrollBar();

        // Update grid overlay scroll offset and repaint overlays
        gridOverlay->setScrollOffset(scrollX);
        playheadComponent->repaint();
        selectionOverlay->repaint();
    }

    // Sync track headers viewport and update zoom scroll bar when scrolling vertically
    if (scrollBarThatHasMoved == &trackContentViewport->getVerticalScrollBar()) {
        // Sync track headers viewport to same vertical position
        int scrollY = trackContentViewport->getViewPositionY();
        trackHeadersViewport->setViewPosition(0, scrollY);

        // Update zoom scroll bar (skip if we're handling a ZoomScrollBar change)
        if (!isUpdatingFromVerticalZoomScrollBar) {
            updateVerticalZoomScrollBar();
        }
    }
}

void MainView::syncTrackHeights() {
    // Sync track heights between headers and content panels
    int numTracks = trackHeadersPanel->getNumTracks();
    for (int i = 0; i < numTracks; ++i) {
        int headerHeight = trackHeadersPanel->getTrackHeight(i);
        int contentHeight = trackContentPanel->getTrackHeight(i);

        if (headerHeight != contentHeight) {
            // Sync to the header height (headers are the source of truth)
            trackContentPanel->setTrackHeight(i, headerHeight);
        }
    }
}

void MainView::setupTrackSynchronization() {
    // Set up callbacks to keep track headers and content in sync
    trackHeadersPanel->onTrackHeightChanged = [this](int trackIndex, int newHeight) {
        trackContentPanel->setTrackHeight(trackIndex, newHeight);
        updateContentSizes();
    };

    trackHeadersPanel->onTrackSelected = [this](int trackIndex) {
        if (!isUpdatingTrackSelection) {
            isUpdatingTrackSelection = true;
            trackContentPanel->selectTrack(trackIndex);
            isUpdatingTrackSelection = false;
        }
    };

    // Wire up automation lane visibility toggle
    trackHeadersPanel->onShowAutomationLane = [this](TrackId trackId, AutomationLaneId laneId) {
        trackContentPanel->toggleAutomationLane(trackId, laneId);
        updateContentSizes();
    };

    trackContentPanel->onTrackSelected = [this](int trackIndex) {
        if (!isUpdatingTrackSelection) {
            isUpdatingTrackSelection = true;
            trackHeadersPanel->selectTrack(trackIndex);
            isUpdatingTrackSelection = false;
        }
    };
}

void MainView::updateHorizontalZoomScrollBar() {
    if (timelineLength <= 0 || horizontalZoom <= 0)
        return;

    const auto& st = timelineController->getState();
    int viewportWidth = trackContentViewport->getWidth();
    int scrollX = trackContentViewport->getViewPositionX();

    // Calculate visible range as fraction of total timeline
    // horizontalZoom is ppb, convert through beats
    double totalBeats = st.secondsToBeats(timelineLength);
    double visibleBeats =
        (horizontalZoom > 0) ? static_cast<double>(viewportWidth) / horizontalZoom : 0;
    double scrollBeats = (horizontalZoom > 0) ? static_cast<double>(scrollX) / horizontalZoom : 0;
    double visibleDuration = st.beatsToSeconds(visibleBeats);
    double scrollTime = st.beatsToSeconds(scrollBeats);

    double visibleStart = scrollTime / timelineLength;
    double visibleEnd = (scrollTime + visibleDuration) / timelineLength;

    // Clamp to valid range
    visibleStart = juce::jlimit(0.0, 1.0, visibleStart);
    visibleEnd = juce::jlimit(0.0, 1.0, visibleEnd);

    horizontalZoomScrollBar->setVisibleRange(visibleStart, visibleEnd);
}

void MainView::updateVerticalZoomScrollBar() {
    int totalContentHeight = trackHeadersPanel->getTotalTracksHeight();
    if (totalContentHeight <= 0)
        return;

    int viewportHeight = trackContentViewport->getHeight();
    int scrollY = trackContentViewport->getViewPositionY();

    // Calculate rangeHeight from zoom using inverse of: zoom = 0.5 + rangeHeight * 2.5
    // rangeHeight = (zoom - 0.5) / 2.5
    double rangeHeight = (verticalZoom - 0.5) / 2.5;
    rangeHeight = juce::jlimit(0.01, 1.0, rangeHeight);

    // Calculate scaled content height for scroll position
    int scaledContentHeight = static_cast<int>(totalContentHeight * verticalZoom);
    if (scaledContentHeight <= 0)
        scaledContentHeight = viewportHeight;

    // Calculate scroll position as fraction
    double scrollFraction =
        (scaledContentHeight > viewportHeight)
            ? static_cast<double>(scrollY) / (scaledContentHeight - viewportHeight)
            : 0.0;
    scrollFraction = juce::jlimit(0.0, 1.0, scrollFraction);

    // Position the thumb based on scroll position, keeping rangeHeight constant
    double maxStart = 1.0 - rangeHeight;
    double visibleStart = scrollFraction * maxStart;
    double visibleEnd = visibleStart + rangeHeight;

    // Clamp to valid range
    visibleStart = juce::jlimit(0.0, 1.0, visibleStart);
    visibleEnd = juce::jlimit(0.0, 1.0, visibleEnd);

    verticalZoomScrollBar->setVisibleRange(visibleStart, visibleEnd);
}

void MainView::setupTimelineCallbacks() {
    // Set up timeline zoom callback - dispatches to TimelineController
    timeline->onZoomChanged = [this](double newZoom, double anchorTime, int anchorContentX) {
        // Set crosshair cursor during zoom operations
        setMouseCursor(juce::MouseCursor::CrosshairCursor);

        // On first zoom callback, capture the viewport-relative position
        if (!isZoomActive) {
            isZoomActive = true;
            int currentScrollX = trackContentViewport->getViewPositionX();
            zoomAnchorViewportX = anchorContentX - currentScrollX;
        }

        // Dispatch to controller with anchor information
        timelineController->dispatch(
            SetZoomAnchoredEvent{newZoom, anchorTime, zoomAnchorViewportX});
    };

    // Set up timeline zoom end callback
    timeline->onZoomEnd = [this]() {
        // Reset zoom anchor tracking for next zoom operation
        isZoomActive = false;

        // Reset cursor to normal when zoom ends
        setMouseCursor(juce::MouseCursor::NormalCursor);
    };

    // Set up zoom-to-fit callback (e.g., double-click to fit loop region)
    timeline->onZoomToFitRequested = [this](double startTime, double endTime) {
        if (endTime <= startTime)
            return;

        // Dispatch to controller
        timelineController->dispatch(ZoomToFitEvent{startTime, endTime, 0.05});
    };
}

// PlayheadComponent implementation
MainView::PlayheadComponent::PlayheadComponent(MainView& owner) : owner(owner) {
    setInterceptsMouseClicks(false, true);  // Only intercept clicks when hitTest returns true
}

MainView::PlayheadComponent::~PlayheadComponent() = default;

void MainView::PlayheadComponent::paint(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();
    int scrollOffset = owner.trackContentViewport->getViewPositionX();

    // Get positions from state
    double editPos = state.playhead.editPosition;
    double playbackPos = state.playhead.playbackPosition;
    bool isPlaying = state.playhead.isPlaying;

    // Calculate edit cursor position in pixels (triangle position)
    // horizontalZoom is ppb, convert time to beats
    double editBeats = state.secondsToBeats(editPos);
    int editX =
        static_cast<int>(editBeats * owner.horizontalZoom) + LayoutConfig::TIMELINE_LEFT_PADDING;
    editX -= scrollOffset;

    // Calculate play cursor position in pixels (vertical line position)
    double playBeats = state.secondsToBeats(playbackPos);
    int playX =
        static_cast<int>(playBeats * owner.horizontalZoom) + LayoutConfig::TIMELINE_LEFT_PADDING;
    playX -= scrollOffset;

    // Draw edit cursor (triangle) - always visible
    if (editPos >= 0 && editPos <= owner.timelineLength && editX >= 0 && editX < getWidth()) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        juce::Path triangle;
        triangle.addTriangle(editX - 6, 6, editX + 6, 6, editX, 20);
        g.fillPath(triangle);
    }

    // Draw play cursor (vertical line) - only during playback when position differs from edit
    if (isPlaying && playbackPos >= 0 && playbackPos <= owner.timelineLength && playX >= 0 &&
        playX < getWidth()) {
        // Draw thin vertical line extending full height of track area
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.drawLine(static_cast<float>(playX), 20.0f, static_cast<float>(playX),
                   static_cast<float>(getHeight()), 1.5f);
    }
}

void MainView::PlayheadComponent::setPlayheadPosition(double position) {
    playheadPosition = position;
    repaint();
}

bool MainView::PlayheadComponent::hitTest([[maybe_unused]] int x, [[maybe_unused]] int y) {
    // Don't intercept mouse events - playhead is display-only (just a triangle)
    // Clicks pass through to timeline/tracks for time selection
    return false;
}

void MainView::PlayheadComponent::mouseDown(const juce::MouseEvent& e) {
    // Get edit position from controller state
    const auto& state = owner.timelineController->getState();
    double editPos = state.playhead.editPosition;

    // Calculate edit cursor (triangle) position in pixels
    // horizontalZoom is ppb, convert time to beats
    double editBeats = state.secondsToBeats(editPos);
    int editX =
        static_cast<int>(editBeats * owner.horizontalZoom) + LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for horizontal scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    editX -= scrollOffset;

    // Check if click is near the edit cursor triangle (within 10 pixels)
    if (std::abs(e.x - editX) <= 10) {
        isDragging = true;
        dragStartX = e.x;
        dragStartPosition = editPos;
    }
}

void MainView::PlayheadComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isDragging) {
        // Calculate the change in position
        int deltaX = e.x - dragStartX;

        // Convert pixel change to time change
        // horizontalZoom is ppb: deltaX / ppb = deltaBeats, then convert to seconds
        const auto& state = owner.timelineController->getState();
        double deltaBeats = deltaX / owner.horizontalZoom;
        double deltaTime = state.beatsToSeconds(deltaBeats);

        // Calculate new playhead position
        double newPosition = dragStartPosition + deltaTime;

        // Clamp to valid range
        newPosition = juce::jlimit(0.0, owner.timelineLength, newPosition);

        // Update playhead position
        owner.setPlayheadPosition(newPosition);

        // Notify timeline of position change
        owner.timeline->setPlayheadPosition(newPosition);
    }
}

void MainView::PlayheadComponent::mouseUp([[maybe_unused]] const juce::MouseEvent& event) {
    isDragging = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MainView::PlayheadComponent::mouseMove(const juce::MouseEvent& event) {
    // Get edit position from controller state
    const auto& state = owner.timelineController->getState();
    double editPos = state.playhead.editPosition;

    // Calculate edit cursor (triangle) position in pixels
    // horizontalZoom is ppb, convert time to beats
    double editBeats = state.secondsToBeats(editPos);
    int editX =
        static_cast<int>(editBeats * owner.horizontalZoom) + LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for horizontal scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    editX -= scrollOffset;

    // Change cursor when over edit cursor triangle
    if (std::abs(event.x - editX) <= 10) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void MainView::mouseDown(const juce::MouseEvent& event) {
    // Always grab keyboard focus so shortcuts work
    grabKeyboardFocus();

    if (getResizeHandleArea().contains(event.getPosition())) {
        isResizingHeaders = true;
        lastMouseX = event.x;
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (getMasterResizeHandleArea().contains(event.getPosition())) {
        isResizingMasterStrip = true;
        resizeStartY = event.y;
        resizeStartHeight = masterStripHeight;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    // Removed timeline zoom handling - let timeline component handle its own zoom
    // The timeline component now handles zoom gestures in its lower half
}

void MainView::mouseDrag(const juce::MouseEvent& event) {
    if (isResizingHeaders) {
        int deltaX = event.x - lastMouseX;
        auto& layout = LayoutConfig::getInstance();
        int newWidth = juce::jlimit(layout.minTrackHeaderWidth, layout.maxTrackHeaderWidth,
                                    trackHeaderWidth + deltaX);

        if (newWidth != trackHeaderWidth) {
            trackHeaderWidth = newWidth;
            resized();  // Trigger layout update
        }

        lastMouseX = event.x;  // Update for next drag event
    }

    if (isResizingMasterStrip) {
        // Dragging up (negative deltaY) should increase height
        int deltaY = resizeStartY - event.y;
        int newHeight = juce::jlimit(MIN_MASTER_STRIP_HEIGHT, MAX_MASTER_STRIP_HEIGHT,
                                     resizeStartHeight + deltaY);

        if (newHeight != masterStripHeight) {
            masterStripHeight = newHeight;
            resized();  // Trigger layout update
        }
    }
}

void MainView::mouseUp([[maybe_unused]] const juce::MouseEvent& event) {
    if (isResizingHeaders) {
        isResizingHeaders = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (isResizingMasterStrip) {
        isResizingMasterStrip = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // Removed zoom handling - timeline component handles its own zoom
}

void MainView::mouseMove(const juce::MouseEvent& event) {
    auto handleArea = getResizeHandleArea();
    auto masterHandleArea = getMasterResizeHandleArea();

    if (handleArea.contains(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        repaint(handleArea);  // Repaint to show hover effect
    } else if (masterHandleArea.contains(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        repaint(masterHandleArea);  // Repaint to show hover effect
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint(handleArea);        // Repaint to remove hover effect
        repaint(masterHandleArea);  // Repaint to remove hover effect
    }
}

void MainView::mouseExit([[maybe_unused]] const juce::MouseEvent& event) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(getResizeHandleArea());        // Remove hover effect
    repaint(getMasterResizeHandleArea());  // Remove hover effect
}

// Resize handle helper methods
juce::Rectangle<int> MainView::getResizeHandleArea() const {
    // Position the resize handle in the padding space between headers and content
    auto& layout = LayoutConfig::getInstance();
    return juce::Rectangle<int>(trackHeaderWidth, getTimelineHeight(), layout.componentSpacing,
                                getHeight() - getTimelineHeight());
}

void MainView::paintResizeHandle(juce::Graphics& g) {
    auto handleArea = getResizeHandleArea();

    // Check if mouse is over the handle for hover effect
    auto mousePos = getMouseXYRelative();
    bool isHovered = handleArea.contains(mousePos);

    // Draw subtle resize handle with hover effect
    if (isHovered || isResizingHeaders) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }

    // Draw a thinner visual line in the center
    int centerX = handleArea.getCentreX();
    g.fillRect(centerX - 1, handleArea.getY(), 2, handleArea.getHeight());

    // Draw resize indicator dots when hovered or resizing
    if (isHovered || isResizingHeaders) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).brighter(0.2f));
        int centerY = handleArea.getCentreY();

        for (int i = -1; i <= 1; ++i) {
            g.fillEllipse(centerX - 1, centerY + i * 4 - 1, 2, 2);
        }
    }
}

juce::Rectangle<int> MainView::getMasterResizeHandleArea() const {
    // Return empty area if master is not visible
    if (!masterVisible_) {
        return {};
    }

    // Position the resize handle in the gap between track content and master strip
    static constexpr int ZOOM_SCROLLBAR_SIZE = 20;
    int effectiveAuxHeight = auxVisible_ ? auxSectionHeight : 0;
    // Master row top is at: getHeight() - ZOOM_SCROLLBAR_SIZE - masterStripHeight
    // Resize handle is ABOVE that, and aux section is above that
    int resizeHandleY = getHeight() - ZOOM_SCROLLBAR_SIZE - masterStripHeight -
                        MASTER_RESIZE_HANDLE_HEIGHT - effectiveAuxHeight;
    return juce::Rectangle<int>(0, resizeHandleY, getWidth(), MASTER_RESIZE_HANDLE_HEIGHT);
}

void MainView::paintMasterResizeHandle(juce::Graphics& g) {
    auto handleArea = getMasterResizeHandleArea();

    // Check if mouse is over the handle for hover effect
    auto mousePos = getMouseXYRelative();
    bool isHovered = handleArea.contains(mousePos);

    // Draw subtle resize handle with hover effect
    if (isHovered || isResizingMasterStrip) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }

    // Draw a horizontal line
    int centerY = handleArea.getCentreY();
    g.fillRect(handleArea.getX(), centerY - 1, handleArea.getWidth(), 2);

    // Draw resize indicator dots when hovered or resizing
    if (isHovered || isResizingMasterStrip) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).brighter(0.2f));
        int centerX = handleArea.getCentreX();

        for (int i = -1; i <= 1; ++i) {
            g.fillEllipse(static_cast<float>(centerX + i * 4 - 1), static_cast<float>(centerY - 1),
                          2.0f, 2.0f);
        }
    }
}

void MainView::resetZoomToFitTimeline() {
    // Dispatch to controller
    timelineController->dispatch(ResetZoomEvent{});

    std::cout << "🎯 ZOOM RESET: timelineLength=" << timelineController->getState().timelineLength
              << ", zoom=" << timelineController->getState().zoom.horizontalZoom << std::endl;
}

void MainView::zoomToSelection() {
    const auto& sel = timelineController->getState().selection;
    if (sel.isActive()) {
        timelineController->dispatch(ZoomToFitEvent{sel.startTime, sel.endTime, 0.05});
    }
}

void MainView::setAllTrackHeights(int height) {
    int numTracks = trackHeadersPanel->getNumTracks();
    for (int i = 0; i < numTracks; ++i) {
        trackHeadersPanel->setTrackHeight(i, height);
        trackContentPanel->setTrackHeight(i, height);
    }
    updateContentSizes();
    updateVerticalZoomScrollBar();
}

void MainView::clearTimeSelection() {
    // Dispatch to controller
    timelineController->dispatch(ClearTimeSelectionEvent{});
}

void MainView::createLoopFromSelection() {
    // Dispatch to controller - it handles clearing selection after creating loop
    timelineController->dispatch(CreateLoopFromSelectionEvent{});

    const auto& state = timelineController->getState();
    if (state.loop.isValid()) {
        std::cout << "🔁 LOOP CREATED: " << state.loop.startTime << "s - " << state.loop.endTime
                  << "s" << std::endl;
    }
}

void MainView::setupSelectionCallbacks() {
    // Set up snap to grid callback for track content panel
    // This uses the controller's state for snapping
    trackContentPanel->snapTimeToGrid = [this](double time) {
        return timelineController->getState().snapTimeToGrid(time);
    };

    // Set up render callbacks (bubble up to MainWindow)
    trackContentPanel->onClipRenderRequested = [this](ClipId id) {
        if (onClipRenderRequested)
            onClipRenderRequested(id);
    };
    trackContentPanel->onRenderTimeSelectionRequested = [this]() {
        if (onRenderTimeSelectionRequested)
            onRenderTimeSelectionRequested();
    };

    // Set up time selection callback from track content panel
    trackContentPanel->onTimeSelectionChanged = [this](double start, double end,
                                                       std::set<int> trackIndices) {
        if (start < 0 || end < 0) {
            timelineController->dispatch(ClearTimeSelectionEvent{});
        } else {
            timelineController->dispatch(SetTimeSelectionEvent{start, end, trackIndices});
            // Move playhead to follow the left side of selection
            timelineController->dispatch(SetPlayheadPositionEvent{start});
        }
    };

    // Set up playhead position callback from track content panel (click to set playhead)
    trackContentPanel->onPlayheadPositionChanged = [this](double position) {
        timelineController->dispatch(SetPlayheadPositionEvent{position});
    };

    // Set up loop region callback from timeline
    timeline->onLoopRegionChanged = [this](double start, double end) {
        // Prevent recursive updates - only dispatch if user changed it, not programmatic update
        if (isUpdatingLoopRegion) {
            return;
        }

        if (start < 0 || end < 0) {
            timelineController->dispatch(ClearLoopRegionEvent{});
        } else {
            timelineController->dispatch(SetLoopRegionEvent{start, end});
        }
    };
}

// SelectionOverlayComponent implementation
MainView::SelectionOverlayComponent::SelectionOverlayComponent(MainView& owner) : owner(owner) {
    setInterceptsMouseClicks(false, false);  // Transparent to all mouse events
}

MainView::SelectionOverlayComponent::~SelectionOverlayComponent() = default;

void MainView::SelectionOverlayComponent::paint(juce::Graphics& g) {
    // NOTE: Recording region drawing removed — now handled by
    // TrackContentPanel::paintRecordingPreviews() with real-time MIDI notes.
    drawTimeSelection(g);
    drawLoopRegion(g);
}

void MainView::SelectionOverlayComponent::drawTimeSelection(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();
    if (!state.selection.isVisuallyActive()) {
        return;
    }

    // Calculate pixel positions
    // horizontalZoom is ppb, convert times to beats
    double startBeats = state.secondsToBeats(state.selection.startTime);
    double endBeats = state.secondsToBeats(state.selection.endTime);
    // Add LEFT_PADDING to align with timeline markers
    int startX = static_cast<int>(startBeats * state.zoom.horizontalZoom) +
                 LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = static_cast<int>(endBeats * state.zoom.horizontalZoom) +
               LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    startX -= scrollOffset;
    endX -= scrollOffset;

    // Skip if out of view horizontally
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    // Clamp to visible area horizontally
    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);

    int selectionWidth = endX - startX;

    // Check if this is an all-tracks selection
    if (state.selection.isAllTracks()) {
        // Draw full-height selection (backward compatible behavior)
        g.setColour(DarkTheme::getColour(DarkTheme::TIME_SELECTION));
        g.fillRect(startX, 0, selectionWidth, getHeight());

        // Draw selection edges
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.8f));
        g.drawLine(static_cast<float>(startX), 0.0f, static_cast<float>(startX),
                   static_cast<float>(getHeight()), 2.0f);
        g.drawLine(static_cast<float>(endX), 0.0f, static_cast<float>(endX),
                   static_cast<float>(getHeight()), 2.0f);
    } else {
        // Per-track selection: draw only on selected tracks
        int scrollY = owner.trackContentViewport->getViewPositionY();
        int numTracks = owner.trackContentPanel->getNumTracks();

        for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
            if (state.selection.includesTrack(trackIndex)) {
                // Get track position and height
                int trackY = owner.trackContentPanel->getTrackYPosition(trackIndex) - scrollY;
                int trackHeight = owner.trackContentPanel->getTrackHeight(trackIndex);

                // Apply vertical zoom
                trackHeight = static_cast<int>(trackHeight * owner.verticalZoom);

                // Skip if track is not visible vertically
                if (trackY + trackHeight < 0 || trackY > getHeight()) {
                    continue;
                }

                // Clamp to visible area vertically
                int drawY = juce::jmax(0, trackY);
                int drawBottom = juce::jmin(getHeight(), trackY + trackHeight);
                int drawHeight = drawBottom - drawY;

                if (drawHeight > 0) {
                    // Draw semi-transparent selection highlight for this track
                    g.setColour(DarkTheme::getColour(DarkTheme::TIME_SELECTION));
                    g.fillRect(startX, drawY, selectionWidth, drawHeight);

                    // Draw selection edges within track bounds
                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.8f));
                    g.drawLine(static_cast<float>(startX), static_cast<float>(drawY),
                               static_cast<float>(startX), static_cast<float>(drawBottom), 2.0f);
                    g.drawLine(static_cast<float>(endX), static_cast<float>(drawY),
                               static_cast<float>(endX), static_cast<float>(drawBottom), 2.0f);
                }
            }
        }
    }
}

void MainView::SelectionOverlayComponent::drawLoopRegion(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();

    // Always draw if there's a valid loop region, but use grey when disabled
    if (!state.loop.isValid()) {
        return;
    }

    // Calculate pixel positions
    // horizontalZoom is ppb, convert times to beats
    double loopStartBeats = state.secondsToBeats(state.loop.startTime);
    double loopEndBeats = state.secondsToBeats(state.loop.endTime);
    int startX = static_cast<int>(loopStartBeats * state.zoom.horizontalZoom) +
                 LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = static_cast<int>(loopEndBeats * state.zoom.horizontalZoom) +
               LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    startX -= scrollOffset;
    endX -= scrollOffset;

    // Skip if out of view
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    // Track original positions before clamping (for marker visibility)
    int originalStartX = startX;
    int originalEndX = endX;

    // Clamp to visible area for the filled region
    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);

    // Use different colors based on enabled state
    bool enabled = state.loop.enabled;
    juce::Colour regionColour = enabled ? DarkTheme::getColour(DarkTheme::LOOP_REGION)
                                        : juce::Colour(0x15808080);  // Light grey, very transparent
    juce::Colour markerColour = enabled
                                    ? DarkTheme::getColour(DarkTheme::LOOP_MARKER).withAlpha(0.8f)
                                    : juce::Colour(0xFF606060);  // Medium grey

    // Draw semi-transparent loop region
    g.setColour(regionColour);
    g.fillRect(startX, 0, endX - startX, getHeight());

    // Draw loop region edges only if they're actually visible (not clamped)
    g.setColour(markerColour);
    if (originalStartX >= 0 && originalStartX <= getWidth()) {
        g.drawLine(static_cast<float>(originalStartX), 0.0f, static_cast<float>(originalStartX),
                   static_cast<float>(getHeight()), 2.0f);
    }
    if (originalEndX >= 0 && originalEndX <= getWidth()) {
        g.drawLine(static_cast<float>(originalEndX), 0.0f, static_cast<float>(originalEndX),
                   static_cast<float>(getHeight()), 2.0f);
    }
}

void MainView::SelectionOverlayComponent::drawRecordingRegion(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();

    if (!state.playhead.isRecording) {
        return;
    }

    // Recording region: from editPosition (start) to playbackPosition (current)
    double recordStartTime = state.playhead.editPosition;
    double recordEndTime = state.playhead.playbackPosition;

    if (recordEndTime <= recordStartTime) {
        return;
    }

    // Convert to pixels
    double startBeats = state.secondsToBeats(recordStartTime);
    double endBeats = state.secondsToBeats(recordEndTime);
    int startX = static_cast<int>(startBeats * state.zoom.horizontalZoom) +
                 LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = static_cast<int>(endBeats * state.zoom.horizontalZoom) +
               LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    startX -= scrollOffset;
    endX -= scrollOffset;

    // Skip if out of view
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);

    int scrollY = owner.trackContentViewport->getViewPositionY();
    auto& tracks = TrackManager::getInstance().getTracks();

    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        if (!tracks[trackIndex].recordArmed) {
            continue;
        }

        int trackY = owner.trackContentPanel->getTrackYPosition(trackIndex) - scrollY;
        int trackHeight = static_cast<int>(owner.trackContentPanel->getTrackHeight(trackIndex) *
                                           owner.verticalZoom);

        // Skip if not visible
        if (trackY + trackHeight < 0 || trackY > getHeight()) {
            continue;
        }

        int drawY = juce::jmax(0, trackY);
        int drawBottom = juce::jmin(getHeight(), trackY + trackHeight);
        int drawHeight = drawBottom - drawY;

        if (drawHeight > 0) {
            // Use the same style as a MIDI clip: darker fill of the default clip color
            auto clipColour = ClipInfo::getDefaultColor(
                static_cast<int>(ClipManager::getInstance().getArrangementClips().size()));
            g.setColour(clipColour.darker(0.3f));
            g.fillRoundedRectangle(startX, drawY, endX - startX, drawHeight, 3.0f);

            // Red recording border
            g.setColour(juce::Colours::red);
            g.drawRoundedRectangle(static_cast<float>(startX), static_cast<float>(drawY),
                                   static_cast<float>(endX - startX),
                                   static_cast<float>(drawHeight), 3.0f, 1.5f);
        }
    }
}

// ===== Horizontal Stereo Meter for MasterHeaderPanel =====

class MainView::MasterHeaderPanel::HorizontalStereoMeter : public juce::Component {
  public:
    void setLevels(float left, float right) {
        leftLevel_ = juce::jlimit(0.0f, 2.0f, left);
        rightLevel_ = juce::jlimit(0.0f, 2.0f, right);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        const float gap = 1.0f;
        float barHeight = (bounds.getHeight() - gap) / 2.0f;

        // Left channel (top bar)
        auto leftBounds = bounds.removeFromTop(barHeight);
        drawMeterBar(g, leftBounds, leftLevel_);

        // Gap
        bounds.removeFromTop(gap);

        // Right channel (bottom bar)
        auto rightBounds = bounds.removeFromTop(barHeight);
        drawMeterBar(g, rightBounds, rightLevel_);
    }

  private:
    float leftLevel_ = 0.0f;
    float rightLevel_ = 0.0f;

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level) {
        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        // Meter fill (using same scaling as track meters for visual consistency)
        float displayLevel = dbToMeterPos(gainToDb(level));
        float meterWidth = bounds.getWidth() * displayLevel;
        auto fillBounds = bounds.withWidth(meterWidth);

        // Color based on level
        g.setColour(getMeterColour(level));
        g.fillRoundedRectangle(fillBounds, 1.0f);
    }

    static juce::Colour getMeterColour(float level) {
        float dbLevel = gainToDb(level);
        juce::Colour green(0xFF55AA55);
        juce::Colour yellow(0xFFAAAA55);
        juce::Colour red(0xFFAA5555);

        if (dbLevel < -12.0f) {
            return green;
        } else if (dbLevel < 0.0f) {
            float t = (dbLevel + 12.0f) / 12.0f;
            return green.interpolatedWith(yellow, t);
        } else if (dbLevel < 3.0f) {
            float t = dbLevel / 3.0f;
            return yellow.interpolatedWith(red, t);
        } else {
            return red;
        }
    }
};

// ===== MasterHeaderPanel Implementation =====

MainView::MasterHeaderPanel::MasterHeaderPanel() {
    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    setupControls();

    // Sync initial state from master channel
    masterChannelChanged();
}

MainView::MasterHeaderPanel::~MasterHeaderPanel() {
    TrackManager::getInstance().removeListener(this);
}

void MainView::MasterHeaderPanel::setupControls() {
    // Name label
    nameLabel = std::make_unique<juce::Label>("masterName", "Master");
    nameLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    nameLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    nameLabel->setFont(FontManager::getInstance().getUIFont(12.0f));
    addAndMakeVisible(*nameLabel);

    // Speaker on/off button (toggles master mute)
    auto speakerOnIcon = juce::Drawable::createFromImageData(BinaryData::volume_up_svg,
                                                             BinaryData::volume_up_svgSize);
    auto speakerOffIcon = juce::Drawable::createFromImageData(BinaryData::volume_off_svg,
                                                              BinaryData::volume_off_svgSize);

    speakerButton =
        std::make_unique<juce::DrawableButton>("Speaker", juce::DrawableButton::ImageFitted);
    speakerButton->setImages(speakerOnIcon.get(), nullptr, nullptr, nullptr, speakerOffIcon.get());
    speakerButton->setClickingTogglesState(true);
    speakerButton->setColour(juce::DrawableButton::backgroundColourId,
                             juce::Colours::transparentBlack);
    speakerButton->setColour(juce::DrawableButton::backgroundOnColourId,
                             DarkTheme::getColour(DarkTheme::STATUS_ERROR).withAlpha(0.3f));
    speakerButton->onClick = [this]() {
        TrackManager::getInstance().setMasterMuted(speakerButton->getToggleState());
    };
    addAndMakeVisible(*speakerButton);

    // Volume as draggable dB label
    volumeLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    volumeLabel->setRange(-60.0, 6.0, 0.0);  // -60 dB to +6 dB, default 0 dB
    volumeLabel->setDoubleClickResetsValue(true);
    volumeLabel->onValueChange = [this]() {
        // Convert dB to linear gain
        float db = static_cast<float>(volumeLabel->getValue());
        float gain = dbToGain(db);
        TrackManager::getInstance().setMasterVolume(gain);
    };
    addAndMakeVisible(*volumeLabel);

    // Pan as draggable L/C/R label
    panLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
    panLabel->setRange(-1.0, 1.0, 0.0);  // Full left to full right, default center
    panLabel->setDoubleClickResetsValue(true);
    panLabel->onValueChange = [this]() {
        TrackManager::getInstance().setMasterPan(static_cast<float>(panLabel->getValue()));
    };
    addAndMakeVisible(*panLabel);

    // Peak meter with label
    peakLabel = std::make_unique<juce::Label>("peak", "Peak");
    peakLabel->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    peakLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    peakLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*peakLabel);

    peakMeter = std::make_unique<HorizontalStereoMeter>();
    addAndMakeVisible(*peakMeter);

    peakValueLabel = std::make_unique<juce::Label>("peakValue", "-inf");
    peakValueLabel->setColour(juce::Label::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    peakValueLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    peakValueLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*peakValueLabel);

    // VU meter with label
    vuLabel = std::make_unique<juce::Label>("vu", "VU");
    vuLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    vuLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    vuLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*vuLabel);

    vuMeter = std::make_unique<HorizontalStereoMeter>();
    addAndMakeVisible(*vuMeter);

    vuValueLabel = std::make_unique<juce::Label>("vuValue", "-inf");
    vuValueLabel->setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    vuValueLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    vuValueLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*vuValueLabel);
}

void MainView::MasterHeaderPanel::paint(juce::Graphics& g) {
    // Background - slightly different from regular tracks to distinguish
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
}

void MainView::MasterHeaderPanel::mouseDown(const juce::MouseEvent& /*event*/) {
    SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
}

void MainView::MasterHeaderPanel::resized() {
    auto contentArea = getLocalBounds().reduced(4);

    // Top row: Name label, speaker button, volume label, pan label
    auto topRow = contentArea.removeFromTop(18);
    nameLabel->setBounds(topRow.removeFromLeft(44));
    topRow.removeFromLeft(2);
    speakerButton->setBounds(topRow.removeFromLeft(18).withSizeKeepingCentre(16, 16));
    topRow.removeFromLeft(4);
    volumeLabel->setBounds(topRow.removeFromLeft(44));
    topRow.removeFromLeft(4);
    panLabel->setBounds(topRow.removeFromLeft(36));

    contentArea.removeFromTop(2);  // Spacing

    // Bottom area: meters
    // Calculate meter layout based on remaining height
    int remainingHeight = contentArea.getHeight();
    int meterRowHeight = (remainingHeight - 2) / 2;  // 2px gap between meters
    int labelWidth = 28;
    int valueWidth = 36;

    // Peak meter row
    auto peakRow = contentArea.removeFromTop(meterRowHeight);
    peakLabel->setBounds(peakRow.removeFromLeft(labelWidth));
    peakRow.removeFromLeft(2);
    peakValueLabel->setBounds(peakRow.removeFromRight(valueWidth));
    peakRow.removeFromRight(2);
    peakMeter->setBounds(peakRow);

    contentArea.removeFromTop(2);  // Spacing between meters

    // VU meter row
    auto vuRow = contentArea.removeFromTop(meterRowHeight);
    vuLabel->setBounds(vuRow.removeFromLeft(labelWidth));
    vuRow.removeFromLeft(2);
    vuValueLabel->setBounds(vuRow.removeFromRight(valueWidth));
    vuRow.removeFromRight(2);
    vuMeter->setBounds(vuRow);
}

void MainView::MasterHeaderPanel::masterChannelChanged() {
    const auto& master = TrackManager::getInstance().getMasterChannel();

    speakerButton->setToggleState(master.muted, juce::dontSendNotification);

    // Convert linear gain to dB for volume label
    float db = gainToDb(master.volume);
    volumeLabel->setValue(db, juce::dontSendNotification);

    // Pan value
    panLabel->setValue(master.pan, juce::dontSendNotification);

    repaint();
}

void MainView::MasterHeaderPanel::setPeakLevels(float leftPeak, float rightPeak) {
    if (peakMeter) {
        peakMeter->setLevels(leftPeak, rightPeak);
    }

    // Update peak value label (show current max of both channels)
    float maxPeak = std::max(leftPeak, rightPeak);
    if (peakValueLabel) {
        float db = gainToDb(maxPeak);
        juce::String text = (db <= MIN_DB) ? "-inf" : juce::String(db, 1);
        peakValueLabel->setText(text, juce::dontSendNotification);
    }
}

void MainView::MasterHeaderPanel::setVuLevels(float leftVu, float rightVu) {
    if (vuMeter) {
        vuMeter->setLevels(leftVu, rightVu);
    }

    // Update VU value label (show current max of both channels)
    float maxVu = std::max(leftVu, rightVu);
    if (vuValueLabel) {
        float db = gainToDb(maxVu);
        juce::String text = (db <= MIN_DB) ? "-inf" : juce::String(db, 1);
        vuValueLabel->setText(text, juce::dontSendNotification);
    }
}

// ===== MasterContentPanel Implementation =====

MainView::MasterContentPanel::MasterContentPanel() {
    // Empty for now - will show waveform later
}

void MainView::MasterContentPanel::paint(juce::Graphics& g) {
    // Background matching track content area
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Draw a subtle indicator that this is the master output area
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.3f));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText("Master Output", getLocalBounds(), juce::Justification::centred);
}

// ===== TrackManagerListener — aux track management =====

void MainView::tracksChanged() {
    // Count aux tracks and update aux section visibility
    const auto& tracks = TrackManager::getInstance().getTracks();
    int auxCount = 0;
    for (const auto& track : tracks) {
        if (track.type == TrackType::Aux && track.isVisibleIn(currentViewMode_))
            ++auxCount;
    }

    auxSectionHeight = auxCount * AUX_ROW_HEIGHT;
    bool newAuxVisible = auxCount > 0;

    auxVisible_ = newAuxVisible;
    auxHeadersPanel->setVisible(auxVisible_);
    auxContentPanel->setVisible(auxVisible_);
    auxContentPanel->setAuxTrackCount(auxCount);
    resized();
}

// ===== Grid Division Display =====

juce::String MainView::calculateGridDivisionString() const {
    const auto& state = timelineController->getState();

    // If grid override is active, return the numerator/denominator string
    if (!state.display.gridQuantize.autoGrid) {
        int num = state.display.gridQuantize.numerator;
        int den = state.display.gridQuantize.denominator;
        return juce::String(num) + "/" + juce::String(den);
    }

    // Auto mode: compute smart grid and format as text
    int num = 0, den = 0;
    bool isBars = false;
    calculateSmartGridNumeratorDenominator(num, den, isBars);

    if (isBars) {
        return num == 1 ? "1 bar" : juce::String(num) + " bars";
    }
    return juce::String(num) + "/" + juce::String(den);
}

void MainView::calculateSmartGridNumeratorDenominator(int& outNum, int& outDen,
                                                      bool& outIsBars) const {
    const auto& state = timelineController->getState();
    double zoom = state.zoom.horizontalZoom;
    int timeSigNumerator = state.tempo.timeSignatureNumerator;
    auto& layout = LayoutConfig::getInstance();
    int minPixelSpacing = layout.minGridPixelSpacing;

    outIsBars = false;

    // Try beat subdivisions (powers of 2)
    double frac = GridConstants::findBeatSubdivision(zoom, minPixelSpacing);
    if (frac > 0) {
        // Convert beat fraction to whole-note-relative num/den
        // beatFraction = 2^p, denominator = 4 / beatFraction
        outNum = 1;
        outDen = static_cast<int>(4.0 / frac);
        if (outDen < 1)
            outDen = 1;  // For frac > 4 (shouldn't happen)
        return;
    }

    // Bar multiples
    int mult = GridConstants::findBarMultiple(zoom, timeSigNumerator, minPixelSpacing);
    outNum = mult;
    outDen = 0;
    outIsBars = true;
}

void MainView::updateGridDivisionDisplay() {
    if (horizontalZoomScrollBar) {
        horizontalZoomScrollBar->setLabel(calculateGridDivisionString());
    }

    // When Auto mode is on, push the smart grid values to the transport panel
    // and update the state so auto→manual switch can seed from them
    const auto& state = timelineController->getState();
    if (state.display.gridQuantize.autoGrid) {
        int num = 0, den = 0;
        bool isBars = false;
        calculateSmartGridNumeratorDenominator(num, den, isBars);
        timelineController->dispatch(SetAutoGridDisplayEvent{num, den});
        if (onGridQuantizeChanged)
            onGridQuantizeChanged(true, num, den, isBars);
    }
}

// ===== AuxHeadersPanel Implementation =====

MainView::AuxHeadersPanel::AuxHeadersPanel() {
    TrackManager::getInstance().addListener(this);
    rebuildAuxRows();
}

MainView::AuxHeadersPanel::~AuxHeadersPanel() {
    TrackManager::getInstance().removeListener(this);
}

void MainView::AuxHeadersPanel::tracksChanged() {
    rebuildAuxRows();
}

void MainView::AuxHeadersPanel::rebuildAuxRows() {
    // Remove all existing child components
    for (auto& row : auxRows_) {
        removeChildComponent(row->nameLabel.get());
        removeChildComponent(row->volumeLabel.get());
        removeChildComponent(row->panLabel.get());
        removeChildComponent(row->muteButton.get());
        removeChildComponent(row->soloButton.get());
    }
    auxRows_.clear();

    const auto& tracks = TrackManager::getInstance().getTracks();
    auto currentViewMode = ViewModeController::getInstance().getViewMode();

    for (const auto& track : tracks) {
        if (track.type != TrackType::Aux || !track.isVisibleIn(currentViewMode))
            continue;

        auto row = std::make_unique<AuxRow>();
        row->trackId = track.id;

        // Name label - show "Aux N" based on bus index
        juce::String auxName = "Aux " + juce::String(track.auxBusIndex + 1);
        row->nameLabel = std::make_unique<juce::Label>("auxName", auxName);
        row->nameLabel->setColour(juce::Label::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        row->nameLabel->setFont(FontManager::getInstance().getUIFont(11.0f));
        addAndMakeVisible(*row->nameLabel);

        // Volume label
        row->volumeLabel =
            std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
        row->volumeLabel->setRange(-60.0, 6.0, 0.0);
        float db = gainToDb(track.volume);
        row->volumeLabel->setValue(db, juce::dontSendNotification);
        TrackId tid = track.id;
        auto* volLabelPtr = row->volumeLabel.get();
        row->volumeLabel->onValueChange = [tid, volLabelPtr]() {
            float newDb = static_cast<float>(volLabelPtr->getValue());
            float gain = dbToGain(newDb);
            TrackManager::getInstance().setTrackVolume(tid, gain);
        };
        addAndMakeVisible(*row->volumeLabel);

        // Pan label
        row->panLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
        row->panLabel->setRange(-1.0, 1.0, 0.0);
        row->panLabel->setValue(track.pan, juce::dontSendNotification);
        auto* panLabelPtr = row->panLabel.get();
        row->panLabel->onValueChange = [tid, panLabelPtr]() {
            TrackManager::getInstance().setTrackPan(tid,
                                                    static_cast<float>(panLabelPtr->getValue()));
        };
        addAndMakeVisible(*row->panLabel);

        // Mute button
        row->muteButton = std::make_unique<juce::TextButton>("M");
        row->muteButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        row->muteButton->setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
        row->muteButton->setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getColour(DarkTheme::STATUS_WARNING));
        row->muteButton->setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        row->muteButton->setColour(juce::TextButton::textColourOnId,
                                   DarkTheme::getColour(DarkTheme::BACKGROUND));
        row->muteButton->setClickingTogglesState(true);
        row->muteButton->setToggleState(track.muted, juce::dontSendNotification);
        auto* muteBtnPtr = row->muteButton.get();
        row->muteButton->onClick = [tid, muteBtnPtr]() {
            TrackManager::getInstance().setTrackMuted(tid, muteBtnPtr->getToggleState());
        };
        addAndMakeVisible(*row->muteButton);

        // Solo button
        row->soloButton = std::make_unique<juce::TextButton>("S");
        row->soloButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        row->soloButton->setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
        row->soloButton->setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        row->soloButton->setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        row->soloButton->setColour(juce::TextButton::textColourOnId,
                                   DarkTheme::getColour(DarkTheme::BACKGROUND));
        row->soloButton->setClickingTogglesState(true);
        row->soloButton->setToggleState(track.soloed, juce::dontSendNotification);
        auto* soloBtnPtr = row->soloButton.get();
        row->soloButton->onClick = [tid, soloBtnPtr]() {
            TrackManager::getInstance().setTrackSoloed(tid, soloBtnPtr->getToggleState());
        };
        addAndMakeVisible(*row->soloButton);

        auxRows_.push_back(std::move(row));
    }

    resized();
    repaint();
}

void MainView::AuxHeadersPanel::paint(juce::Graphics& g) {
    // Slightly different background to distinguish from regular tracks
    g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE).darker(0.1f));

    // Draw borders between rows
    int rowHeight = getHeight() / juce::jmax(1, static_cast<int>(auxRows_.size()));
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    for (size_t i = 1; i < auxRows_.size(); ++i) {
        int y = static_cast<int>(i) * rowHeight;
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
}

void MainView::AuxHeadersPanel::mouseDown(const juce::MouseEvent& event) {
    if (auxRows_.empty())
        return;

    int rowHeight = getHeight() / static_cast<int>(auxRows_.size());
    int rowIndex = event.getPosition().getY() / rowHeight;

    if (rowIndex >= 0 && rowIndex < static_cast<int>(auxRows_.size())) {
        SelectionManager::getInstance().selectTrack(auxRows_[rowIndex]->trackId);
    }
}

void MainView::AuxHeadersPanel::resized() {
    if (auxRows_.empty())
        return;

    int rowHeight = getHeight() / static_cast<int>(auxRows_.size());
    auto bounds = getLocalBounds();

    for (size_t i = 0; i < auxRows_.size(); ++i) {
        auto& row = *auxRows_[i];
        auto rowArea = bounds.removeFromTop(rowHeight);
        // Centre a fixed 18px-tall strip within the row (matching master header controls)
        auto controlArea = rowArea.withSizeKeepingCentre(rowArea.getWidth() - 8, 18);

        // Layout: [Name 36px] [M 18px] [S 18px] [Vol 40px] [Pan 32px]
        row.nameLabel->setBounds(controlArea.removeFromLeft(36));
        controlArea.removeFromLeft(4);
        row.muteButton->setBounds(controlArea.removeFromLeft(18).withSizeKeepingCentre(16, 16));
        controlArea.removeFromLeft(2);
        row.soloButton->setBounds(controlArea.removeFromLeft(18).withSizeKeepingCentre(16, 16));
        controlArea.removeFromLeft(4);
        row.volumeLabel->setBounds(controlArea.removeFromLeft(40));
        controlArea.removeFromLeft(4);
        row.panLabel->setBounds(controlArea.removeFromLeft(32));
    }
}

void MainView::AuxHeadersPanel::updateMetering(AudioEngine* engine) {
    // Aux metering could be added here in the future
    // For now, aux tracks share the same metering infrastructure as regular tracks
    (void)engine;
}

// ===== AuxContentPanel Implementation =====

void MainView::AuxContentPanel::paint(juce::Graphics& g) {
    // Background matching track content but slightly different to distinguish aux
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND).darker(0.05f));

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    if (auxTrackCount_ > 0) {
        // Draw row separators
        int rowHeight = getHeight() / auxTrackCount_;
        for (int i = 1; i < auxTrackCount_; ++i) {
            int y = i * rowHeight;
            g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
        }
    }
}

}  // namespace magda
