#include "TimelineComponent.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include <iostream>

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
    
    // Show visual feedback when actively zooming
    if (isZooming) {
        // Slightly brighten the background when zooming
        g.setColour(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND).brighter(0.1f));
        g.fillRect(getLocalBounds().reduced(1));
    }
    
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
    playheadPosition = juce::jlimit(0.0, timelineLength, position);
    // Don't repaint - timeline doesn't draw playhead anymore
}

void TimelineComponent::setZoom(double pixelsPerSecond) {
    zoom = pixelsPerSecond;
    repaint();
}

void TimelineComponent::mouseDown(const juce::MouseEvent& event) {
    // Store initial mouse position for drag detection
    zoomStartY = event.y;
    zoomStartValue = zoom;
    
    // FIRST: Check if mouseDown is even reaching us
    std::cout << "🎯 MOUSE DOWN REACHED TIMELINE COMPONENT! Y=" << event.y << " X=" << event.x << std::endl;
    std::cout << "🎯 Timeline bounds: width=" << getWidth() << " height=" << getHeight() << std::endl;
    std::cout << "🎯 Timeline position: x=" << getX() << " y=" << getY() << std::endl;
    
    // Check if we're in the critical tick area
    if (event.y >= 65) {
        std::cout << "🎯 *** CLICK IN TICK AREA! Y=" << event.y << " ***" << std::endl;
    }
    
    // Define zones based on actual drawing layout with expanded zoom zone
    int sectionsHeight = static_cast<int>(getHeight() * 0.4);  // Top 40% - sections area
    int playheadZoneStart = getHeight() - 25;  // Bottom 25 pixels for numbers + ticks (reduced from 40)
    
    bool inPlayheadZone = event.y >= playheadZoneStart;
    bool inSectionsArea = event.y <= sectionsHeight;
    bool inZoomZone = event.y > sectionsHeight && event.y < playheadZoneStart;
    
    // DEBUG: Use multiple output methods
    DBG("=== TIMELINE MOUSE DOWN DEBUG ===");
    DBG("Mouse Y: " << event.y);
    DBG("Timeline Height: " << getHeight());
    DBG("Sections Height: " << sectionsHeight);
    DBG("Playhead Zone Start: " << playheadZoneStart);
    DBG("In Sections Area: " << (inSectionsArea ? "YES" : "NO"));
    DBG("In Zoom Zone: " << (inZoomZone ? "YES" : "NO"));
    DBG("In Playhead Zone: " << (inPlayheadZone ? "YES" : "NO"));
    DBG("Arrangement Locked: " << (arrangementLocked ? "YES" : "NO"));
    
    // Also output to console
    std::cout << "=== TIMELINE MOUSE DOWN DEBUG ===" << std::endl;
    std::cout << "Mouse Y: " << event.y << std::endl;
    std::cout << "Timeline Height: " << getHeight() << std::endl;
    std::cout << "Sections (0-" << sectionsHeight << "), Zoom (" << sectionsHeight << "-" << playheadZoneStart << "), Playhead (" << playheadZoneStart << "-" << getHeight() << ")" << std::endl;
    std::cout << "In Sections: " << (inSectionsArea ? "YES" : "NO") << ", In Zoom: " << (inZoomZone ? "YES" : "NO") << ", In Playhead: " << (inPlayheadZone ? "YES" : "NO") << std::endl;
    std::cout << "Numbers area: " << (getHeight() - 35) << "-" << (getHeight() - 15) << ", Ticks area: " << (getHeight() - 15) << "-" << (getHeight() - 2) << std::endl;
    
    // Zone 1: Playhead zone (bottom area with ticks and numbers)
    if (inPlayheadZone) {
        DBG("ENTERING PLAYHEAD ZONE LOGIC");
        std::cout << "🎯 *** PLAYHEAD ZONE TRIGGERED *** Y=" << event.y << " playheadStart=" << playheadZoneStart << std::endl;
        
        double clickTime = pixelToTime(event.x);
        clickTime = juce::jlimit(0.0, timelineLength, clickTime);
        
        DBG("Setting playhead to time: " << clickTime);
        std::cout << "Setting playhead to time: " << clickTime << std::endl;
        setPlayheadPosition(clickTime);
        
        if (onPlayheadPositionChanged) {
            DBG("Calling onPlayheadPositionChanged callback");
            std::cout << "Calling onPlayheadPositionChanged callback" << std::endl;
            onPlayheadPositionChanged(clickTime);
        }
        
        DBG("RETURNING FROM PLAYHEAD ZONE");
        std::cout << "RETURNING FROM PLAYHEAD ZONE" << std::endl;
        return; // No dragging from playhead zone
    }
    
    // Zone 2: Sections area handling (when unlocked)
    if (!arrangementLocked && inSectionsArea) {
        DBG("CHECKING SECTIONS AREA");
        std::cout << "CHECKING SECTIONS AREA" << std::endl;
        
        int sectionIndex = findSectionAtPosition(event.x, event.y);
        DBG("Section index found: " << sectionIndex);
        std::cout << "Section index found: " << sectionIndex << std::endl;
        
        if (sectionIndex >= 0) {
            DBG("ENTERING SECTION EDITING LOGIC");
            std::cout << "ENTERING SECTION EDITING LOGIC" << std::endl;
            selectedSectionIndex = sectionIndex;
            
            // Check if clicking on section edge for resizing
            bool isStartEdge;
            if (isOnSectionEdge(event.x, sectionIndex, isStartEdge)) {
                isDraggingEdge = true;
                isDraggingStart = isStartEdge;
                DBG("Starting edge drag");
                std::cout << "Starting edge drag" << std::endl;
            } else {
                isDraggingSection = true;
                DBG("Starting section drag");
                std::cout << "Starting section drag" << std::endl;
            }
            repaint();
            return;
        }
    }
    
    // Zone 3: Zoom zone (middle area between sections and playhead zone)
    if (inZoomZone) {
        DBG("ENTERING ZOOM ZONE LOGIC");
        std::cout << "🎯 *** ZOOM ZONE TRIGGERED *** Y=" << event.y << " (zone: " << sectionsHeight << "-" << playheadZoneStart << ")" << std::endl;
        std::cout << "Prepared for zoom dragging" << std::endl;
        return; // Ready for zoom dragging in mouseDrag
    }
    
    // Fallback: if not in any specific zone, still allow zoom
    DBG("NOT IN ANY SPECIFIC ZONE - ALLOWING ZOOM");
    std::cout << "🎯 *** FALLBACK ZONE *** Y=" << event.y << " - zones: sections=" << inSectionsArea << " zoom=" << inZoomZone << " playhead=" << inPlayheadZone << std::endl;
}

void TimelineComponent::mouseMove(const juce::MouseEvent& event) {
    // Check if we're getting mouse events in the tick area
    if (event.y >= getHeight() - 20) {  // Bottom 20 pixels (tick area)
        std::cout << "🎯 MOUSE MOVE IN TICK AREA: Y=" << event.y << " X=" << event.x << std::endl;
    }
    
    // Debug all mouse moves to see the range we're getting
    static int lastY = -1;
    if (std::abs(event.y - lastY) > 2) {  // Only log when Y changes significantly
        std::cout << "🎯 MOUSE MOVE: Y=" << event.y << " (max should be " << (getHeight() - 1) << ")" << std::endl;
        lastY = event.y;
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& event) {
    std::cout << "🎯 MOUSE DRAG: Y=" << event.y << " startY=" << zoomStartY << std::endl;
    
    // Handle section dragging first
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
        return;
    }
    
    if (!arrangementLocked && isDraggingEdge && selectedSectionIndex >= 0) {
        // Resize section
        auto& section = *sections[selectedSectionIndex];
        double newTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));
        
        if (isDraggingStart) {
            section.startTime = juce::jmin(newTime, section.endTime - 1.0);
        } else {
            section.endTime = juce::jmax(newTime, section.startTime + 1.0);
        }
        
        if (onSectionChanged) {
            onSectionChanged(selectedSectionIndex, section);
        }
        repaint();
        return;
    }
    
    // Only allow zoom dragging if we didn't start in the playhead zone
    int playheadZoneStart = getHeight() - 25;
    bool startedInPlayheadZone = zoomStartY >= playheadZoneStart;
    
    int sectionsHeight = static_cast<int>(getHeight() * 0.4);
    bool startedInSections = zoomStartY <= sectionsHeight;
    bool startedInZoomZone = zoomStartY > sectionsHeight && zoomStartY < playheadZoneStart;
    
    std::cout << "🎯 ZOOM ANALYSIS: startY=" << zoomStartY << " playheadStart=" << playheadZoneStart << " sectionsEnd=" << sectionsHeight << std::endl;
    std::cout << "🎯 ZONES: inPlayhead=" << (startedInPlayheadZone ? "YES" : "NO") << " inSections=" << (startedInSections ? "YES" : "NO") << " inZoom=" << (startedInZoomZone ? "YES" : "NO") << std::endl;
    
    if (!startedInPlayheadZone) {
        std::cout << "🎯 ZOOM DRAG: startedInPlayheadZone=false, deltaY=" << std::abs(event.y - zoomStartY) << std::endl;
        
        // Check for vertical movement to start zoom mode
        int deltaY = std::abs(event.y - zoomStartY);
        if (deltaY > 3) {
            if (!isZooming) {
                // Start zoom mode
                std::cout << "🎯 STARTING ZOOM MODE" << std::endl;
                isZooming = true;
                setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
                repaint();
            }
            
            // Simple zoom calculation - drag up = zoom in, drag down = zoom out
            int actualDeltaY = zoomStartY - event.y;
            double sensitivity = 60.0; // 60 pixels = 2x zoom
            double zoomFactor = 1.0 + (actualDeltaY / sensitivity);
            double newZoom = juce::jlimit(0.1, 100000.0, zoomStartValue * zoomFactor);
            
            std::cout << "🎯 ZOOM: factor=" << zoomFactor << ", newZoom=" << newZoom << std::endl;
            
            setZoom(newZoom);
            if (onZoomChanged) {
                onZoomChanged(newZoom);
            }
        }
    } else {
        std::cout << "🎯 ZOOM DRAG BLOCKED: startedInPlayheadZone=true" << std::endl;
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

void TimelineComponent::mouseUp(const juce::MouseEvent& /*event*/) {
    // Reset all dragging states
    isDraggingSection = false;
    isDraggingEdge = false;
    isDraggingStart = false;
    isZooming = false;
    
    // Reset cursor
    setMouseCursor(juce::MouseCursor::NormalCursor);
    
    repaint();
}

void TimelineComponent::addSection(const juce::String& name, double startTime, double endTime, juce::Colour colour) {
    sections.push_back(std::make_unique<ArrangementSection>(startTime, endTime, name, colour));
    repaint();
}

void TimelineComponent::removeSection(int index) {
    if (index >= 0 && index < static_cast<int>(sections.size())) {
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
        return (pixel - LEFT_PADDING) / zoom;
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
    
    // Define marker intervals in seconds (including sub-second intervals)
    const double intervals[] = {
        0.001,    // 1ms (sample level at 44.1kHz ≈ 0.023ms)
        0.005,    // 5ms
        0.01,     // 10ms
        0.05,     // 50ms
        0.1,      // 100ms
        0.25,     // 250ms
        0.5,      // 500ms
        1.0,      // 1 second
        2.0,      // 2 seconds
        5.0,      // 5 seconds
        10.0,     // 10 seconds
        30.0,     // 30 seconds
        60.0      // 1 minute
    };
    
    // Find the appropriate interval
    double markerInterval = 1.0; // Default to 1 second
    for (double interval : intervals) {
        if (timeToPixel(interval) >= minPixelSpacing) {
            markerInterval = interval;
            break;
        }
    }
    
    // If even the finest interval is too wide, use sample-level precision
    if (markerInterval == 0.001 && timeToPixel(0.001) > minPixelSpacing * 2) {
        // At very high zoom, show sample markers (assuming 44.1kHz)
        double sampleInterval = 1.0 / 44100.0; // ~0.0000227 seconds per sample
        int sampleStep = 1;
        while (timeToPixel(sampleStep * sampleInterval) < minPixelSpacing) {
            sampleStep *= 10; // 1, 10, 100, 1000 samples
        }
        markerInterval = sampleStep * sampleInterval;
    }
    
    // Calculate start position (align to interval boundaries)
    double startTime = 0.0;
    if (markerInterval >= 1.0) {
        startTime = std::floor(0.0 / markerInterval) * markerInterval;
    } else {
        startTime = std::floor(0.0 / markerInterval) * markerInterval;
    }
    
    // Draw time markers
    for (double time = startTime; time <= timelineLength; time += markerInterval) {
        int x = timeToPixel(time) + LEFT_PADDING;
        if (x >= 0 && x < getWidth()) {
            // Draw short tick mark at bottom (back to original style)
            g.drawLine(x, getHeight() - 15, x, getHeight() - 2);
            
            // Format time label based on interval precision
            juce::String timeStr;
            if (markerInterval < 1.0) {
                // Sub-second precision
                if (markerInterval >= 0.1) {
                    timeStr = juce::String(time, 1) + "s";
                } else if (markerInterval >= 0.01) {
                    timeStr = juce::String(time, 2) + "s";
                } else if (markerInterval >= 0.001) {
                    timeStr = juce::String(time, 3) + "s";
                } else {
                    // Sample level - show as samples
                    int samples = static_cast<int>(time * 44100.0);
                    timeStr = juce::String(samples) + " smp";
                }
            } else {
                // Second precision and above
                int minutes = static_cast<int>(time) / 60;
                int seconds = static_cast<int>(time) % 60;
                timeStr = juce::String::formatted("%d:%02d", minutes, seconds);
            }
            
            // Draw time label with more padding from triangle (back to original style)
            g.drawText(timeStr, x - 30, getHeight() - 35, 60, 20, juce::Justification::centred);
        }
    }
}

void TimelineComponent::drawPlayhead(juce::Graphics& g) {
    int playheadX = timeToPixel(playheadPosition) + LEFT_PADDING;
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
    int startX = timeToPixel(section.startTime) + LEFT_PADDING;
    int endX = timeToPixel(section.endTime) + LEFT_PADDING;
    int width = endX - startX;
    
    if (width <= 0 || startX >= getWidth() || endX <= 0) {
        return;
    }
    
    // Clip to visible area
    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);
    width = endX - startX;
    
    // Draw section background - smaller size (40% of timeline height)
    auto sectionArea = juce::Rectangle<int>(startX, 0, width, static_cast<int>(getHeight() * 0.4));
    
    // Section background - dimmed if locked
    float alpha = arrangementLocked ? 0.2f : 0.3f;
    g.setColour(section.colour.withAlpha(alpha));
    g.fillRect(sectionArea);
    
    // Section border - different style if locked
    if (arrangementLocked) {
        g.setColour(section.colour.withAlpha(0.5f));
        // Draw dotted border to indicate locked state
        const float dashLengths[] = {2.0f, 2.0f};
        g.drawDashedLine(juce::Line<float>(startX, 0, startX, sectionArea.getBottom()), 
                        dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(endX, 0, endX, sectionArea.getBottom()), 
                        dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(startX, 0, endX, 0), 
                        dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(startX, sectionArea.getBottom(), endX, sectionArea.getBottom()), 
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
        
        // Draw section name without lock symbol (lock will be shown elsewhere)
        g.drawText(section.name, sectionArea.reduced(2), juce::Justification::centred, true);
    }
}

int TimelineComponent::findSectionAtPosition(int x, int y) const {
    // Check the arrangement section area (now 40% of timeline height)
    if (y > static_cast<int>(getHeight() * 0.4)) {
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
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections.size())) {
        return false;
    }
    
    const auto& section = *sections[sectionIndex];
    int startX = timeToPixel(section.startTime) + LEFT_PADDING;
    int endX = timeToPixel(section.endTime) + LEFT_PADDING;
    
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