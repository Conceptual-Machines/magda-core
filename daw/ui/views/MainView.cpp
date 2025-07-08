#include "MainView.hpp"
#include "../themes/DarkTheme.hpp"
#include <BinaryData.h>

namespace magica {

MainView::MainView() {
    // Enable keyboard focus for shortcut handling
    setWantsKeyboardFocus(true);
    
    // Create timeline viewport
    timelineViewport = std::make_unique<juce::Viewport>();
    timeline = std::make_unique<TimelineComponent>();
    timelineViewport->setViewedComponent(timeline.get(), false);
    timelineViewport->setScrollBarsShown(false, false);
    addAndMakeVisible(*timelineViewport);
    
    // Set up timeline callbacks
    timeline->onPlayheadPositionChanged = [this](double position) {
        setPlayheadPosition(position);
    };
    
    // Create zoom manager
    zoomManager = std::make_unique<ZoomManager>();
    
    // Create track headers panel
    trackHeadersPanel = std::make_unique<TrackHeadersPanel>();
    addAndMakeVisible(trackHeadersPanel.get());
    
    // Create arrangement lock button
    arrangementLockButton = std::make_unique<SvgButton>("ArrangementLock", BinaryData::lock_svg, BinaryData::lock_svgSize);
    arrangementLockButton->setTooltip("Toggle arrangement lock (F4)");
    arrangementLockButton->onClick = [this]() {
        toggleArrangementLock();
    };
    addAndMakeVisible(arrangementLockButton.get());
    
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
    
    // Set up scroll synchronization
    trackContentViewport->getHorizontalScrollBar().addListener(this);
    
    // Set up track synchronization between headers and content
    setupTrackSynchronization();
    
    // Configure zoom manager callbacks
    setupZoomManagerCallbacks();
    
    // Set initial timeline length and zoom
    setTimelineLength(120.0);
}

MainView::~MainView() = default;

void MainView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
    
    // Draw resize handle
    paintResizeHandle(g);
}

void MainView::resized() {
    auto bounds = getLocalBounds();
    
    // Timeline viewport at the top - offset by track header width
    auto timelineArea = bounds.removeFromTop(TIMELINE_HEIGHT);
    
    // Position lock button in the top-left corner above track headers
    auto lockButtonArea = timelineArea.removeFromLeft(trackHeaderWidth);
    lockButtonArea = lockButtonArea.removeFromTop(30).reduced(5); // 30px height, 5px margin
    arrangementLockButton->setBounds(lockButtonArea);
    
    // Add padding space for the resize handle
    timelineArea.removeFromLeft(HEADER_CONTENT_PADDING); // Remove padding from timeline area too
    
    // Timeline takes the remaining width
    timelineViewport->setBounds(timelineArea);
    
    // Track headers panel on the left (variable width)
    auto trackHeadersArea = bounds.removeFromLeft(trackHeaderWidth);
    trackHeadersPanel->setBounds(trackHeadersArea);
    
    // Remove padding space between headers and content
    bounds.removeFromLeft(HEADER_CONTENT_PADDING);
    
    // Track content viewport gets the remaining space
    trackContentViewport->setBounds(bounds);
    
    // Playhead component extends from above timeline down to track content 
    // This allows the triangle to be drawn in the timeline area
    auto playheadArea = bounds;
    playheadArea = playheadArea.withTop(TIMELINE_HEIGHT - 20); // Start 20px above timeline border
    // Reduce the area to avoid covering scrollbars
    int scrollBarThickness = trackContentViewport->getScrollBarThickness();
    playheadArea = playheadArea.withTrimmedRight(scrollBarThickness).withTrimmedBottom(scrollBarThickness);
    playheadComponent->setBounds(playheadArea);
    
    // Always recalculate zoom to ensure proper timeline distribution
    auto viewportWidth = timelineViewport->getWidth();
    if (viewportWidth > 0) {
        // Update zoom manager with viewport width
        zoomManager->setViewportWidth(viewportWidth);
        
        // Show about 60 seconds initially, but ensure minimum zoom for visibility
        double newZoom = juce::jmax(1.0, viewportWidth / 60.0);
        if (std::abs(horizontalZoom - newZoom) > 0.1) { // Only update if significantly different
            horizontalZoom = newZoom;
            // Update zoom on timeline and track content
            timeline->setZoom(horizontalZoom);
            trackContentPanel->setZoom(horizontalZoom);
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
        arrangementLockButton->updateSvgData(BinaryData::lock_open_svg, BinaryData::lock_open_svgSize);
        arrangementLockButton->setTooltip("Arrangement unlocked - Click to lock (F4)");
    }
}

bool MainView::isArrangementLocked() const {
    return timeline->isArrangementLocked();
}

bool MainView::keyPressed(const juce::KeyPress& key) {
    // Toggle arrangement lock with F4
    if (key.isKeyCode(juce::KeyPress::F4Key)) {
        toggleArrangementLock();
        return true;
    }
    
    return false;
}

void MainView::updateContentSizes() {
    auto contentWidth = static_cast<int>(timelineLength * horizontalZoom);
    auto trackContentHeight = trackHeadersPanel->getTotalTracksHeight();
    
    // Update timeline size
    timeline->setSize(juce::jmax(contentWidth, timelineViewport->getWidth()), TIMELINE_HEIGHT);
    
    // Update track content size - let viewport handle scrollbar ranges automatically
    trackContentPanel->setSize(contentWidth, trackContentHeight);
    
    // Update track headers panel height to match content
    trackHeadersPanel->setSize(trackHeaderWidth, juce::jmax(trackContentHeight, trackContentViewport->getHeight()));
    
    // Repaint playhead after content size changes
    playheadComponent->repaint();
}

void MainView::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) {
    // Don't interfere if this is triggered by zoom logic
    if (isUpdatingFromZoom) {
        return;
    }
    
    // Sync timeline viewport when track content viewport scrolls horizontally
    if (scrollBarThatHasMoved == &trackContentViewport->getHorizontalScrollBar()) {
        timelineViewport->setViewPosition(static_cast<int>(newRangeStart), 0);
        // Notify zoom manager of scroll position change
        zoomManager->setCurrentScrollPosition(static_cast<int>(newRangeStart));
        // Force playhead repaint when scrolling
        playheadComponent->repaint();
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

void MainView::setupZoomManagerCallbacks() {
    // Set up callback to handle zoom changes
    zoomManager->onZoomChanged = [this](double newZoom) {
        isUpdatingFromZoom = true;
        horizontalZoom = newZoom;
        timeline->setZoom(newZoom);
        trackContentPanel->setZoom(newZoom);
        updateContentSizes();
        playheadComponent->repaint();
        repaint();
        isUpdatingFromZoom = false;
    };
    
    // Set up callback to handle scroll changes
    zoomManager->onScrollChanged = [this](int scrollX) {
        isUpdatingFromZoom = true;
        timelineViewport->setViewPosition(scrollX, 0);
        trackContentViewport->setViewPosition(scrollX, trackContentViewport->getViewPositionY());
        playheadComponent->repaint();
        isUpdatingFromZoom = false;
    };
    
    // Set up callback to handle content size changes
    zoomManager->onContentSizeChanged = [this]([[maybe_unused]] int contentWidth) {
        updateContentSizes();
    };
    
    // Set up timeline zoom callback to use ZoomManager
    timeline->onZoomChanged = [this](double newZoom) {
        zoomManager->setZoom(newZoom);
    };
}

// PlayheadComponent implementation
MainView::PlayheadComponent::PlayheadComponent(MainView& owner) : owner(owner) {
    setInterceptsMouseClicks(false, true); // Only intercept clicks when hitTest returns true
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
        int newWidth = juce::jlimit(MIN_TRACK_HEADER_WIDTH, MAX_TRACK_HEADER_WIDTH, trackHeaderWidth + deltaX);
        
        if (newWidth != trackHeaderWidth) {
            trackHeaderWidth = newWidth;
            resized(); // Trigger layout update
        }
        
        lastMouseX = event.x; // Update for next drag event
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
        repaint(handleArea); // Repaint to show hover effect
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint(handleArea); // Repaint to remove hover effect
    }
}

void MainView::mouseExit([[maybe_unused]] const juce::MouseEvent& event) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(getResizeHandleArea()); // Remove hover effect
}

// Resize handle helper methods
juce::Rectangle<int> MainView::getResizeHandleArea() const {
    // Position the resize handle in the padding space between headers and content
    return juce::Rectangle<int>(trackHeaderWidth, 
                               TIMELINE_HEIGHT, 
                               HEADER_CONTENT_PADDING, 
                               getHeight() - TIMELINE_HEIGHT);
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

} // namespace magica 