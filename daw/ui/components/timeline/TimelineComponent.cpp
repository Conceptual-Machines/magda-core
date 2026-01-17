#include "TimelineComponent.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "Config.hpp"

namespace magica {

TimelineComponent::TimelineComponent() {
    // Load configuration
    auto& config = magica::Config::getInstance();
    timelineLength = config.getDefaultTimelineLength();

    setMouseCursor(juce::MouseCursor::NormalCursor);
    setWantsKeyboardFocus(false);
    setSize(800, 40);

    // Create some sample arrangement sections with eye-catching colors
    addSection("Intro", 0, 8, juce::Colour(0xff00ff80));      // Bright lime green
    addSection("Verse 1", 8, 24, juce::Colour(0xff4080ff));   // Electric blue
    addSection("Chorus", 24, 40, juce::Colour(0xffff6600));   // Vivid orange
    addSection("Verse 2", 40, 56, juce::Colour(0xff8040ff));  // Bright purple
    addSection("Bridge", 56, 72, juce::Colour(0xffff0080));   // Hot pink
    addSection("Outro", 72, 88, juce::Colour(0xffff4040));    // Bright red

    // Lock arrangement sections by default to prevent accidental movement
    arrangementLocked = true;
}

TimelineComponent::~TimelineComponent() = default;

void TimelineComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND));

    // Get layout configuration
    auto& layout = LayoutConfig::getInstance();
    int arrangementHeight = layout.arrangementBarHeight;
    int timeRulerHeight = layout.timeRulerHeight;

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Show visual feedback when actively zooming
    if (isZooming) {
        // Slightly brighten the background when zooming
        g.setColour(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND).brighter(0.1f));
        g.fillRect(getLocalBounds().reduced(1));
    }

    // Draw arrangement sections first (in top section)
    drawArrangementSections(g);

    // Draw time markers (in time ruler section)
    drawTimeMarkers(g);

    // Draw separator line between arrangement and time ruler
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
    g.drawLine(0, static_cast<float>(arrangementHeight), static_cast<float>(getWidth()),
               static_cast<float>(arrangementHeight), 1.0f);

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

void TimelineComponent::setViewportWidth(int width) {
    viewportWidth = width;
}

void TimelineComponent::setTimeDisplayMode(TimeDisplayMode mode) {
    displayMode = mode;
    repaint();
}

void TimelineComponent::setTempo(double bpm) {
    tempoBPM = juce::jlimit(20.0, 999.0, bpm);
    repaint();
}

void TimelineComponent::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = juce::jlimit(1, 16, numerator);
    timeSignatureDenominator = juce::jlimit(1, 16, denominator);
    repaint();
}

double TimelineComponent::timeToBars(double timeInSeconds) const {
    // Calculate beats per second
    double beatsPerSecond = tempoBPM / 60.0;
    // Calculate total beats
    double totalBeats = timeInSeconds * beatsPerSecond;
    // Convert to bars (considering time signature)
    double bars = totalBeats / timeSignatureNumerator;
    return bars;
}

double TimelineComponent::barsToTime(double bars) const {
    // Convert bars to beats
    double totalBeats = bars * timeSignatureNumerator;
    // Calculate seconds per beat
    double secondsPerBeat = 60.0 / tempoBPM;
    return totalBeats * secondsPerBeat;
}

juce::String TimelineComponent::formatTimePosition(double timeInSeconds) const {
    if (displayMode == TimeDisplayMode::Seconds) {
        // Format as seconds with appropriate precision
        if (timeInSeconds < 10.0) {
            return juce::String(timeInSeconds, 1) + "s";
        } else if (timeInSeconds < 60.0) {
            return juce::String(timeInSeconds, 0) + "s";
        } else {
            int minutes = static_cast<int>(timeInSeconds) / 60;
            int seconds = static_cast<int>(timeInSeconds) % 60;
            return juce::String(minutes) + ":" + juce::String(seconds).paddedLeft('0', 2);
        }
    } else {
        // Format as bar.beat.subdivision (1-indexed)
        double beatsPerSecond = tempoBPM / 60.0;
        double totalBeats = timeInSeconds * beatsPerSecond;

        int bar = static_cast<int>(totalBeats / timeSignatureNumerator) + 1;
        int beatInBar = static_cast<int>(std::fmod(totalBeats, timeSignatureNumerator)) + 1;

        // Subdivision (16th notes within the beat)
        double beatFraction = std::fmod(totalBeats, 1.0);
        int subdivision = static_cast<int>(beatFraction * 4) + 1;  // 1-4 for 16th notes

        return juce::String(bar) + "." + juce::String(beatInBar) + "." + juce::String(subdivision);
    }
}

void TimelineComponent::mouseDown(const juce::MouseEvent& event) {
    // Store initial mouse position for drag detection
    mouseDownX = event.x;
    mouseDownY = event.y;
    zoomStartValue = zoom;
    isZooming = false;
    isPendingPlayheadClick = false;

    // Get layout configuration for zone calculations
    auto& layout = LayoutConfig::getInstance();
    int arrangementHeight = layout.arrangementBarHeight;
    int timeRulerEnd = arrangementHeight + layout.timeRulerHeight;

    // Define zones based on LayoutConfig
    bool inSectionsArea = event.y <= arrangementHeight;
    bool inTimeRulerArea = event.y > arrangementHeight && event.y <= timeRulerEnd;

    // Zone 1: Time ruler area - prepare for click (playhead) or drag (zoom)
    // Don't set playhead yet - wait for mouseUp to distinguish click from drag
    if (inTimeRulerArea) {
        isPendingPlayheadClick = true;
        return;
    }

    // Zone 2: Sections area handling (arrangement bar)
    if (!arrangementLocked && inSectionsArea) {
        int sectionIndex = findSectionAtPosition(event.x, event.y);

        if (sectionIndex >= 0) {
            selectedSectionIndex = sectionIndex;

            // Check if clicking on section edge for resizing
            bool isStartEdge;
            if (isOnSectionEdge(event.x, sectionIndex, isStartEdge)) {
                isDraggingEdge = true;
                isDraggingStart = isStartEdge;
                repaint();
                return;
            } else {
                isDraggingSection = true;
                repaint();
                return;
            }
        }
        // If no section found, fall through to allow zoom
    }

    // Zone 3: Empty area - prepare for zoom dragging
}

void TimelineComponent::mouseMove(const juce::MouseEvent& event) {
    // Update cursor based on zone
    auto& layout = LayoutConfig::getInstance();
    int arrangementHeight = layout.arrangementBarHeight;

    if (event.y <= arrangementHeight && !arrangementLocked) {
        // In arrangement area - show resize cursor if near section edge
        int sectionIndex = findSectionAtPosition(event.x, event.y);
        if (sectionIndex >= 0) {
            bool isStartEdge;
            if (isOnSectionEdge(event.x, sectionIndex, isStartEdge)) {
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
    } else {
        // In time ruler area - show magnifying glass for zoom
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& event) {
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

    // Check for vertical movement to start zoom mode
    int deltaY = std::abs(event.y - mouseDownY);

    if (deltaY > DRAG_THRESHOLD) {
        // Vertical drag detected - this is a zoom operation
        if (!isZooming) {
            std::cout << "🎯 STARTING ZOOM MODE (vertical drag detected)" << std::endl;
            isZooming = true;
            isPendingPlayheadClick = false;  // Cancel any pending playhead click
            // Capture the time position under the mouse at zoom start (using initial zoom level)
            zoomAnchorTime = pixelToTime(mouseDownX);
            zoomAnchorTime = juce::jlimit(0.0, timelineLength, zoomAnchorTime);
            // Capture the screen X position where the mouse is (relative to this component)
            zoomAnchorScreenX = mouseDownX;
            std::cout << "🎯 ZOOM ANCHOR: time=" << zoomAnchorTime
                      << "s, screenX=" << zoomAnchorScreenX << std::endl;
            repaint();
        }

        // Zoom calculation - drag up = zoom in, drag down = zoom out
        // Use exponential scaling for smooth, fluid zoom
        int deltaY = mouseDownY - event.y;

        // Check for modifier keys for accelerated zoom
        bool isShiftHeld = event.mods.isShiftDown();

        // Sensitivity: pixels of drag to double/halve zoom (higher = less sensitive)
        double sensitivity = 150.0;  // Base: 150px drag to double zoom
        if (isShiftHeld) {
            sensitivity = 50.0;  // Shift: faster zoom
        }

        // Exponential zoom: drag up doubles, drag down halves
        // This feels natural because zoom is multiplicative
        double exponent = static_cast<double>(deltaY) / sensitivity;
        double newZoom = zoomStartValue * std::pow(2.0, exponent);

        // Calculate minimum zoom based on timeline length and viewport width
        auto& config = magica::Config::getInstance();
        double minZoom = config.getMinZoomLevel();
        if (timelineLength > 0 && viewportWidth > 0) {
            double availableWidth = viewportWidth - 50.0;
            minZoom = availableWidth / timelineLength;
            minZoom = juce::jmax(minZoom, config.getMinZoomLevel());
        }

        double maxZoom = config.getMaxZoomLevel();

        // Apply limits
        if (std::isnan(newZoom) || newZoom < minZoom) {
            newZoom = minZoom;
        } else if (newZoom > maxZoom) {
            newZoom = maxZoom;
        }

        // Call the callback with zoom value, anchor time, and screen position
        if (onZoomChanged) {
            onZoomChanged(newZoom, zoomAnchorTime, zoomAnchorScreenX);
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

void TimelineComponent::mouseUp(const juce::MouseEvent& event) {
    // Reset all dragging states
    isDraggingSection = false;
    isDraggingEdge = false;
    isDraggingStart = false;

    // End zoom operation
    if (isZooming && onZoomEnd) {
        onZoomEnd();
    }

    // Handle pending playhead click - if we didn't zoom, set the playhead
    if (isPendingPlayheadClick && !isZooming) {
        // Check if we haven't moved much (it's a click, not a drag)
        int deltaX = std::abs(event.x - mouseDownX);
        int deltaY = std::abs(event.y - mouseDownY);

        if (deltaX <= DRAG_THRESHOLD && deltaY <= DRAG_THRESHOLD) {
            // It was a click - set playhead position
            double clickTime = pixelToTime(mouseDownX);
            clickTime = juce::jlimit(0.0, timelineLength, clickTime);
            setPlayheadPosition(clickTime);

            if (onPlayheadPositionChanged) {
                onPlayheadPositionChanged(clickTime);
            }
        }
    }

    isPendingPlayheadClick = false;
    isZooming = false;

    repaint();
}

void TimelineComponent::addSection(const juce::String& name, double startTime, double endTime,
                                   juce::Colour colour) {
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

int TimelineComponent::timeDurationToPixels(double duration) const {
    return static_cast<int>(duration * zoom);
}

void TimelineComponent::drawTimeMarkers(juce::Graphics& g) {
    // Get layout configuration
    auto& layout = LayoutConfig::getInstance();
    int arrangementHeight = layout.arrangementBarHeight;
    int timeRulerHeight = layout.timeRulerHeight;

    // Time ruler area starts after arrangement bar
    int rulerTop = arrangementHeight;
    int rulerBottom = arrangementHeight + timeRulerHeight;

    // Tick and label sizing from config
    int majorTickHeight = layout.rulerMajorTickHeight;
    int minorTickHeight = layout.rulerMinorTickHeight;
    int labelFontSize = layout.rulerLabelFontSize;
    int labelY = rulerTop + layout.rulerLabelTopMargin;
    int labelHeight = timeRulerHeight - majorTickHeight - layout.rulerLabelTopMargin - 2;

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(static_cast<float>(labelFontSize)));

    const int minPixelSpacing = 50;

    if (displayMode == TimeDisplayMode::Seconds) {
        // ===== SECONDS MODE =====
        const double intervals[] = {0.001, 0.005, 0.01, 0.05, 0.1,  0.25, 0.5,
                                    1.0,   2.0,   5.0,  10.0, 30.0, 60.0};

        double markerInterval = 1.0;
        for (double interval : intervals) {
            if (timeDurationToPixels(interval) >= minPixelSpacing) {
                markerInterval = interval;
                break;
            }
        }

        for (double time = 0.0; time <= timelineLength; time += markerInterval) {
            int x = timeToPixel(time) + LEFT_PADDING;
            if (x >= 0 && x < getWidth()) {
                bool isMajor =
                    (markerInterval >= 1.0) || (std::fmod(time, markerInterval * 5) < 0.0001);
                int tickHeight = isMajor ? majorTickHeight : minorTickHeight;

                g.setColour(DarkTheme::getColour(isMajor ? DarkTheme::TEXT_SECONDARY
                                                         : DarkTheme::TEXT_DIM));
                g.drawVerticalLine(x, static_cast<float>(rulerBottom - tickHeight),
                                   static_cast<float>(rulerBottom));

                if (isMajor) {
                    juce::String timeStr;
                    if (markerInterval >= 1.0) {
                        int minutes = static_cast<int>(time) / 60;
                        int seconds = static_cast<int>(time) % 60;
                        timeStr = juce::String::formatted("%d:%02d", minutes, seconds);
                    } else {
                        timeStr = juce::String(time, 1) + "s";
                    }

                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.drawText(timeStr, x - 35, labelY, 70, labelHeight,
                               juce::Justification::centred);
                }
            }
        }
    } else {
        // ===== BARS/BEATS MODE =====
        // Calculate beat duration in seconds
        double secondsPerBeat = 60.0 / tempoBPM;
        double secondsPerBar = secondsPerBeat * timeSignatureNumerator;

        // Define musical intervals: 16th, 8th, quarter, half, bar, 2 bars, 4 bars, 8 bars
        const double beatFractions[] = {0.25, 0.5, 1.0, 2.0};
        const int barMultiples[] = {1, 2, 4, 8, 16, 32};

        // Find appropriate interval
        double markerIntervalBeats = 1.0;  // Default to quarter note
        bool useBarMultiples = false;

        // First try beat subdivisions
        for (double fraction : beatFractions) {
            double intervalSeconds = secondsPerBeat * fraction;
            if (timeDurationToPixels(intervalSeconds) >= minPixelSpacing) {
                markerIntervalBeats = fraction;
                break;
            }
            if (fraction == 2.0) {
                useBarMultiples = true;
            }
        }

        // If beats are too dense, use bar multiples
        if (useBarMultiples || timeDurationToPixels(secondsPerBar) < minPixelSpacing) {
            for (int mult : barMultiples) {
                double intervalSeconds = secondsPerBar * mult;
                if (timeDurationToPixels(intervalSeconds) >= minPixelSpacing) {
                    markerIntervalBeats = timeSignatureNumerator * mult;
                    break;
                }
            }
        }

        double markerIntervalSeconds = secondsPerBeat * markerIntervalBeats;

        // Draw markers
        for (double time = 0.0; time <= timelineLength; time += markerIntervalSeconds) {
            int x = timeToPixel(time) + LEFT_PADDING;
            if (x >= 0 && x < getWidth()) {
                // Calculate bar and beat position
                double totalBeats = time / secondsPerBeat;
                int bar = static_cast<int>(totalBeats / timeSignatureNumerator) + 1;
                int beatInBar = static_cast<int>(std::fmod(totalBeats, timeSignatureNumerator)) + 1;

                // Major tick on bar boundaries, minor on beats
                bool isMajor = (beatInBar == 1);
                int tickHeight = isMajor ? majorTickHeight : minorTickHeight;

                g.setColour(DarkTheme::getColour(isMajor ? DarkTheme::TEXT_SECONDARY
                                                         : DarkTheme::TEXT_DIM));
                g.drawVerticalLine(x, static_cast<float>(rulerBottom - tickHeight),
                                   static_cast<float>(rulerBottom));

                // Label on major ticks (bar start)
                if (isMajor) {
                    juce::String barStr = juce::String(bar);

                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.drawText(barStr, x - 35, labelY, 70, labelHeight,
                               juce::Justification::centred);
                }
            }
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

void TimelineComponent::drawSection(juce::Graphics& g, const ArrangementSection& section,
                                    bool isSelected) const {
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

    // Draw section background using arrangement bar height from LayoutConfig
    int arrangementHeight = LayoutConfig::getInstance().arrangementBarHeight;
    auto sectionArea = juce::Rectangle<int>(startX, 0, width, arrangementHeight);

    // Section background - dimmed if locked
    float alpha = arrangementLocked ? 0.2f : 0.3f;
    g.setColour(section.colour.withAlpha(alpha));
    g.fillRect(sectionArea);

    // Section border - different style if locked
    if (arrangementLocked) {
        g.setColour(section.colour.withAlpha(0.5f));
        // Draw dotted border to indicate locked state
        const float dashLengths[] = {2.0f, 2.0f};
        g.drawDashedLine(juce::Line<float>(startX, 0, startX, sectionArea.getBottom()), dashLengths,
                         2, 1.0f);
        g.drawDashedLine(juce::Line<float>(endX, 0, endX, sectionArea.getBottom()), dashLengths, 2,
                         1.0f);
        g.drawDashedLine(juce::Line<float>(startX, 0, endX, 0), dashLengths, 2, 1.0f);
        g.drawDashedLine(
            juce::Line<float>(startX, sectionArea.getBottom(), endX, sectionArea.getBottom()),
            dashLengths, 2, 1.0f);
    } else {
        g.setColour(isSelected ? section.colour.brighter(0.5f) : section.colour);
        g.drawRect(sectionArea, isSelected ? 2 : 1);
    }

    // Section name
    if (width > 40) {  // Only draw text if there's enough space
        g.setColour(arrangementLocked ? DarkTheme::getColour(DarkTheme::TEXT_SECONDARY)
                                      : DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));

        // Draw section name without lock symbol (lock will be shown elsewhere)
        g.drawText(section.name, sectionArea.reduced(2), juce::Justification::centred, true);
    }
}

int TimelineComponent::findSectionAtPosition(int x, int y) const {
    // Check the arrangement section area using LayoutConfig
    int arrangementHeight = LayoutConfig::getInstance().arrangementBarHeight;
    if (y > arrangementHeight) {
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

    const int edgeThreshold = 5;  // 5 pixels from edge

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

}  // namespace magica
