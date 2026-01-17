#include "TrackContentPanel.hpp"

#include <iostream>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "Config.hpp"

namespace magica {

TrackContentPanel::TrackContentPanel() {
    // Load configuration values
    auto& config = magica::Config::getInstance();
    timelineLength = config.getDefaultTimelineLength();

    // Set up the component
    setSize(1000, 200);
    setOpaque(true);

    // Add some default tracks
    addTrack();  // Audio Track 1
    addTrack();  // Audio Track 2
    addTrack();  // MIDI Track 1
}

void TrackContentPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    // Draw track lanes
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        auto laneArea = getTrackLaneArea(static_cast<int>(i));
        if (laneArea.intersects(getLocalBounds())) {
            paintTrackLane(g, *trackLanes[i], laneArea, static_cast<int>(i) == selectedTrackIndex,
                           static_cast<int>(i));
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

void TrackContentPanel::setVerticalZoom(double zoom) {
    verticalZoom = juce::jlimit(0.5, 3.0, zoom);
    resized();
    repaint();
}

void TrackContentPanel::setTimelineLength(double lengthInSeconds) {
    timelineLength = lengthInSeconds;
    resized();
    repaint();
}

void TrackContentPanel::setTimeDisplayMode(TimeDisplayMode mode) {
    displayMode = mode;
    repaint();
}

void TrackContentPanel::setTempo(double bpm) {
    tempoBPM = juce::jlimit(20.0, 999.0, bpm);
    repaint();
}

void TrackContentPanel::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = juce::jlimit(1, 16, numerator);
    timeSignatureDenominator = juce::jlimit(1, 16, denominator);
    repaint();
}

int TrackContentPanel::getTotalTracksHeight() const {
    int totalHeight = 0;
    for (const auto& lane : trackLanes) {
        totalHeight += static_cast<int>(lane->height * verticalZoom);
    }
    return totalHeight;
}

int TrackContentPanel::getTrackYPosition(int trackIndex) const {
    int yPosition = 0;
    for (int i = 0; i < trackIndex && i < trackLanes.size(); ++i) {
        yPosition += static_cast<int>(trackLanes[i]->height * verticalZoom);
    }
    return yPosition;
}

void TrackContentPanel::paintTrackLane(juce::Graphics& g, const TrackLane& lane,
                                       juce::Rectangle<int> area, bool isSelected, int trackIndex) {
    // Background
    g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::TRACK_SELECTED)
                           : DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));
    g.fillRect(area);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(area, 1);

    // Draw grid overlay
    paintGrid(g, area);

    // Track number removed - track names are shown in the headers panel
}

void TrackContentPanel::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw time grid (vertical lines)
    drawTimeGrid(g, area);

    // Draw beat grid (more subtle)
    drawBeatGrid(g, area);
}

void TrackContentPanel::drawTimeGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    const int minPixelSpacing = 30;

    if (displayMode == TimeDisplayMode::Seconds) {
        // ===== SECONDS MODE =====
        g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.2f));

        const double intervals[] = {0.001, 0.005, 0.01, 0.05, 0.1,  0.25, 0.5,
                                    1.0,   2.0,   5.0,  10.0, 30.0, 60.0};

        double gridInterval = 1.0;
        for (double interval : intervals) {
            if (static_cast<int>(interval * currentZoom) >= minPixelSpacing) {
                gridInterval = interval;
                break;
            }
        }

        for (double time = 0.0; time <= timelineLength; time += gridInterval) {
            int x = static_cast<int>(time * currentZoom) + LEFT_PADDING;
            if (x >= area.getX() && x <= area.getRight()) {
                g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
            }
        }
    } else {
        // ===== BARS/BEATS MODE =====
        double secondsPerBeat = 60.0 / tempoBPM;
        double secondsPerBar = secondsPerBeat * timeSignatureNumerator;

        // Find appropriate interval
        const double beatFractions[] = {0.25, 0.5, 1.0, 2.0};
        const int barMultiples[] = {1, 2, 4, 8, 16, 32};

        double markerIntervalBeats = 1.0;
        bool useBarMultiples = false;

        for (double fraction : beatFractions) {
            double intervalSeconds = secondsPerBeat * fraction;
            if (static_cast<int>(intervalSeconds * currentZoom) >= minPixelSpacing) {
                markerIntervalBeats = fraction;
                break;
            }
            if (fraction == 2.0) {
                useBarMultiples = true;
            }
        }

        if (useBarMultiples || static_cast<int>(secondsPerBar * currentZoom) < minPixelSpacing) {
            for (int mult : barMultiples) {
                double intervalSeconds = secondsPerBar * mult;
                if (static_cast<int>(intervalSeconds * currentZoom) >= minPixelSpacing) {
                    markerIntervalBeats = timeSignatureNumerator * mult;
                    break;
                }
            }
        }

        double markerIntervalSeconds = secondsPerBeat * markerIntervalBeats;

        // Draw grid lines
        for (double time = 0.0; time <= timelineLength; time += markerIntervalSeconds) {
            int x = static_cast<int>(time * currentZoom) + LEFT_PADDING;
            if (x >= area.getX() && x <= area.getRight()) {
                // Brighter line on bar boundaries
                double totalBeats = time / secondsPerBeat;
                bool isBarLine = std::fmod(totalBeats, timeSignatureNumerator) < 0.001;

                if (isBarLine) {
                    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.4f));
                    g.drawLine(x, area.getY(), x, area.getBottom(), 1.5f);
                } else {
                    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.1f));
                    g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
                }
            }
        }
    }
}

void TrackContentPanel::drawBeatGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    // Only draw beat overlay in seconds mode (bars/beats mode handles this in drawTimeGrid)
    if (displayMode == TimeDisplayMode::BarsBeats) {
        return;
    }

    // Draw beat subdivisions using actual tempo
    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).withAlpha(0.5f));

    const double beatInterval = 60.0 / tempoBPM;
    const int beatPixelSpacing = static_cast<int>(beatInterval * currentZoom);

    // Only draw beat grid if it's not too dense
    if (beatPixelSpacing >= 10) {
        for (double beat = 0.0; beat <= timelineLength; beat += beatInterval) {
            int x = static_cast<int>(beat * currentZoom) + LEFT_PADDING;
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
    int height = static_cast<int>(trackLanes[trackIndex]->height * verticalZoom);

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

}  // namespace magica
