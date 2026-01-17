#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "../timeline/TimelineComponent.hpp"  // For TimeDisplayMode

namespace magica {

class TrackContentPanel : public juce::Component {
  public:
    static constexpr int DEFAULT_TRACK_HEIGHT = 80;
    static constexpr int MIN_TRACK_HEIGHT = 75;
    static constexpr int MAX_TRACK_HEIGHT = 200;

    TrackContentPanel();
    ~TrackContentPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Track management
    void addTrack();
    void removeTrack(int index);
    void selectTrack(int index);
    int getNumTracks() const;
    void setTrackHeight(int trackIndex, int height);
    int getTrackHeight(int trackIndex) const;

    // Zoom management
    void setZoom(double zoom);
    double getZoom() const {
        return currentZoom;
    }
    void setVerticalZoom(double zoom);
    double getVerticalZoom() const {
        return verticalZoom;
    }

    // Timeline properties
    void setTimelineLength(double lengthInSeconds);
    double getTimelineLength() const {
        return timelineLength;
    }

    // Time display mode and tempo (for grid drawing)
    void setTimeDisplayMode(TimeDisplayMode mode);
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);

    // Get total height of all tracks
    int getTotalTracksHeight() const;

    // Get track Y position
    int getTrackYPosition(int trackIndex) const;

    // Callbacks
    std::function<void(int)> onTrackSelected;
    std::function<void(int, int)> onTrackHeightChanged;

  private:
    // Layout constants
    static constexpr int LEFT_PADDING = 18;  // Left padding to align with timeline

    struct TrackLane {
        bool selected = false;
        int height = DEFAULT_TRACK_HEIGHT;

        TrackLane() = default;
        ~TrackLane() = default;
    };

    std::vector<std::unique_ptr<TrackLane>> trackLanes;
    int selectedTrackIndex = -1;
    double currentZoom = 1.0;     // pixels per second (horizontal zoom)
    double verticalZoom = 1.0;    // track height multiplier
    double timelineLength = 0.0;  // Will be loaded from config

    // Time display mode and tempo (for grid drawing)
    TimeDisplayMode displayMode = TimeDisplayMode::Seconds;
    double tempoBPM = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Helper methods
    void paintTrackLane(juce::Graphics& g, const TrackLane& lane, juce::Rectangle<int> area,
                        bool isSelected, int trackIndex);
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area);
    juce::Rectangle<int> getTrackLaneArea(int trackIndex) const;

    // Grid drawing
    void drawTimeGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void drawBeatGrid(juce::Graphics& g, juce::Rectangle<int> area);

    // Mouse handling
    void mouseDown(const juce::MouseEvent& event) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackContentPanel)
};

}  // namespace magica
