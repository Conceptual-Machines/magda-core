#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "../../layout/LayoutConfig.hpp"

namespace magica {

// Time display mode for timeline
enum class TimeDisplayMode {
    Seconds,   // Display as 0.0s, 1.0s, 2.0s, etc.
    BarsBeats  // Display as 1.1.1, 1.2.1, 2.1.1, etc. (bar.beat.subdivision)
};

struct ArrangementSection {
    double startTime;
    double endTime;
    juce::String name;
    juce::Colour colour;

    ArrangementSection(double start, double end, const juce::String& sectionName,
                       juce::Colour sectionColour = juce::Colours::blue)
        : startTime(start), endTime(end), name(sectionName), colour(sectionColour) {}
};

class TimelineComponent : public juce::Component {
  public:
    TimelineComponent();
    ~TimelineComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

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

    // Note: Uses callbacks to communicate with ZoomManager via MainView

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

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

    // Callback for playhead position changes
    std::function<void(double)> onPlayheadPositionChanged;
    std::function<void(int, const ArrangementSection&)> onSectionChanged;
    std::function<void(const juce::String&, double, double)> onSectionAdded;
    std::function<void(double, double, int)>
        onZoomChanged;  // Callback for zoom changes (newZoom, anchorTime, anchorScreenX)
    std::function<void()> onZoomEnd;  // Callback when zoom operation ends

  private:
    // Layout constants
    static constexpr int LEFT_PADDING = 18;  // Left padding to ensure first time label is visible

    double timelineLength = 300.0;  // 5 minutes
    double playheadPosition = 0.0;
    double zoom = 1.0;         // pixels per second
    int viewportWidth = 1500;  // Default viewport width for minimum zoom calculation

    // Time display mode and tempo
    TimeDisplayMode displayMode = TimeDisplayMode::Seconds;
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

    // Arrangement interaction helpers
    int findSectionAtPosition(int x, int y) const;
    bool isOnSectionEdge(int x, int sectionIndex, bool& isStartEdge) const;
    juce::String getDefaultSectionName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};

}  // namespace magica
