#include "TrackContentPanel.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magica {

TrackContentPanel::TrackContentPanel() {
    setSize(800, 400);
    
    // Add some initial tracks
    addTrack();
    addTrack();
    addTrack();
}

void TrackContentPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));
    
    // Draw track lanes
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        auto laneArea = getTrackLaneArea(static_cast<int>(i));
        if (laneArea.intersects(getLocalBounds())) {
            paintTrackLane(g, *trackLanes[i], laneArea, static_cast<int>(i) == selectedTrackIndex, static_cast<int>(i));
        }
    }
}

void TrackContentPanel::resized() {
    // Update size based on zoom and timeline length
    int contentWidth = static_cast<int>(timelineLength * currentZoom);
    int contentHeight = getTotalTracksHeight();
    
    setSize(juce::jmax(contentWidth, getWidth()), juce::jmax(contentHeight, getHeight()));
}

void TrackContentPanel::addTrack() {
    auto lane = std::make_unique<TrackLane>();
    trackLanes.push_back(std::move(lane));
    
    resized();
    repaint();
}

void TrackContentPanel::removeTrack(int index) {
    if (index >= 0 && index < trackLanes.size()) {
        trackLanes.erase(trackLanes.begin() + index);
        
        if (selectedTrackIndex == index) {
            selectedTrackIndex = -1;
        } else if (selectedTrackIndex > index) {
            selectedTrackIndex--;
        }
        
        resized();
        repaint();
    }
}

void TrackContentPanel::selectTrack(int index) {
    if (index >= 0 && index < trackLanes.size()) {
        selectedTrackIndex = index;
        
        if (onTrackSelected) {
            onTrackSelected(index);
        }
        
        repaint();
    }
}

int TrackContentPanel::getNumTracks() const {
    return static_cast<int>(trackLanes.size());
}

void TrackContentPanel::setTrackHeight(int trackIndex, int height) {
    if (trackIndex >= 0 && trackIndex < trackLanes.size()) {
        height = juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, height);
        trackLanes[trackIndex]->height = height;
        
        resized();
        repaint();
        
        if (onTrackHeightChanged) {
            onTrackHeightChanged(trackIndex, height);
        }
    }
}

int TrackContentPanel::getTrackHeight(int trackIndex) const {
    if (trackIndex >= 0 && trackIndex < trackLanes.size()) {
        return trackLanes[trackIndex]->height;
    }
    return DEFAULT_TRACK_HEIGHT;
}

void TrackContentPanel::setZoom(double zoom) {
    currentZoom = juce::jmax(0.1, zoom);
    resized();
    repaint();
}

void TrackContentPanel::setTimelineLength(double lengthInSeconds) {
    timelineLength = lengthInSeconds;
    resized();
    repaint();
}

int TrackContentPanel::getTotalTracksHeight() const {
    int totalHeight = 0;
    for (const auto& lane : trackLanes) {
        totalHeight += lane->height;
    }
    return totalHeight;
}

int TrackContentPanel::getTrackYPosition(int trackIndex) const {
    int yPosition = 0;
    for (int i = 0; i < trackIndex && i < trackLanes.size(); ++i) {
        yPosition += trackLanes[i]->height;
    }
    return yPosition;
}

void TrackContentPanel::paintTrackLane(juce::Graphics& g, const TrackLane& lane, juce::Rectangle<int> area, bool isSelected, int trackIndex) {
    // Background
    g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::TRACK_SELECTED) : DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));
    g.fillRect(area);
    
    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(area, 1);
    
    // Draw grid overlay
    paintGrid(g, area);
    
    // Track number (small text in top-left corner)
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText(juce::String(trackIndex + 1), area.getX() + 5, area.getY() + 2, 20, 15, juce::Justification::centred);
}

void TrackContentPanel::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw time grid (vertical lines)
    drawTimeGrid(g, area);
    
    // Draw beat grid (more subtle)
    drawBeatGrid(g, area);
}

void TrackContentPanel::drawTimeGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    // Make grid lines more visible
    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.2f));
    
    // Calculate appropriate grid spacing based on zoom
    const int minPixelSpacing = 30;
    int gridInterval = 1; // Start with 1 second intervals
    
    // Adjust interval if grid lines would be too close
    while (static_cast<int>(gridInterval * currentZoom) < minPixelSpacing && gridInterval < 60) {
        gridInterval *= (gridInterval < 10) ? 2 : 5; // 1,2,5,10,20,50...
    }
    
    // Draw vertical grid lines
    for (int i = 0; i <= timelineLength; i += gridInterval) {
        int x = static_cast<int>(i * currentZoom);
        if (x >= area.getX() && x <= area.getRight()) {
            g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
        }
    }
}

void TrackContentPanel::drawBeatGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw beat subdivisions (quarter notes at 120 BPM = 0.5 seconds)
    // Make beat grid more visible too
    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).withAlpha(0.5f));
    
    const double beatInterval = 0.5; // 0.5 seconds per beat at 120 BPM
    const int beatPixelSpacing = static_cast<int>(beatInterval * currentZoom);
    
    // Only draw beat grid if it's not too dense
    if (beatPixelSpacing >= 10) {
        for (double beat = 0; beat <= timelineLength; beat += beatInterval) {
            int x = static_cast<int>(beat * currentZoom);
            if (x >= area.getX() && x <= area.getRight()) {
                g.drawLine(x, area.getY(), x, area.getBottom(), 0.5f);
            }
        }
    }
}

juce::Rectangle<int> TrackContentPanel::getTrackLaneArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= trackLanes.size()) {
        return {};
    }
    
    int yPosition = getTrackYPosition(trackIndex);
    int height = trackLanes[trackIndex]->height;
    
    return juce::Rectangle<int>(0, yPosition, getWidth(), height);
}

void TrackContentPanel::mouseDown(const juce::MouseEvent& event) {
    // Select track based on click position
    for (int i = 0; i < trackLanes.size(); ++i) {
        if (getTrackLaneArea(i).contains(event.getPosition())) {
            selectTrack(i);
            break;
        }
    }
}

} // namespace magica 