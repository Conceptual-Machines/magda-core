#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "../../../utils/ScopedListener.hpp"
#include "../../layout/LayoutConfig.hpp"
#include "../../state/TimelineController.hpp"
#include "LoopMarkerInteraction.hpp"
#include "core/GestureRouter.hpp"
#include "core/TempoUtils.hpp"

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
    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

    // Set the controller reference (called by MainView after construction)
    void setController(TimelineController* controller);
    TimelineController* getController() const {
        return timelineListener_.get();
    }

    // Marker lane visibility — marker guide lines are only drawn when the
    // marker lane is shown.
    void setMarkerLaneVisible(bool visible) {
        if (markerLaneVisible_ != visible) {
            markerLaneVisible_ = visible;
            repaint();
        }
    }

    // Optional seconds ruler: splits the time ruler into bars/beats (top) and
    // seconds (bottom) rows inside its existing height. Display only.
    void setSecondsRulerVisible(bool visible) {
        if (secondsRulerVisible_ != visible) {
            secondsRulerVisible_ = visible;
            repaint();
        }
    }
    bool isSecondsRulerVisible() const {
        return secondsRulerVisible_;
    }

    // Timeline controls
    void setTimelineLength(double lengthInSeconds);
    void setPlayheadPosition(double position);
    void setPlayheadPositionBeats(double positionBeats);
    void setZoom(double pixelsPerBeat);
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
    void addSectionBeats(const juce::String& name, double startBeats, double endBeats,
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
    void setLoopRegionBeats(double startBeats, double endBeats);
    void clearLoopRegion();
    bool isLoopEnabled() const {
        return loopInteraction_.isEnabled();
    }
    void setLoopEnabled(bool enabled);
    double getLoopStartBeats() const {
        return loopInteraction_.getStartPosition();
    }
    double getLoopEndBeats() const {
        return loopInteraction_.getEndPosition();
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
    void setTimeSelectionBeats(double startBeats, double endBeats);
    void clearTimeSelection();

    // Callback for playhead position changes
    std::function<void(double, bool)> onPlayheadPositionBeatsChanged;
    std::function<void(int, const ArrangementSection&)> onSectionChanged;
    std::function<void(const juce::String&, double, double)> onSectionAddedBeats;
    std::function<void(double, double, int)>
        onZoomChanged;  // Callback for zoom changes (newZoom, anchorBeats, anchorScreenX)
    std::function<void()> onZoomEnd;  // Callback when zoom operation ends
    std::function<void(double, double)>
        onLoopRegionBeatsChanged;  // Callback when loop region changes
    std::function<void(const ResolvedGesture&)>
        onArrangementGesture;  // Wheel gesture resolved via GestureRouter (#21/#26)
    std::function<void(double, double)>
        onTimeSelectionBeatsChanged;  // Callback when time selection changes in ruler
    std::function<void(double, double)>
        onZoomToFitBeatsRequested;  // Callback to zoom to fit a beat range (startBeats, endBeats)

  private:
    // RAII listener guard — destroyed before cached state below
    ScopedListener<TimelineController, TimelineStateListener> timelineListener_{this};

    // Layout: use LayoutConfig::TIMELINE_LEFT_PADDING directly

    // Local state (cached from controller for quick access during rendering)
    // These are updated via TimelineStateListener callbacks
    double timelineLength = 300.0;  // 5 minutes
    double playheadPositionBeats = 0.0;
    double pixelsPerBeat = 1.0;  // Horizontal zoom
    int viewportWidth = 1500;    // Default viewport width for minimum zoom calculation

    // Time display mode and tempo
    TimeDisplayMode displayMode = TimeDisplayMode::BarsBeats;
    double tempoBPM = DEFAULT_BPM;
    int timeSignatureNumerator = DEFAULT_TIME_SIGNATURE_NUMERATOR;
    int timeSignatureDenominator = DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    std::vector<TimelineMarker> markers_;

    // Arrangement sections
    std::vector<std::unique_ptr<ArrangementSection>> sections;
    int selectedSectionIndex = -1;
    bool isDraggingSection = false;
    bool isDraggingEdge = false;
    bool isDraggingStart = false;    // true for start edge, false for end edge
    bool arrangementLocked = false;  // Lock arrangement sections to prevent accidental movement

    // Loop marker interaction helper
    LoopMarkerInteraction loopInteraction_;
    void initLoopInteraction();

    // Snap to grid state
    bool snapEnabled = true;    // Snap enabled by default
    GridQuantize gridQuantize;  // Grid quantize settings

    // Time selection state (for ruler highlight)
    double timeSelectionStartBeats = -1.0;
    double timeSelectionEndBeats = -1.0;
    bool isDraggingTimeSelection = false;
    double timeSelectionDragStartBeats = -1.0;  // Initial drag position for time selection

    bool markerLaneVisible_ = true;     // Gates marker guide-line drawing
    bool secondsRulerVisible_ = false;  // Splits the ruler into bars + seconds rows

    // Mouse interaction state
    bool isZooming = false;
    bool isPendingPlayheadClick = false;  // True if we might set playhead on mouseUp
    int mouseDownX = 0;
    int mouseDownY = 0;
    double zoomStartValue = 1.0;
    double zoomAnchorBeats = 0.0;  // Beat position to keep stable during zoom
    int zoomAnchorScreenX = 0;     // Screen X position where anchor should stay
    GestureAxis zoomDragAxis = GestureAxis::Vertical;
    static constexpr int DRAG_THRESHOLD = 5;  // Pixels of movement before it's a drag

    // Helper methods — beats are the native domain
    int beatsToPixel(double beats) const;
    double pixelToBeats(int pixel) const;
    double secondsToBeats(double timeInSeconds) const;
    double beatsToSeconds(double beats) const;
    double getTimelineLengthBeats() const;
    double snapBeatsToGrid(double beats) const;
    int secondsDurationToPixels(double durationSeconds) const;  // Seconds display mode only
    void drawTimeMarkers(juce::Graphics& g);
    void drawPlayhead(juce::Graphics& g);
    void drawArrangementSections(juce::Graphics& g);
    void drawSection(juce::Graphics& g, const ArrangementSection& section, bool isSelected) const;
    void drawLoopMarkers(juce::Graphics& g);      // Draws shaded region (background)
    void drawLoopMarkerFlags(juce::Graphics& g);  // Draws triangular flags (foreground)
    void drawTimeSelection(juce::Graphics& g);
    void drawMarkerGuides(juce::Graphics& g);
    // Draws a bar-number label, masking the dashed marker guide behind it with
    // a small padded background box so the line never slashes through the digits.
    void drawBarNumberLabel(juce::Graphics& g, const juce::String& text, int x, int labelY,
                            int labelHeight);
    // Draws a bar's time (bars translated to seconds) in the seconds row, at the
    // same x as the bar number.
    void drawSecondsBandLabel(juce::Graphics& g, int x, const juce::String& text, bool isFirstBar);
    bool secondsRowShown() const;
    // Bar numbers grow when the seconds row is hidden (the bars row is taller).
    float barLabelFontSize() const;

    // The ruler is a fixed-height stack of four rows. When the seconds row is
    // hidden its height folds into the bars row, so the total never changes.
    struct RulerRows {
        int barsTop, barsBottom;
        int secondsTop, secondsBottom;  // equal (zero height) when hidden
        int loopTop, loopBottom;
        int playheadTop, playheadBottom;
        bool hasSeconds;
    };
    RulerRows rulerRows() const;

    // Arrangement interaction helpers
    int findSectionAtPosition(int x, int y) const;
    bool isOnSectionEdge(int x, int sectionIndex, bool& isStartEdge) const;
    juce::String getDefaultSectionName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};

}  // namespace magda
