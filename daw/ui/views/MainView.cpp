#include "MainView.hpp"
#include "../themes/DarkTheme.hpp"

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
    
    // Create track headers panel (fixed, no scrolling)
    trackHeadersPanel = std::make_unique<TrackHeadersPanel>();
    addAndMakeVisible(*trackHeadersPanel);
    
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

MainView::~MainView() = default;

void MainView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void MainView::resized() {
    auto bounds = getLocalBounds();
    
    // Timeline viewport at the top - offset by track header width
    auto timelineArea = bounds.removeFromTop(TIMELINE_HEIGHT);
    timelineArea.removeFromLeft(TRACK_HEADER_WIDTH); // Align with track content
    timelineViewport->setBounds(timelineArea);
    
    // Track headers panel on the left (fixed width)
    auto trackHeadersArea = bounds.removeFromLeft(TRACK_HEADER_WIDTH);
    trackHeadersPanel->setBounds(trackHeadersArea);
    
    // Track content viewport gets the remaining space
    trackContentViewport->setBounds(bounds);
    
    // Playhead component covers the timeline and track content areas
    auto playheadArea = getLocalBounds();
    playheadArea.removeFromTop(0); // Include timeline area
    playheadArea.removeFromLeft(TRACK_HEADER_WIDTH); // Exclude track headers
    playheadComponent->setBounds(playheadArea);
    
    // Always recalculate zoom to ensure proper timeline distribution
    auto viewportWidth = timelineViewport->getWidth();
    if (viewportWidth > 0) {
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
    
    // Update zoom on timeline and track content
    timeline->setZoom(horizontalZoom);
    trackContentPanel->setZoom(horizontalZoom);
    
    updateContentSizes();
    repaint(); // Repaint for unified playhead
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
    updateContentSizes();
}

void MainView::setPlayheadPosition(double position) {
    playheadPosition = position;
    playheadComponent->setPlayheadPosition(position);
    playheadComponent->repaint();
}

void MainView::toggleArrangementLock() {
    timeline->setArrangementLocked(!timeline->isArrangementLocked());
    timeline->repaint();
}

bool MainView::isArrangementLocked() const {
    return timeline->isArrangementLocked();
}

bool MainView::keyPressed(const juce::KeyPress& key) {
    // Toggle arrangement lock with Ctrl+L (or Cmd+L on Mac)
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
    
    // Update track content size
    trackContentPanel->setSize(juce::jmax(contentWidth, trackContentViewport->getWidth()),
                              juce::jmax(trackContentHeight, trackContentViewport->getHeight()));
    
    // Update track headers panel height to match content
    trackHeadersPanel->setSize(TRACK_HEADER_WIDTH, juce::jmax(trackContentHeight, trackContentViewport->getHeight()));
    
    // Repaint playhead after content size changes
    playheadComponent->repaint();
}

void MainView::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) {
    // Sync timeline viewport when track content viewport scrolls horizontally
    if (scrollBarThatHasMoved == &trackContentViewport->getHorizontalScrollBar()) {
        timelineViewport->setViewPosition(static_cast<int>(newRangeStart), 0);
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

// PlayheadComponent implementation
MainView::PlayheadComponent::PlayheadComponent(MainView& owner) : owner(owner) {
    setInterceptsMouseClicks(false, false); // Allow clicks to pass through
}

MainView::PlayheadComponent::~PlayheadComponent() = default;

void MainView::PlayheadComponent::paint(juce::Graphics& g) {
    if (playheadPosition < 0 || playheadPosition > owner.timelineLength) {
        return;
    }
    
    // Calculate playhead position in pixels
    int playheadX = static_cast<int>(playheadPosition * owner.horizontalZoom);
    
    // Adjust for horizontal scroll offset
    int scrollOffset = owner.timelineViewport->getViewPositionX();
    playheadX -= scrollOffset;
    
    // Only draw if playhead is visible
    if (playheadX >= 0 && playheadX < getWidth()) {
        // Draw shadow for better visibility
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawLine(playheadX + 1, 0, playheadX + 1, getHeight(), 5.0f);
        
        // Draw main playhead line
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.drawLine(playheadX, 0, playheadX, getHeight(), 4.0f);
        
        // Draw playhead handle at the top
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        juce::Path triangle;
        triangle.addTriangle(playheadX - 6, 0, playheadX + 6, 0, playheadX, 12);
        g.fillPath(triangle);
    }
}

void MainView::PlayheadComponent::setPlayheadPosition(double position) {
    playheadPosition = position;
    repaint();
}

} // namespace magica 