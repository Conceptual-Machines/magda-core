#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "../../layout/LayoutConfig.hpp"
#include "../../state/TimelineController.hpp"

namespace magda {

// Forward declaration
class TimelineController;

// TimeDisplayMode and ArrangementSection are now defined in TimelineState.hpp

class TimelineComponent : public juce::Component, public TimelineStateListener {
  public:
    TimelineComponent();
    ~TimelineComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TimelineStateListener implementation
    void timelineStateChanged(const TimelineState& state) override;
    void zoomStateChanged(const TimelineState& state) override;
    void loopStateChanged(const TimelineState& state) override;
    void selectionStateChanged(const TimelineState& state) override;

    // Set the controller reference (called by MainView after construction)
    void setController(TimelineController* controller);
    TimelineController* getController() const {
        return timelineController;
    }

    // Timeline controls
    void setTimelineLength(double lengthInSeconds);
    void setPlayheadPosition(double position);
    void setZoom(double pixelsPerSecond);
    void setViewportWidth(int width);  // For calculating minimum zoom

    // Time display mode
    void setTimeDisplayMode(TimeDisplayMode mode);
    TimeDisplayMode getTimeDisplayMode() const {
        return displayMode;
    }

    // Tempo settings
    void setTempo(double bpm);
    double getTempo() const {
        return tempoBPM;
    }
    void setTimeSignature(int numerator, int denominator);
    int getTimeSignatureNumerator() const {
        return timeSignatureNumerator;
    }
    int getTimeSignatureDenominator() const {
        return timeSignatureDenominator;
    }

    // Conversion helpers
    double timeToBars(double timeInSeconds) const;
    double barsToTime(double bars) const;
    juce::String formatTimePosition(double timeInSeconds) const;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    // Arrangement section management
    void addSection(const juce::String& name, double startTime, double endTime,
                    juce::Colour colour = juce::Colours::blue);
    void removeSection(int index);
    void clearSections();

    // Arrangement locking
    void setArrangementLocked(bool locked) {
        arrangementLocked = locked;
    }
    bool isArrangementLocked() const {
        return arrangementLocked;
    }

    // Loop region management
    void setLoopRegion(double startTime, double endTime);
    void clearLoopRegion();
    bool isLoopEnabled() const {
        return loopEnabled;
    }
    void setLoopEnabled(bool enabled);
    double getLoopStartTime() const {
        return loopStartTime;
    }
    double getLoopEndTime() const {
        return loopEndTime;
    }

    // Snap to grid
    void setSnapEnabled(bool enabled) {
        snapEnabled = enabled;
    }
    bool isSnapEnabled() const {
        return snapEnabled;
    }
    double snapTimeToGrid(double time) const;
    double getSnapInterval() const;  // Returns current snap interval based on zoom and display mode

    // Time selection (for visual feedback in ruler area)
    void setTimeSelection(double startTime, double endTime);
    void clearTimeSelection();

    // Callback for playhead position changes
    std::function<void(double)> onPlayheadPositionChanged;
    std::function<void(int, const ArrangementSection&)> onSectionChanged;
    std::function<void(const juce::String&, double, double)> onSectionAdded;
    std::function<void(double, double, int)>
        onZoomChanged;  // Callback for zoom changes (newZoom, anchorTime, anchorScreenX)
    std::function<void()> onZoomEnd;                          // Callback when zoom operation ends
    std::function<void(double, double)> onLoopRegionChanged;  // Callback when loop region changes
    std::function<void(float deltaX, float deltaY)>
        onScrollRequested;  // Callback for scroll requests from mouse wheel
    std::function<void(double, double)>
        onTimeSelectionChanged;  // Callback when time selection changes in ruler
    std::function<void(double, double)>
        onZoomToFitRequested;  // Callback to zoom to fit a time range (startTime, endTime)

  private:
    // Controller reference (not owned)
    TimelineController* timelineController = nullptr;

    // Layout constants - use shared constant from LayoutConfig
    static constexpr int LEFT_PADDING = LayoutConfig::TIMELINE_LEFT_PADDING;

    // Local state (cached from controller for quick access during rendering)
    // These are updated via TimelineStateListener callbacks
    double timelineLength = 300.0;  // 5 minutes
    double playheadPosition = 0.0;
    double zoom = 1.0;         // pixels per second
    int viewportWidth = 1500;  // Default viewport width for minimum zoom calculation

    // Time display mode and tempo
    TimeDisplayMode displayMode = TimeDisplayMode::BarsBeats;
    double tempoBPM = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Arrangement sections
    std::vector<std::unique_ptr<ArrangementSection>> sections;
    int selectedSectionIndex = -1;
    bool isDraggingSection = false;
    bool isDraggingEdge = false;
    bool isDraggingStart = false;    // true for start edge, false for end edge
    bool arrangementLocked = false;  // Lock arrangement sections to prevent accidental movement

    // Loop region state
    double loopStartTime = -1.0;
    double loopEndTime = -1.0;
    bool loopEnabled = false;
    bool isDraggingLoopStart = false;
    bool isDraggingLoopEnd = false;
    bool isDraggingLoopRegion = false;  // Dragging entire loop region
    double loopDragOffset = 0.0;        // Offset from click position to loop start

    // Snap to grid state
    bool snapEnabled = true;    // Snap enabled by default
    GridQuantize gridQuantize;  // Grid quantize settings

    // Time selection state (for ruler highlight)
    double timeSelectionStart = -1.0;
    double timeSelectionEnd = -1.0;
    bool isDraggingTimeSelection = false;
    double timeSelectionDragStart = -1.0;  // Initial drag position for time selection

    // Mouse interaction state
    bool isZooming = false;
    bool isPendingPlayheadClick = false;  // True if we might set playhead on mouseUp
    int mouseDownX = 0;
    int mouseDownY = 0;
    double zoomStartValue = 1.0;
    double zoomAnchorTime = 0.0;              // Time position to keep stable during zoom
    int zoomAnchorScreenX = 0;                // Screen X position where anchor should stay
    static constexpr int DRAG_THRESHOLD = 5;  // Pixels of movement before it's a drag

    // Helper methods
    double pixelToTime(int pixel) const;
    int timeToPixel(double time) const;
    int timeDurationToPixels(double duration) const;  // For calculating spacing/widths
    void drawTimeMarkers(juce::Graphics& g);
    void drawPlayhead(juce::Graphics& g);
    void drawArrangementSections(juce::Graphics& g);
    void drawSection(juce::Graphics& g, const ArrangementSection& section, bool isSelected) const;
    void drawLoopMarkers(juce::Graphics& g);      // Draws shaded region (background)
    void drawLoopMarkerFlags(juce::Graphics& g);  // Draws triangular flags (foreground)
    void drawTimeSelection(juce::Graphics& g);

    // Arrangement interaction helpers
    int findSectionAtPosition(int x, int y) const;
    bool isOnSectionEdge(int x, int sectionIndex, bool& isStartEdge) const;
    juce::String getDefaultSectionName() const;

    // Loop marker interaction helpers
    bool isOnLoopMarker(int x, int y, bool& isStartMarker) const;
    bool isOnLoopTopBorder(int x, int y) const;  // Check if on the top connecting line

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};

}  // namespace magda
