#include "MainView.hpp"
#include "../themes/DarkTheme.hpp"
#include <BinaryData.h>

namespace magica {

MainView::MainView() : timelineLength(120.0), playheadPosition(0.0), horizontalZoom(20.0), initialZoomSet(false) {
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
    
    // Set initial timeline length and zoom
    setTimelineLength(120.0);
}

void MainView::setupCallbacks() {
    // Set up timeline callbacks
    timeline->onPlayheadPositionChanged = [this](double position) {
        setPlayheadPosition(position);
    };
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
    
    // Update zoom manager with viewport width (but preserve user's zoom)
    auto viewportWidth = timelineViewport->getWidth();
    if (viewportWidth > 0) {
        zoomManager->setViewportWidth(viewportWidth);
        
        // Set initial zoom to show 1 minute (60 seconds) ONLY on first resize
        if (!initialZoomSet) {
            int availableWidth = viewportWidth - 18; // Account for LEFT_PADDING
            
            if (availableWidth > 0) {
                double zoomFor1Minute = static_cast<double>(availableWidth) / 60.0; // 60 seconds = 1 minute
                
                // Ensure minimum zoom level for usability
                zoomFor1Minute = juce::jmax(zoomFor1Minute, 0.5);
                
                // Set zoom centered at the beginning of timeline
                zoomManager->setZoomCentered(zoomFor1Minute, 0.0);
                
                std::cout << "🎯 INITIAL ZOOM: showing 1 minute, viewportWidth=" << viewportWidth 
                          << ", availableWidth=" << availableWidth 
                          << ", zoomFor1Minute=" << zoomFor1Minute << std::endl;
                
                initialZoomSet = true;
            }
        } else {
            std::cout << "🎯 VIEWPORT UPDATE: width=" << viewportWidth << " (zoom preserved)" << std::endl;
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
    
    return false;
}

void MainView::updateContentSizes() {
    // Use the same content width calculation as ZoomManager for consistency
    auto baseWidth = static_cast<int>(timelineLength * horizontalZoom);
    auto viewportWidth = timelineViewport->getWidth();
    auto minWidth = viewportWidth + (viewportWidth / 2); // 1.5x viewport width for centering
    auto contentWidth = juce::jmax(baseWidth, minWidth);
    
    auto trackContentHeight = trackHeadersPanel->getTotalTracksHeight();
    
    // Update timeline size with enhanced content width
    timeline->setSize(contentWidth, TIMELINE_HEIGHT);
    
    // Update track content size with enhanced content width
    trackContentPanel->setSize(contentWidth, trackContentHeight);
    
    // Update track headers panel height to match content
    trackHeadersPanel->setSize(trackHeaderWidth, juce::jmax(trackContentHeight, trackContentViewport->getHeight()));
    
    // Repaint playhead after content size changes
    playheadComponent->repaint();
}

void MainView::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) {
    // Sync timeline viewport when track content viewport scrolls horizontally
    if (scrollBarThatHasMoved == &trackContentViewport->getHorizontalScrollBar()) {
        std::cout << "📊 SCROLLBAR MOVED: newRangeStart=" << newRangeStart 
                  << ", viewportPosX=" << trackContentViewport->getViewPositionX() << std::endl;
        
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
        // Temporarily remove scroll bar listener to prevent race condition
        trackContentViewport->getHorizontalScrollBar().removeListener(this);
        
        horizontalZoom = newZoom;
        timeline->setZoom(newZoom);
        trackContentPanel->setZoom(newZoom);
        updateContentSizes();
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
        
        // Debug output
        std::cout << "🔄 SCROLL: targetScrollX=" << scrollX 
                  << ", actualScrollX=" << trackContentViewport->getViewPositionX() 
                  << ", scrollBarStart=" << trackContentViewport->getHorizontalScrollBar().getCurrentRangeStart()
                  << ", contentWidth=" << trackContentPanel->getWidth()
                  << ", viewportWidth=" << trackContentViewport->getWidth()
                  << std::endl;
        
        // Re-add scroll bar listener
        trackContentViewport->getHorizontalScrollBar().addListener(this);
    };
    
    // Set up callback to handle content size changes
    zoomManager->onContentSizeChanged = [this]([[maybe_unused]] int contentWidth) {
        updateContentSizes();
    };
    
    // Set up timeline zoom callback to use ZoomManager with playhead centering
    timeline->onZoomChanged = [this](double newZoom, int mouseX) {
        // Set crosshair cursor during zoom operations
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        
        // Since playhead gets set on mouseDown, it's already at the desired position
        // Center zoom around the playhead position for consistent behavior
        zoomManager->setZoomCentered(newZoom, playheadPosition);
    };
    
    // Set up timeline zoom end callback
    timeline->onZoomEnd = [this]() {
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

void MainView::resetZoomToFitTimeline() {
    // Calculate zoom level that fits the entire timeline in the viewport
    int viewportWidth = trackContentViewport->getWidth();
    int availableWidth = viewportWidth - 18; // Account for LEFT_PADDING
    
    if (availableWidth > 0 && timelineLength > 0) {
        double fitZoom = static_cast<double>(availableWidth) / timelineLength;
        
        // Ensure minimum zoom level for usability
        fitZoom = juce::jmax(fitZoom, 0.5);
        
        // Set zoom centered at the beginning of timeline
        zoomManager->setZoomCentered(fitZoom, 0.0);
        
        std::cout << "🎯 ZOOM RESET: timelineLength=" << timelineLength 
                  << ", availableWidth=" << availableWidth 
                  << ", fitZoom=" << fitZoom << std::endl;
    }
}

} // namespace magica 