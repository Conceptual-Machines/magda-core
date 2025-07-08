#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <memory>

namespace magica {

struct ArrangementSection {
    double startTime;
    double endTime;
    juce::String name;
    juce::Colour colour;
    
    ArrangementSection(double start, double end, const juce::String& sectionName, juce::Colour sectionColour = juce::Colours::blue)
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

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    
    // Arrangement section management
    void addSection(const juce::String& name, double startTime, double endTime, juce::Colour colour = juce::Colours::blue);
    void removeSection(int index);
    void clearSections();
    
    // Arrangement locking
    void setArrangementLocked(bool locked) { arrangementLocked = locked; }
    bool isArrangementLocked() const { return arrangementLocked; }
    
    // Callback for playhead position changes
    std::function<void(double)> onPlayheadPositionChanged;
    std::function<void(int, const ArrangementSection&)> onSectionChanged;
    std::function<void(const juce::String&, double, double)> onSectionAdded;
    std::function<void(double)> onZoomChanged; // Callback for zoom changes

private:
    // Layout constants
    static constexpr int LEFT_PADDING = 18; // Left padding to ensure first time label is visible
    
    double timelineLength = 300.0;
    double playheadPosition = 0.0;
    double zoom = 1.0; // pixels per second
    double viewStartTime = 0.0; // What time is shown at the left edge of the view
    
    // Arrangement sections
    std::vector<std::unique_ptr<ArrangementSection>> sections;
    int selectedSectionIndex = -1;
    bool isDraggingSection = false;
    bool isDraggingEdge = false;
    bool isDraggingStart = false; // true for start edge, false for end edge
    bool arrangementLocked = false; // Lock arrangement sections to prevent accidental movement
    
    // Zoom interaction state
    bool isZooming = false;
    int zoomStartY = 0;
    double zoomStartValue = 1.0;
    
    // Helper methods
    double pixelToTime(int pixel) const;
    int timeToPixel(double time) const;
    int timeDurationToPixels(double duration) const; // For calculating spacing/widths
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

} // namespace magica 