#include "TimelineComponent.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magica {

TimelineComponent::TimelineComponent() {
    setSize(800, 40);
    
    // Create some sample arrangement sections
    addSection("Intro", 0, 8, juce::Colours::green);
    addSection("Verse 1", 8, 24, juce::Colours::blue);
    addSection("Chorus", 24, 40, juce::Colours::orange);
    addSection("Verse 2", 40, 56, juce::Colours::blue);
    addSection("Bridge", 56, 72, juce::Colours::purple);
    addSection("Outro", 72, 88, juce::Colours::red);
    
    // Lock arrangement sections by default to prevent accidental movement
    arrangementLocked = true;
}

TimelineComponent::~TimelineComponent() = default;

void TimelineComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND));
    
    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    
    // Draw arrangement sections first (behind time markers)
    drawArrangementSections(g);
    drawTimeMarkers(g);
    // Note: Playhead is now drawn by MainView's unified playhead component
}

void TimelineComponent::resized() {
    // Zoom is now controlled by parent component for proper synchronization
    // No automatic zoom calculation here
}

void TimelineComponent::setTimelineLength(double lengthInSeconds) {
    timelineLength = lengthInSeconds;
    resized();
    repaint();
}

void TimelineComponent::setPlayheadPosition(double position) {
    playheadPosition = position;
    repaint();
}

void TimelineComponent::setZoom(double pixelsPerSecond) {
    zoom = pixelsPerSecond;
    repaint();
}

void TimelineComponent::mouseDown(const juce::MouseEvent& event) {
    // Always prioritize playhead positioning unless specifically targeting arrangement sections
    // and arrangement sections are unlocked
    if (!arrangementLocked && event.y <= getHeight() / 2) {
        // Check if clicking on arrangement section (only in upper half)
        int sectionIndex = findSectionAtPosition(event.x, event.y);
        if (sectionIndex >= 0) {
            selectedSectionIndex = sectionIndex;
            
            // Check if clicking on section edge for resizing
            bool isStartEdge;
            if (isOnSectionEdge(event.x, sectionIndex, isStartEdge)) {
                isDraggingEdge = true;
                isDraggingStart = isStartEdge;
            } else {
                isDraggingSection = true;
            }
            repaint();
            return;
        }
    }
    
    // Default behavior: handle playhead positioning
    double clickTime = pixelToTime(event.x);
    setPlayheadPosition(clickTime);
    
    // Notify parent of position change
    if (onPlayheadPositionChanged) {
        onPlayheadPositionChanged(clickTime);
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& event) {
    if (!arrangementLocked && isDraggingSection && selectedSectionIndex >= 0) {
        // Move entire section
        auto& section = *sections[selectedSectionIndex];
        double sectionDuration = section.endTime - section.startTime;
        double newStartTime = juce::jmax(0.0, pixelToTime(event.x));
        double newEndTime = juce::jmin(timelineLength, newStartTime + sectionDuration);
        
        section.startTime = newStartTime;
        section.endTime = newEndTime;
        
        if (onSectionChanged) {
            onSectionChanged(selectedSectionIndex, section);
        }
        repaint();
    } else if (!arrangementLocked && isDraggingEdge && selectedSectionIndex >= 0) {
        // Resize section
        auto& section = *sections[selectedSectionIndex];
        double newTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));
        
        if (isDraggingStart) {
            section.startTime = juce::jmin(newTime, section.endTime - 1.0); // Minimum 1 second
        } else {
            section.endTime = juce::jmax(newTime, section.startTime + 1.0); // Minimum 1 second
        }
        
        if (onSectionChanged) {
            onSectionChanged(selectedSectionIndex, section);
        }
        repaint();
    } else {
        // Handle playhead dragging (default behavior)
        double dragTime = pixelToTime(event.x);
        setPlayheadPosition(dragTime);
        
        // Notify parent of position change
        if (onPlayheadPositionChanged) {
            onPlayheadPositionChanged(dragTime);
        }
    }
}

void TimelineComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    if (!arrangementLocked) {
        int sectionIndex = findSectionAtPosition(event.x, event.y);
        if (sectionIndex >= 0) {
            // Edit section name (simplified - in real app would show text editor)
            auto& section = *sections[sectionIndex];
            juce::String newName = "Section " + juce::String(sectionIndex + 1);
            section.name = newName;
            
            if (onSectionChanged) {
                onSectionChanged(sectionIndex, section);
            }
            repaint();
        }
    }
}

void TimelineComponent::addSection(const juce::String& name, double startTime, double endTime, juce::Colour colour) {
    sections.push_back(std::make_unique<ArrangementSection>(startTime, endTime, name, colour));
    repaint();
}

void TimelineComponent::removeSection(int index) {
    if (index >= 0 && index < sections.size()) {
        sections.erase(sections.begin() + index);
        if (selectedSectionIndex == index) {
            selectedSectionIndex = -1;
        } else if (selectedSectionIndex > index) {
            selectedSectionIndex--;
        }
        repaint();
    }
}

void TimelineComponent::clearSections() {
    sections.clear();
    selectedSectionIndex = -1;
    repaint();
}

double TimelineComponent::pixelToTime(int pixel) const {
    if (zoom > 0) {
        return pixel / zoom;
    }
    return 0.0;
}

int TimelineComponent::timeToPixel(double time) const {
    return static_cast<int>(time * zoom);
}

void TimelineComponent::drawTimeMarkers(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    
    // Calculate appropriate marker spacing based on zoom
    // We want markers to be spaced at least 30 pixels apart
    const int minPixelSpacing = 30;
    int markerInterval = 1; // Start with 1 second intervals
    
    // Adjust interval if markers would be too close
    while (timeToPixel(markerInterval) < minPixelSpacing && markerInterval < 60) {
        markerInterval *= (markerInterval < 10) ? 2 : 5; // 1,2,5,10,20,50...
    }
    
    // Draw time markers
    for (int i = 0; i <= timelineLength; i += markerInterval) {
        int x = timeToPixel(i);
        if (x >= 0 && x < getWidth()) {
            // Draw tick mark at bottom
            g.drawLine(x, getHeight() - 10, x, getHeight() - 2);
            
            // Draw time label at bottom to avoid overlap with arrangement sections
            int minutes = i / 60;
            int seconds = i % 60;
            juce::String timeStr = juce::String::formatted("%d:%02d", minutes, seconds);
            g.drawText(timeStr, x - 20, getHeight() - 25, 40, 20, juce::Justification::centred);
        }
    }
}

void TimelineComponent::drawPlayhead(juce::Graphics& g) {
    int playheadX = timeToPixel(playheadPosition);
    if (playheadX >= 0 && playheadX < getWidth()) {
        // Draw shadow for better visibility
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawLine(playheadX + 1, 0, playheadX + 1, getHeight(), 5.0f);
        // Draw main playhead line
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.drawLine(playheadX, 0, playheadX, getHeight(), 4.0f);
    }
}

void TimelineComponent::drawArrangementSections(juce::Graphics& g) {
    for (size_t i = 0; i < sections.size(); ++i) {
        drawSection(g, *sections[i], static_cast<int>(i) == selectedSectionIndex);
    }
}

void TimelineComponent::drawSection(juce::Graphics& g, const ArrangementSection& section, bool isSelected) const {
    int startX = timeToPixel(section.startTime);
    int endX = timeToPixel(section.endTime);
    int width = endX - startX;
    
    if (width <= 0 || startX >= getWidth() || endX <= 0) {
        return;
    }
    
    // Clip to visible area
    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);
    width = endX - startX;
    
    // Draw section background (upper half of timeline)
    auto sectionArea = juce::Rectangle<int>(startX, 0, width, getHeight() / 2);
    
    // Section background - dimmed if locked
    float alpha = arrangementLocked ? 0.2f : 0.3f;
    g.setColour(section.colour.withAlpha(alpha));
    g.fillRect(sectionArea);
    
    // Section border - different style if locked
    if (arrangementLocked) {
        g.setColour(section.colour.withAlpha(0.5f));
        // Draw dotted border to indicate locked state
        const float dashLengths[] = {2.0f, 2.0f};
        g.drawDashedLine(juce::Line<float>(startX, 0, startX, getHeight() / 2), 
                        dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(endX, 0, endX, getHeight() / 2), 
                        dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(startX, 0, endX, 0), 
                        dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(startX, getHeight() / 2, endX, getHeight() / 2), 
                        dashLengths, 2, 1.0f);
    } else {
        g.setColour(isSelected ? section.colour.brighter(0.5f) : section.colour);
        g.drawRect(sectionArea, isSelected ? 2 : 1);
    }
    
    // Section name
    if (width > 40) { // Only draw text if there's enough space
        g.setColour(arrangementLocked ? 
                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY) : 
                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        
        // Add lock indicator to name if locked
        juce::String displayName = arrangementLocked ? "🔒 " + section.name : section.name;
        g.drawText(displayName, sectionArea.reduced(2), juce::Justification::centred, true);
    }
}

int TimelineComponent::findSectionAtPosition(int x, int y) const {
    // Only check upper half of timeline where sections are drawn
    if (y > getHeight() / 2) {
        return -1;
    }
    
    double time = pixelToTime(x);
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& section = *sections[i];
        if (time >= section.startTime && time <= section.endTime) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool TimelineComponent::isOnSectionEdge(int x, int sectionIndex, bool& isStartEdge) const {
    if (sectionIndex < 0 || sectionIndex >= sections.size()) {
        return false;
    }
    
    const auto& section = *sections[sectionIndex];
    int startX = timeToPixel(section.startTime);
    int endX = timeToPixel(section.endTime);
    
    const int edgeThreshold = 5; // 5 pixels from edge
    
    if (std::abs(x - startX) <= edgeThreshold) {
        isStartEdge = true;
        return true;
    } else if (std::abs(x - endX) <= edgeThreshold) {
        isStartEdge = false;
        return true;
    }
    
    return false;
}

juce::String TimelineComponent::getDefaultSectionName() const {
    return "Section " + juce::String(sections.size() + 1);
}

} // namespace magica 