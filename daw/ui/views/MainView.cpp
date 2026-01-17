#include "MainView.hpp"

#include <BinaryData.h>

#include <iostream>

#include "../themes/DarkTheme.hpp"
#include "Config.hpp"

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

    // Set up zoom manager
    setupZoomManager();

    // Set up UI components
    setupComponents();

    // Set up callbacks
    setupCallbacks();

    // Connect zoom manager callbacks
    setupZoomManagerCallbacks();
}

void MainView::setupZoomManager() {
    // Create zoom manager
    zoomManager = std::make_unique<ZoomManager>();
}

void MainView::setupComponents() {
    // Create timeline viewport
    timelineViewport = std::make_unique<juce::Viewport>();
    timeline = std::make_unique<TimelineComponent>();
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
        auto currentMode = timeline->getTimeDisplayMode();
        if (currentMode == TimeDisplayMode::Seconds) {
            timeline->setTimeDisplayMode(TimeDisplayMode::BarsBeats);
            trackContentPanel->setTimeDisplayMode(TimeDisplayMode::BarsBeats);
            timeDisplayToggleButton->setButtonText("BARS");
        } else {
            timeline->setTimeDisplayMode(TimeDisplayMode::Seconds);
            trackContentPanel->setTimeDisplayMode(TimeDisplayMode::Seconds);
            timeDisplayToggleButton->setButtonText("TIME");
        }
    };
    addAndMakeVisible(timeDisplayToggleButton.get());

    // Create track content viewport
    trackContentViewport = std::make_unique<juce::Viewport>();
    trackContentPanel = std::make_unique<TrackContentPanel>();
    trackContentViewport->setViewedComponent(trackContentPanel.get(), false);
    trackContentViewport->setScrollBarsShown(true, true);
    addAndMakeVisible(*trackContentViewport);

    // Create playhead component (always on top)
    playheadComponent = std::make_unique<PlayheadComponent>(*this);
    addAndMakeVisible(*playheadComponent);
    playheadComponent->toFront(false);

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

            // Update via zoom manager
            zoomManager->setZoom(newZoom);
            zoomManager->setCurrentScrollPosition(scrollX);
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

    // Create layout debug panel (F11 to toggle)
    layoutDebugPanel = std::make_unique<LayoutDebugPanel>();
    layoutDebugPanel->setVisible(false);
    layoutDebugPanel->onLayoutChanged = [this]() {
        resized();
        repaint();
    };
    addAndMakeVisible(*layoutDebugPanel);
    layoutDebugPanel->toFront(false);

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
        setPlayheadPosition(position);
    };
}

MainView::~MainView() {
    // Save configuration on shutdown
    auto& config = magica::Config::getInstance();
    config.saveToFile("magica_config.txt");
    std::cout << "🎯 CONFIG: Saved configuration on shutdown" << std::endl;
}

void MainView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Draw resize handle
    paintResizeHandle(g);
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

    // Now position vertical scroll bar (after horizontal removed its bottom portion)
    verticalScrollBarArea.removeFromBottom(ZOOM_SCROLLBAR_SIZE);  // Don't overlap corner
    verticalScrollBarArea.removeFromTop(getTimelineHeight());     // Start below timeline
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

    // Playhead component extends from above timeline down to track content
    // This allows the triangle to be drawn in the timeline area
    auto playheadArea = bounds;
    playheadArea =
        playheadArea.withTop(getTimelineHeight() - 20);  // Start 20px above timeline border
    // Reduce the area to avoid covering scrollbars
    int scrollBarThickness = trackContentViewport->getScrollBarThickness();
    playheadArea =
        playheadArea.withTrimmedRight(scrollBarThickness).withTrimmedBottom(scrollBarThickness);
    playheadComponent->setBounds(playheadArea);

    // Position layout debug panel in top-right corner
    if (layoutDebugPanel != nullptr) {
        int panelWidth = layoutDebugPanel->getWidth();
        int panelHeight = layoutDebugPanel->getHeight();
        layoutDebugPanel->setBounds(getWidth() - panelWidth - 10, 10, panelWidth, panelHeight);
    }

    // Update zoom manager with viewport width (but preserve user's zoom)
    auto viewportWidth = timelineViewport->getWidth();
    if (viewportWidth > 0) {
        zoomManager->setViewportWidth(viewportWidth);
        // Also update timeline component with viewport width for minimum zoom calculation
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

                // Set zoom centered at the beginning of timeline
                zoomManager->setZoomCentered(zoomForDefaultView, 0.0);

                std::cout << "🎯 INITIAL ZOOM: showing " << zoomViewDuration
                          << " seconds, availableWidth=" << availableWidth
                          << ", zoomForDefaultView=" << zoomForDefaultView << std::endl;

                initialZoomSet = true;
            }
        } else {
            std::cout << "🎯 VIEWPORT UPDATE: width=" << viewportWidth << " (zoom preserved)"
                      << std::endl;
        }
    }

    updateContentSizes();
}

void MainView::setHorizontalZoom(double zoomFactor) {
    horizontalZoom = juce::jmax(0.1, zoomFactor);
    zoomManager->setZoom(horizontalZoom);
    // Ensure horizontalZoom stays in sync with ZoomManager
    horizontalZoom = zoomManager->getCurrentZoom();
}

void MainView::setVerticalZoom(double zoomFactor) {
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
    timelineLength = lengthInSeconds;
    timeline->setTimelineLength(lengthInSeconds);
    trackContentPanel->setTimelineLength(lengthInSeconds);
    zoomManager->setTimelineLength(lengthInSeconds);
}

void MainView::setPlayheadPosition(double position) {
    playheadPosition = juce::jlimit(0.0, timelineLength, position);
    playheadComponent->setPlayheadPosition(playheadPosition);
    playheadComponent->repaint();
}

void MainView::toggleArrangementLock() {
    timeline->setArrangementLocked(!timeline->isArrangementLocked());
    timeline->repaint();

    // Update lock button icon
    if (timeline->isArrangementLocked()) {
        arrangementLockButton->updateSvgData(BinaryData::lock_svg, BinaryData::lock_svgSize);
        arrangementLockButton->setTooltip("Arrangement locked - Click to unlock (F4)");
    } else {
        arrangementLockButton->updateSvgData(BinaryData::lock_open_svg,
                                             BinaryData::lock_open_svgSize);
        arrangementLockButton->setTooltip("Arrangement unlocked - Click to lock (F4)");
    }
}

bool MainView::isArrangementLocked() const {
    return timeline->isArrangementLocked();
}

// Add keyboard event handler for zoom reset shortcut
bool MainView::keyPressed(const juce::KeyPress& key) {
    // Check for Ctrl+0 (or Cmd+0 on Mac) to reset zoom to fit timeline
    if (key == juce::KeyPress('0', juce::ModifierKeys::commandModifier, 0)) {
        resetZoomToFitTimeline();
        return true;
    }

    // Check for F4 to toggle arrangement lock
    if (key == juce::KeyPress::F4Key) {
        toggleArrangementLock();
        return true;
    }

    // Check for F11 to toggle layout debug panel
    if (key == juce::KeyPress::F11Key) {
        layoutDebugPanel->setVisible(!layoutDebugPanel->isVisible());
        if (layoutDebugPanel->isVisible()) {
            layoutDebugPanel->toFront(false);
        }
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
        timelineViewport->setViewPosition(static_cast<int>(newRangeStart), 0);
        // Notify zoom manager of scroll position change
        zoomManager->setCurrentScrollPosition(static_cast<int>(newRangeStart));
        // Update zoom scroll bar
        updateHorizontalZoomScrollBar();
        // Force playhead repaint when scrolling
        playheadComponent->repaint();
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

void MainView::setupZoomManagerCallbacks() {
    // Set up callback to handle zoom changes
    zoomManager->onZoomChanged = [this](double newZoom) {
        // Temporarily remove scroll bar listener to prevent race condition
        trackContentViewport->getHorizontalScrollBar().removeListener(this);

        horizontalZoom = newZoom;
        timeline->setZoom(newZoom);
        trackContentPanel->setZoom(newZoom);
        updateContentSizes();
        updateHorizontalZoomScrollBar();
        playheadComponent->repaint();
        repaint();

        // Re-add scroll bar listener after zoom operations are complete
        trackContentViewport->getHorizontalScrollBar().addListener(this);
    };

    // Set up callback to handle zoom end
    zoomManager->onZoomEnd = [this]() {
        // Zoom end handling - cursor is managed by onCursorChanged callback
    };

    // Set up callback to handle cursor changes
    zoomManager->onCursorChanged = [this](juce::MouseCursor::StandardCursorType cursorType) {
        setMouseCursor(cursorType);
    };

    // Set up callback to handle scroll changes
    zoomManager->onScrollChanged = [this](int scrollX) {
        // Temporarily remove scroll bar listener to prevent feedback loops
        trackContentViewport->getHorizontalScrollBar().removeListener(this);

        // Set both viewports to the same position and let JUCE handle scroll bars
        timelineViewport->setViewPosition(scrollX, 0);
        trackContentViewport->setViewPosition(scrollX, trackContentViewport->getViewPositionY());

        // Force viewport to update its scrollbars
        trackContentViewport->resized();

        // Update zoom scroll bar to reflect new position
        updateHorizontalZoomScrollBar();

        // Re-add scroll bar listener
        trackContentViewport->getHorizontalScrollBar().addListener(this);
    };

    // Set up callback to handle content size changes
    zoomManager->onContentSizeChanged = [this]([[maybe_unused]] int contentWidth) {
        updateContentSizes();
    };

    // Set up timeline zoom callback to use ZoomManager with mouse-centered zoom
    timeline->onZoomChanged = [this](double newZoom, double anchorTime, int anchorContentX) {
        // Set crosshair cursor during zoom operations
        setMouseCursor(juce::MouseCursor::CrosshairCursor);

        // On first zoom callback, capture the viewport-relative position
        // anchorContentX is in content coordinates, we need viewport-relative
        if (!isZoomActive) {
            isZoomActive = true;
            int currentScrollX = trackContentViewport->getViewPositionX();
            zoomAnchorViewportX = anchorContentX - currentScrollX;
        }

        // Calculate scroll position to keep anchorTime at the same viewport position
        int anchorPixelPos = static_cast<int>(anchorTime * newZoom) + 18;
        int newScrollX = anchorPixelPos - zoomAnchorViewportX;

        // Clamp scroll to valid range
        int contentWidth = static_cast<int>(timelineLength * newZoom);
        int viewportWidth = trackContentViewport->getWidth();
        int maxScrollX = juce::jmax(0, contentWidth - viewportWidth);
        newScrollX = juce::jlimit(0, maxScrollX, newScrollX);

        // Update zoom and scroll directly
        zoomManager->setZoom(newZoom);
        zoomManager->setCurrentScrollPosition(newScrollX);

        // Trigger scroll update
        if (zoomManager->onScrollChanged) {
            zoomManager->onScrollChanged(newScrollX);
        }
    };

    // Set up timeline zoom end callback
    timeline->onZoomEnd = [this]() {
        // Reset zoom anchor tracking for next zoom operation
        isZoomActive = false;

        // Reset cursor to normal when zoom ends
        setMouseCursor(juce::MouseCursor::NormalCursor);

        // Call ZoomManager's zoom end callback
        if (zoomManager->onZoomEnd) {
            zoomManager->onZoomEnd();
        }
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

    // Debug output to diagnose sync issues
    std::cout << "🎯 PLAYHEAD: pos=" << playheadPosition << ", zoom=" << owner.horizontalZoom
              << ", scrollOffset=" << scrollOffset << ", finalX=" << playheadX << std::endl;

    // Only draw if playhead is visible
    if (playheadX >= 0 && playheadX < getWidth()) {
        // Draw playhead handle triangle sitting entirely in timeline area
        // Triangle top at y=8 (timeline area), point at y=20 (exactly on timeline border)
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        juce::Path triangle;
        triangle.addTriangle(playheadX - 6, 8, playheadX + 6, 8, playheadX, 20);
        g.fillPath(triangle);

        // Draw playhead line from timeline border down through tracks
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawLine(playheadX + 1, 20, playheadX + 1, getHeight(), 5.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.drawLine(playheadX, 20, playheadX, getHeight(), 4.0f);
    }
}

void MainView::PlayheadComponent::setPlayheadPosition(double position) {
    playheadPosition = position;
    repaint();
}

bool MainView::PlayheadComponent::hitTest(int x, [[maybe_unused]] int y) {
    // Only intercept mouse events when near the actual playhead
    if (playheadPosition < 0 || playheadPosition > owner.timelineLength) {
        return false;
    }

    // Calculate playhead position in pixels
    int playheadX = static_cast<int>(playheadPosition * owner.horizontalZoom) + 18;

    // Adjust for horizontal scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    playheadX -= scrollOffset;

    // Only intercept if within 10 pixels of the playhead
    return std::abs(x - playheadX) <= 10;
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
}

void MainView::mouseUp([[maybe_unused]] const juce::MouseEvent& event) {
    if (isResizingHeaders) {
        isResizingHeaders = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // Removed zoom handling - timeline component handles its own zoom
}

void MainView::mouseMove(const juce::MouseEvent& event) {
    auto handleArea = getResizeHandleArea();

    if (handleArea.contains(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        repaint(handleArea);  // Repaint to show hover effect
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint(handleArea);  // Repaint to remove hover effect
    }
}

void MainView::mouseExit([[maybe_unused]] const juce::MouseEvent& event) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(getResizeHandleArea());  // Remove hover effect
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

void MainView::resetZoomToFitTimeline() {
    // Calculate zoom level that fits the entire timeline in the viewport
    int viewportWidth = trackContentViewport->getWidth();
    int availableWidth = viewportWidth - 18;  // Account for LEFT_PADDING

    if (availableWidth > 0 && timelineLength > 0) {
        double fitZoom = static_cast<double>(availableWidth) / timelineLength;

        // Ensure minimum zoom level for usability
        fitZoom = juce::jmax(fitZoom, 0.5);

        // Set zoom centered at the beginning of timeline
        zoomManager->setZoomCentered(fitZoom, 0.0);

        std::cout << "🎯 ZOOM RESET: timelineLength=" << timelineLength
                  << ", availableWidth=" << availableWidth << ", fitZoom=" << fitZoom << std::endl;
    }
}

}  // namespace magica
