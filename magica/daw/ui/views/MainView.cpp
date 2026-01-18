#include "MainView.hpp"

#include <BinaryData.h>

#include <iostream>
#include <set>

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "Config.hpp"
#include "core/TrackManager.hpp"

namespace magica {

MainView::MainView() : playheadPosition(0.0), horizontalZoom(20.0), initialZoomSet(false) {
    // Load configuration
    auto& config = magica::Config::getInstance();
    config.loadFromFile("magica_config.txt");  // Load from file if it exists
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
    playheadPosition = state.playhead.position;

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

    // Create track headers panel
    trackHeadersPanel = std::make_unique<TrackHeadersPanel>();
    addAndMakeVisible(trackHeadersPanel.get());

    // Create arrangement lock button
    arrangementLockButton = std::make_unique<SvgButton>("ArrangementLock", BinaryData::lock_svg,
                                                        BinaryData::lock_svgSize);
    arrangementLockButton->setTooltip("Toggle arrangement lock (F4)");
    arrangementLockButton->onClick = [this]() { toggleArrangementLock(); };
    addAndMakeVisible(arrangementLockButton.get());

    // Create time display mode toggle button
    timeDisplayToggleButton = std::make_unique<juce::TextButton>("TIME");
    timeDisplayToggleButton->setTooltip("Toggle time display (Seconds/Bars)");
    timeDisplayToggleButton->setColour(juce::TextButton::buttonColourId,
                                       DarkTheme::getColour(DarkTheme::SURFACE));
    timeDisplayToggleButton->setColour(juce::TextButton::textColourOffId,
                                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    timeDisplayToggleButton->onClick = [this]() {
        auto currentMode = timelineController->getState().display.timeDisplayMode;
        TimeDisplayMode newMode;
        bool useBarsBeats;

        if (currentMode == TimeDisplayMode::Seconds) {
            newMode = TimeDisplayMode::BarsBeats;
            timeDisplayToggleButton->setButtonText("BARS");
            useBarsBeats = true;
        } else {
            newMode = TimeDisplayMode::Seconds;
            timeDisplayToggleButton->setButtonText("TIME");
            useBarsBeats = false;
        }

        // Dispatch to controller
        timelineController->dispatch(SetTimeDisplayModeEvent{newMode});

        // Also update child components directly for now
        timeline->setTimeDisplayMode(newMode);
        trackContentPanel->setTimeDisplayMode(newMode);

        // Update loop region display with new mode
        const auto& loop = timelineController->getState().loop;
        if (onLoopRegionChanged && loop.isValid()) {
            onLoopRegionChanged(loop.startTime, loop.endTime, loop.enabled);
        }
    };
    addAndMakeVisible(timeDisplayToggleButton.get());

    // Create track content viewport
    trackContentViewport = std::make_unique<juce::Viewport>();
    trackContentPanel = std::make_unique<TrackContentPanel>();
    trackContentPanel->setController(timelineController.get());  // Connect to centralized state
    trackContentViewport->setViewedComponent(trackContentPanel.get(), false);
    trackContentViewport->setScrollBarsShown(true, true);
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
            int viewportWidth = trackContentViewport->getWidth();
            double newZoom = static_cast<double>(viewportWidth) / (rangeWidth * timelineLength);

            // Calculate scroll position
            double scrollTime = start * timelineLength;
            int scrollX = static_cast<int>(scrollTime * newZoom);

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
        // Convert range to vertical zoom and scroll
        double rangeHeight = end - start;
        if (rangeHeight > 0) {
            // Calculate vertical zoom: smaller range = higher zoom (taller tracks)
            // rangeHeight of 1.0 = seeing all tracks = zoom 1.0
            // rangeHeight of 0.5 = seeing half tracks = zoom 2.0
            double newVerticalZoom = 1.0 / rangeHeight;
            newVerticalZoom = juce::jlimit(0.5, 3.0, newVerticalZoom);

            // Apply vertical zoom
            verticalZoom = newVerticalZoom;

            // Calculate scroll position based on start position
            int totalContentHeight = trackHeadersPanel->getTotalTracksHeight();
            int scaledHeight = static_cast<int>(totalContentHeight * verticalZoom);
            int scrollY = static_cast<int>(start * scaledHeight);

            // Update track heights and viewport
            updateContentSizes();
            trackContentViewport->setViewPosition(trackContentViewport->getViewPositionX(),
                                                  scrollY);
        }
    };
    addAndMakeVisible(*verticalZoomScrollBar);

    // Layout debug panel removed for now

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
    // Remove listener before destruction
    if (timelineController) {
        timelineController->removeListener(this);
    }

    // Save configuration on shutdown
    auto& config = magica::Config::getInstance();
    config.saveToFile("magica_config.txt");
    std::cout << "🎯 CONFIG: Saved configuration on shutdown" << std::endl;
}

// ===== TimelineStateListener Implementation =====

void MainView::timelineStateChanged(const TimelineState& state) {
    // General state change handler - sync all cached values
    syncStateFromController();
}

void MainView::zoomStateChanged(const TimelineState& state) {
    // Update cached zoom values
    horizontalZoom = state.zoom.horizontalZoom;
    verticalZoom = state.zoom.verticalZoom;

    // Update child components
    timeline->setZoom(horizontalZoom);
    trackContentPanel->setZoom(horizontalZoom);
    trackContentPanel->setVerticalZoom(verticalZoom);

    // Update viewports with new scroll position
    timelineViewport->setViewPosition(state.zoom.scrollX, 0);
    trackContentViewport->setViewPosition(state.zoom.scrollX, state.zoom.scrollY);

    // Update content sizes
    updateContentSizes();

    // Update zoom scroll bars
    updateHorizontalZoomScrollBar();
    updateVerticalZoomScrollBar();

    // Repaint
    playheadComponent->repaint();
    selectionOverlay->repaint();
    repaint();
}

void MainView::playheadStateChanged(const TimelineState& state) {
    playheadPosition = state.playhead.position;
    playheadComponent->setPlayheadPosition(playheadPosition);
    playheadComponent->repaint();

    // Notify external listeners about playhead position change
    if (onPlayheadPositionChanged) {
        onPlayheadPositionChanged(playheadPosition);
    }
}

void MainView::selectionStateChanged(const TimelineState& state) {
    timeSelection = state.selection;

    // Update timeline component (only if visually active)
    if (timeSelection.isVisuallyActive()) {
        timeline->setTimeSelection(timeSelection.startTime, timeSelection.endTime);
    } else {
        timeline->clearTimeSelection();
    }

    // Update selection overlay
    if (selectionOverlay) {
        selectionOverlay->repaint();
    }

    // Notify external listeners about time selection change
    // Use isActive() here so transport info still shows selection data even when hidden visually
    if (onTimeSelectionChanged) {
        onTimeSelectionChanged(timeSelection.startTime, timeSelection.endTime,
                               timeSelection.isActive());
    }
}

void MainView::loopStateChanged(const TimelineState& state) {
    loopRegion = state.loop;

    // Prevent recursive updates
    isUpdatingLoopRegion = true;

    // Update timeline component
    if (loopRegion.isValid()) {
        timeline->setLoopRegion(loopRegion.startTime, loopRegion.endTime);
        timeline->setLoopEnabled(loopRegion.enabled);
    } else {
        timeline->clearLoopRegion();
    }

    isUpdatingLoopRegion = false;

    // Update selection overlay
    if (selectionOverlay) {
        selectionOverlay->repaint();
    }

    // Notify external listeners about loop region change
    if (onLoopRegionChanged) {
        if (loopRegion.isValid()) {
            onLoopRegionChanged(loopRegion.startTime, loopRegion.endTime, loopRegion.enabled);
        } else {
            onLoopRegionChanged(-1.0, -1.0, false);
        }
    }
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

    // Layout constants
    static constexpr int ZOOM_SCROLLBAR_SIZE = 20;
    auto& layout = LayoutConfig::getInstance();

    // Vertical zoom scroll bar on the right
    auto verticalScrollBarArea = bounds.removeFromRight(ZOOM_SCROLLBAR_SIZE);

    // Horizontal zoom scroll bar at the bottom
    auto horizontalScrollBarArea = bounds.removeFromBottom(ZOOM_SCROLLBAR_SIZE);
    // Leave space in bottom-left corner for track headers
    horizontalScrollBarArea.removeFromLeft(trackHeaderWidth + layout.componentSpacing);
    horizontalZoomScrollBar->setBounds(horizontalScrollBarArea);

    // Fixed master track row at the bottom (above horizontal scroll bar)
    auto masterRowArea = bounds.removeFromBottom(masterStripHeight);
    // Master header on the left (same width as track headers)
    masterHeaderPanel->setBounds(masterRowArea.removeFromLeft(trackHeaderWidth));
    // Padding between header and content
    masterRowArea.removeFromLeft(layout.componentSpacing);
    // Master content takes the rest
    masterContentPanel->setBounds(masterRowArea);

    // Remove space for the resize handle ABOVE the master row
    bounds.removeFromBottom(MASTER_RESIZE_HANDLE_HEIGHT);

    // Now position vertical scroll bar (after bottom areas removed)
    verticalScrollBarArea.removeFromBottom(ZOOM_SCROLLBAR_SIZE + masterStripHeight +
                                           MASTER_RESIZE_HANDLE_HEIGHT);
    verticalScrollBarArea.removeFromTop(getTimelineHeight());  // Start below timeline
    verticalZoomScrollBar->setBounds(verticalScrollBarArea);

    // Timeline viewport at the top - offset by track header width
    auto timelineArea = bounds.removeFromTop(getTimelineHeight());

    // Position buttons in the top-left corner above track headers
    auto buttonArea = timelineArea.removeFromLeft(trackHeaderWidth);
    auto topRow = buttonArea.removeFromTop(35);

    // Lock button on the left
    arrangementLockButton->setBounds(topRow.removeFromLeft(35).reduced(3));

    // Time display toggle button on the right
    timeDisplayToggleButton->setBounds(topRow.removeFromRight(50).reduced(3));

    // Add padding space for the resize handle
    timelineArea.removeFromLeft(layout.componentSpacing);  // Remove padding from timeline area too

    // Timeline takes the remaining width
    timelineViewport->setBounds(timelineArea);

    // Track headers panel on the left (variable width)
    auto trackHeadersArea = bounds.removeFromLeft(trackHeaderWidth);
    trackHeadersPanel->setBounds(trackHeadersArea);

    // Remove padding space between headers and content
    bounds.removeFromLeft(layout.componentSpacing);

    // Track content viewport gets the remaining space
    trackContentViewport->setBounds(bounds);

    // Grid and selection overlays cover the track content area
    auto overlayArea = bounds;
    // Reduce the area to avoid covering scrollbars
    int scrollBarThickness = trackContentViewport->getScrollBarThickness();
    overlayArea =
        overlayArea.withTrimmedRight(scrollBarThickness).withTrimmedBottom(scrollBarThickness);

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
    playheadArea =
        playheadArea.withTrimmedRight(scrollBarThickness).withTrimmedBottom(scrollBarThickness);
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
                auto& config = magica::Config::getInstance();
                double zoomViewDuration = config.getDefaultZoomViewDuration();
                double zoomForDefaultView = static_cast<double>(availableWidth) / zoomViewDuration;

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
    auto pixelPosition = static_cast<int>(timePosition * horizontalZoom);
    timelineViewport->setViewPosition(pixelPosition, 0);
    trackContentViewport->setViewPosition(pixelPosition, trackContentViewport->getViewPositionY());
}

void MainView::scrollToTrack(int trackIndex) {
    if (trackIndex >= 0 && trackIndex < trackHeadersPanel->getNumTracks()) {
        int yPosition = trackHeadersPanel->getTrackYPosition(trackIndex);
        trackContentViewport->setViewPosition(trackContentViewport->getViewPositionX(), yPosition);
    }
}

void MainView::addTrack() {
    trackHeadersPanel->addTrack();
    trackContentPanel->addTrack();
    updateContentSizes();
}

void MainView::removeTrack(int trackIndex) {
    trackHeadersPanel->removeTrack(trackIndex);
    trackContentPanel->removeTrack(trackIndex);
    updateContentSizes();
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

    // Update lock button icon
    if (newLockedState) {
        arrangementLockButton->updateSvgData(BinaryData::lock_svg, BinaryData::lock_svgSize);
        arrangementLockButton->setTooltip("Arrangement locked - Click to unlock (F4)");
    } else {
        arrangementLockButton->updateSvgData(BinaryData::lock_open_svg,
                                             BinaryData::lock_open_svgSize);
        arrangementLockButton->setTooltip("Arrangement unlocked - Click to lock (F4)");
    }
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

    // Check for 'S' to toggle snap to grid
    if (key == juce::KeyPress('s') || key == juce::KeyPress('S')) {
        bool newSnapState = !timelineController->getState().display.snapEnabled;
        timelineController->dispatch(SetSnapEnabledEvent{newSnapState});
        timeline->setSnapEnabled(newSnapState);  // Also update timeline directly for now
        std::cout << "🎯 SNAP: " << (newSnapState ? "enabled" : "disabled") << std::endl;
        return true;
    }

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

    // Check for Escape to clear time selection
    if (key == juce::KeyPress::escapeKey) {
        timelineController->dispatch(ClearTimeSelectionEvent{});
        return true;
    }

    return false;
}

void MainView::updateContentSizes() {
    // Use the same content width calculation as ZoomManager for consistency
    auto baseWidth = static_cast<int>(timelineLength * horizontalZoom);
    auto viewportWidth = timelineViewport->getWidth();
    auto minWidth = viewportWidth + (viewportWidth / 2);  // 1.5x viewport width for centering
    auto contentWidth = juce::jmax(baseWidth, minWidth);

    // Calculate track content height with vertical zoom
    auto baseTrackHeight = trackHeadersPanel->getTotalTracksHeight();
    auto scaledTrackHeight = static_cast<int>(baseTrackHeight * verticalZoom);

    // Update timeline size with enhanced content width
    timeline->setSize(contentWidth, getTimelineHeight());

    // Update track content size with enhanced content width and vertical zoom
    trackContentPanel->setSize(contentWidth, scaledTrackHeight);
    trackContentPanel->setVerticalZoom(verticalZoom);

    // Update track headers panel height to match content (with vertical zoom)
    trackHeadersPanel->setSize(trackHeaderWidth,
                               juce::jmax(scaledTrackHeight, trackContentViewport->getHeight()));
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

    // Update vertical zoom scroll bar when track content viewport scrolls vertically
    if (scrollBarThatHasMoved == &trackContentViewport->getVerticalScrollBar()) {
        updateVerticalZoomScrollBar();
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

    int viewportWidth = trackContentViewport->getWidth();
    int scrollX = trackContentViewport->getViewPositionX();

    // Calculate visible range as fraction of total timeline
    double visibleDuration = static_cast<double>(viewportWidth) / horizontalZoom;
    double scrollTime = static_cast<double>(scrollX) / horizontalZoom;

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

    // Apply vertical zoom to get scaled content height
    int scaledContentHeight = static_cast<int>(totalContentHeight * verticalZoom);
    if (scaledContentHeight <= 0)
        return;

    // Calculate visible range as fraction of total (scaled) content
    double visibleStart = static_cast<double>(scrollY) / scaledContentHeight;
    double visibleEnd = static_cast<double>(scrollY + viewportHeight) / scaledContentHeight;

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
    if (playheadPosition < 0 || playheadPosition > owner.timelineLength) {
        return;
    }

    // Calculate playhead position in pixels
    // Add LEFT_PADDING to align with timeline markers and track grid lines (18 pixels)
    int playheadX = static_cast<int>(playheadPosition * owner.horizontalZoom) + 18;

    // Adjust for horizontal scroll offset from track content viewport (not timeline viewport)
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    playheadX -= scrollOffset;

    // Only draw if playhead is visible
    if (playheadX >= 0 && playheadX < getWidth()) {
        // Draw playhead handle triangle in the ruler area only (no vertical line)
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        juce::Path triangle;
        triangle.addTriangle(playheadX - 6, 8, playheadX + 6, 8, playheadX, 20);
        g.fillPath(triangle);
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
    // Component now starts 20px above timeline border
    // Add LEFT_PADDING to align with timeline markers and track grid lines (18 pixels)
    int playheadX = static_cast<int>(playheadPosition * owner.horizontalZoom) + 18;

    // Adjust for horizontal scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    playheadX -= scrollOffset;

    // Check if click is near the playhead (within 10 pixels)
    if (std::abs(e.x - playheadX) <= 10) {
        isDragging = true;
        dragStartX = e.x;
        dragStartPosition = playheadPosition;
    }
}

void MainView::PlayheadComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isDragging) {
        // Calculate the change in position
        int deltaX = e.x - dragStartX;

        // Convert pixel change to time change
        double deltaTime = deltaX / owner.horizontalZoom;

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
    // Component now starts 20px above timeline border
    // Add LEFT_PADDING to align with timeline markers and track grid lines (18 pixels)
    int playheadX = static_cast<int>(playheadPosition * owner.horizontalZoom) + 18;

    // Adjust for horizontal scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    playheadX -= scrollOffset;

    // Change cursor when over playhead
    if (std::abs(event.x - playheadX) <= 10) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void MainView::mouseDown(const juce::MouseEvent& event) {
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
    // Position the resize handle in the gap between track content and master strip
    static constexpr int ZOOM_SCROLLBAR_SIZE = 20;
    // Master row top is at: getHeight() - ZOOM_SCROLLBAR_SIZE - masterStripHeight
    // Resize handle is ABOVE that
    int resizeHandleY =
        getHeight() - ZOOM_SCROLLBAR_SIZE - masterStripHeight - MASTER_RESIZE_HANDLE_HEIGHT;
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
    drawTimeSelection(g);
    drawLoopRegion(g);
}

void MainView::SelectionOverlayComponent::drawTimeSelection(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();
    if (!state.selection.isVisuallyActive()) {
        return;
    }

    // Calculate pixel positions
    // Add LEFT_PADDING (18) to align with timeline markers
    int startX = static_cast<int>(state.selection.startTime * state.zoom.horizontalZoom) + 18;
    int endX = static_cast<int>(state.selection.endTime * state.zoom.horizontalZoom) + 18;

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
    int startX = static_cast<int>(state.loop.startTime * state.zoom.horizontalZoom) + 18;
    int endX = static_cast<int>(state.loop.endTime * state.zoom.horizontalZoom) + 18;

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

    // Mute button
    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton->setClickingTogglesState(true);
    muteButton->onClick = [this]() {
        TrackManager::getInstance().setMasterMuted(muteButton->getToggleState());
    };
    addAndMakeVisible(*muteButton);

    // Solo button (for master, this could be "dim" or just solo for consistency)
    soloButton = std::make_unique<juce::TextButton>("S");
    soloButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton->setClickingTogglesState(true);
    soloButton->onClick = [this]() {
        TrackManager::getInstance().setMasterSoloed(soloButton->getToggleState());
    };
    addAndMakeVisible(*soloButton);

    // Volume slider (horizontal fader style)
    volumeSlider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    volumeSlider->setRange(0.0, 1.0);
    volumeSlider->setValue(1.0);
    volumeSlider->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    volumeSlider->setColour(juce::Slider::thumbColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    volumeSlider->onValueChange = [this]() {
        TrackManager::getInstance().setMasterVolume(static_cast<float>(volumeSlider->getValue()));
    };
    addAndMakeVisible(*volumeSlider);

    // Pan slider
    panSlider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    panSlider->setRange(-1.0, 1.0);
    panSlider->setValue(0.0);
    panSlider->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    panSlider->setColour(juce::Slider::thumbColourId, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    panSlider->onValueChange = [this]() {
        TrackManager::getInstance().setMasterPan(static_cast<float>(panSlider->getValue()));
    };
    addAndMakeVisible(*panSlider);
}

void MainView::MasterHeaderPanel::paint(juce::Graphics& g) {
    // Background - slightly different from regular tracks to distinguish
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
}

void MainView::MasterHeaderPanel::resized() {
    auto contentArea = getLocalBounds().reduced(4);

    // Top row: Name label and buttons
    auto topRow = contentArea.removeFromTop(18);
    nameLabel->setBounds(topRow.removeFromLeft(50));
    topRow.removeFromLeft(4);
    muteButton->setBounds(topRow.removeFromLeft(24));
    topRow.removeFromLeft(2);
    soloButton->setBounds(topRow.removeFromLeft(24));

    contentArea.removeFromTop(4);  // Spacing

    // Volume row with label
    auto volumeRow = contentArea.removeFromTop(14);
    volumeSlider->setBounds(volumeRow);

    contentArea.removeFromTop(2);

    // Pan row with label
    auto panRow = contentArea.removeFromTop(14);
    panSlider->setBounds(panRow);
}

void MainView::MasterHeaderPanel::masterChannelChanged() {
    const auto& master = TrackManager::getInstance().getMasterChannel();

    muteButton->setToggleState(master.muted, juce::dontSendNotification);
    soloButton->setToggleState(master.soloed, juce::dontSendNotification);
    volumeSlider->setValue(master.volume, juce::dontSendNotification);
    panSlider->setValue(master.pan, juce::dontSendNotification);

    repaint();
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

}  // namespace magica
