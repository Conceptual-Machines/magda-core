#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <set>
#include <vector>

#include "../../state/TimelineController.hpp"
#include "core/TrackManager.hpp"

namespace magica {

// Forward declaration
class TimelineController;

class TrackContentPanel : public juce::Component,
                          public TimelineStateListener,
                          public TrackManagerListener,
                          private juce::Timer {
  public:
    static constexpr int DEFAULT_TRACK_HEIGHT = 80;
    static constexpr int MIN_TRACK_HEIGHT = 75;
    static constexpr int MAX_TRACK_HEIGHT = 200;
    static constexpr int MASTER_TRACK_HEIGHT = 60;

    TrackContentPanel();
    ~TrackContentPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TimelineStateListener implementation
    void timelineStateChanged(const TimelineState& state) override;
    void zoomStateChanged(const TimelineState& state) override;

    // TrackManagerListener implementation
    void tracksChanged() override;

    // Set the controller reference (called by MainView after construction)
    void setController(TimelineController* controller);
    TimelineController* getController() const {
        return timelineController;
    }

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
    std::function<void(double, double, std::set<int>)>
        onTimeSelectionChanged;                             // startTime, endTime, trackIndices
    std::function<void(double)> onPlayheadPositionChanged;  // Called when playhead is set via click
    std::function<double(double)>
        snapTimeToGrid;  // Callback to snap time to grid (provided by MainView)

  private:
    // Controller reference (not owned)
    TimelineController* timelineController = nullptr;

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
    TimeDisplayMode displayMode = TimeDisplayMode::BarsBeats;
    double tempoBPM = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Helper methods
    void paintTrackLane(juce::Graphics& g, const TrackLane& lane, juce::Rectangle<int> area,
                        bool isSelected, int trackIndex);
    void paintMasterLane(juce::Graphics& g, juce::Rectangle<int> area);
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area);
    juce::Rectangle<int> getTrackLaneArea(int trackIndex) const;
    juce::Rectangle<int> getMasterLaneArea() const;

    // Grid drawing
    void drawTimeGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void drawBeatGrid(juce::Graphics& g, juce::Rectangle<int> area);

    // Mouse handling
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // Mouse interaction constants and state
    static constexpr int DRAG_THRESHOLD = 3;  // Pixels of movement to distinguish click from drag
    int mouseDownX = 0;
    int mouseDownY = 0;

    // Selection state
    bool isCreatingSelection = false;
    double selectionStartTime = -1.0;
    double selectionEndTime = -1.0;

    // Per-track selection state
    bool isShiftHeld = false;
    int selectionStartTrackIndex = -1;
    int selectionEndTrackIndex = -1;

    // Move selection state
    bool isMovingSelection = false;
    double moveDragStartTime = -1.0;
    double moveSelectionOriginalStart = -1.0;
    double moveSelectionOriginalEnd = -1.0;
    std::set<int> moveSelectionOriginalTracks;

    // Pending playhead state (for double-click detection)
    double pendingPlayheadTime = -1.0;
    static constexpr int DOUBLE_CLICK_DELAY_MS = 250;

    // Timer callback for delayed playhead setting
    void timerCallback() override;

    // Helper to check if a position is in a selectable area
    bool isInSelectableArea(int x, int y) const;
    double pixelToTime(int pixel) const;
    int timeToPixel(double time) const;
    int getTrackIndexAtY(int y) const;
    bool isOnExistingSelection(int x, int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackContentPanel)
};

}  // namespace magica
