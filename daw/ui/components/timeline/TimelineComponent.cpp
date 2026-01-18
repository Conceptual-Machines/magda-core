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

    // Arrangement sections are empty by default - can be added via addSection()
    arrangementLocked = true;
}

TimelineComponent::~TimelineComponent() = default;

void TimelineComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND));

    // Get layout configuration
    auto& layout = LayoutConfig::getInstance();
    int arrangementHeight = layout.arrangementBarHeight;

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Show visual feedback when actively zooming
    if (isZooming) {
        // Slightly brighten the background when zooming
        g.setColour(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND).brighter(0.1f));
        g.fillRect(getLocalBounds().reduced(1));
    }

    // Draw time selection first (background layer)
    drawTimeSelection(g);

    // Draw loop markers (background - shaded region behind time labels)
    drawLoopMarkers(g);

    // Draw arrangement sections (in top section)
    drawArrangementSections(g);

    // Draw time markers (in time ruler section) - ON TOP of loop region
    drawTimeMarkers(g);

    // Draw separator line between arrangement and time ruler
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
    g.drawLine(0, static_cast<float>(arrangementHeight), static_cast<float>(getWidth()),
               static_cast<float>(arrangementHeight), 1.0f);

    // Draw loop marker flags on top (triangular indicators)
    drawLoopMarkerFlags(g);

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
    isDraggingLoopStart = false;
    isDraggingLoopEnd = false;
    isDraggingTimeSelection = false;

    // Get layout configuration for zone calculations
    auto& layout = LayoutConfig::getInstance();
    int arrangementHeight = layout.arrangementBarHeight;
    int timeRulerHeight = layout.timeRulerHeight;
    int timeRulerEnd = arrangementHeight + timeRulerHeight;
    int rulerMidpoint = arrangementHeight + (timeRulerHeight / 2);

    // Define zones based on LayoutConfig
    bool inSectionsArea = event.y <= arrangementHeight;
    bool inTimeRulerArea = event.y > arrangementHeight && event.y <= timeRulerEnd;
    bool inTimeSelectionZone = event.y >= rulerMidpoint && event.y <= timeRulerEnd;

    // Check for loop marker dragging first - works in both arrangement and ruler areas
    bool isStartMarker;
    if (isOnLoopMarker(event.x, event.y, isStartMarker)) {
        if (isStartMarker) {
            isDraggingLoopStart = true;
        } else {
            isDraggingLoopEnd = true;
        }
        return;
    }

    // Zone 1a: Lower ruler area (near tick labels) - start time selection
    if (inTimeSelectionZone) {
        isDraggingTimeSelection = true;
        double startTime = pixelToTime(event.x);
        startTime = juce::jlimit(0.0, timelineLength, startTime);
        if (snapEnabled) {
            startTime = snapTimeToGrid(startTime);
        }
        timeSelectionDragStart = startTime;
        timeSelectionStart = startTime;
        timeSelectionEnd = startTime;
        repaint();
        return;
    }

    // Zone 1b: Upper ruler area - prepare for click (playhead) or drag (zoom)
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
    int timeRulerHeight = layout.timeRulerHeight;

    // Check for loop markers first - they span both arrangement and ruler areas
    bool isStartMarker;
    if (isOnLoopMarker(event.x, event.y, isStartMarker)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (event.y <= arrangementHeight) {
        // In arrangement area - check for section edges if not locked
        if (!arrangementLocked) {
            int sectionIndex = findSectionAtPosition(event.x, event.y);
            if (sectionIndex >= 0) {
                bool isStartEdge;
                if (isOnSectionEdge(event.x, sectionIndex, isStartEdge)) {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
            }
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
    } else {
        // In time ruler area - split into two zones
        // Upper half: zoom (crosshair), Lower half: time selection (I-beam)
        int rulerMidpoint = arrangementHeight + (timeRulerHeight / 2);

        if (event.y < rulerMidpoint) {
            // Upper ruler area - zoom cursor
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        } else {
            // Lower ruler area (near tick labels) - time selection cursor
            setMouseCursor(juce::MouseCursor::IBeamCursor);
        }
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& event) {
    // Handle time selection dragging first
    if (isDraggingTimeSelection) {
        double currentTime = pixelToTime(event.x);
        currentTime = juce::jlimit(0.0, timelineLength, currentTime);
        if (snapEnabled) {
            currentTime = snapTimeToGrid(currentTime);
        }

        // Update selection based on drag direction
        if (currentTime < timeSelectionDragStart) {
            timeSelectionStart = currentTime;
            timeSelectionEnd = timeSelectionDragStart;
        } else {
            timeSelectionStart = timeSelectionDragStart;
            timeSelectionEnd = currentTime;
        }

        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(timeSelectionStart, timeSelectionEnd);
        }
        repaint();
        return;
    }

    // Handle loop marker dragging
    if (isDraggingLoopStart || isDraggingLoopEnd) {
        double newTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));

        // Apply snap to grid if enabled
        if (snapEnabled) {
            newTime = snapTimeToGrid(newTime);
        }

        if (isDraggingLoopStart) {
            // Don't let start go past end (leave at least 0.01s)
            loopStartTime = juce::jmin(newTime, loopEndTime - 0.01);
        } else {
            // Don't let end go before start
            loopEndTime = juce::jmax(newTime, loopStartTime + 0.01);
        }

        if (onLoopRegionChanged) {
            onLoopRegionChanged(loopStartTime, loopEndTime);
        }
        repaint();
        return;
    }

    // Handle section dragging
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
    // Finalize time selection if we were dragging
    if (isDraggingTimeSelection) {
        // If selection is too small (just a click), clear it
        if (std::abs(timeSelectionEnd - timeSelectionStart) < 0.01) {
            timeSelectionStart = -1.0;
            timeSelectionEnd = -1.0;
            if (onTimeSelectionChanged) {
                onTimeSelectionChanged(-1.0, -1.0);
            }
        }
        isDraggingTimeSelection = false;
        repaint();
        return;
    }

    // Reset all dragging states
    isDraggingSection = false;
    isDraggingEdge = false;
    isDraggingStart = false;
    isDraggingLoopStart = false;
    isDraggingLoopEnd = false;

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

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel) {
    // Forward horizontal scroll to parent via callback
    // This allows scrolling when the mouse is over the timeline ruler
    if (onScrollRequested) {
        // Use deltaX for horizontal scroll (trackpad left/right)
        // Also allow vertical scroll to trigger horizontal scroll when shift is held
        float deltaX = wheel.deltaX;
        float deltaY = wheel.deltaY;

        // If there's horizontal movement, scroll horizontally
        if (std::abs(deltaX) > 0.0f || std::abs(deltaY) > 0.0f) {
            onScrollRequested(deltaX, deltaY);
        }
    }
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
        // Extended intervals for deep zoom (down to 100 microseconds)
        const double intervals[] = {
            0.0001, 0.0002, 0.0005,       // Sub-millisecond (100μs, 200μs, 500μs)
            0.001,  0.002,  0.005,        // Milliseconds (1ms, 2ms, 5ms)
            0.01,   0.02,   0.05,         // Centiseconds (10ms, 20ms, 50ms)
            0.1,    0.2,    0.25,   0.5,  // Deciseconds
            1.0,    2.0,    5.0,    10.0, 15.0, 30.0, 60.0};  // Seconds and minutes

        double markerInterval = 1.0;
        for (double interval : intervals) {
            if (timeDurationToPixels(interval) >= minPixelSpacing) {
                markerInterval = interval;
                break;
            }
        }

        // Draw ticks and labels
        for (double time = 0.0; time <= timelineLength; time += markerInterval) {
            int x = timeToPixel(time) + LEFT_PADDING;
            if (x >= 0 && x < getWidth()) {
                bool isMajor = false;
                if (markerInterval >= 1.0) {
                    isMajor = true;
                } else if (markerInterval >= 0.1) {
                    isMajor = std::fmod(time, 1.0) < 0.0001;
                } else if (markerInterval >= 0.01) {
                    isMajor = std::fmod(time, 0.1) < 0.0001;
                } else if (markerInterval >= 0.001) {
                    isMajor = std::fmod(time, 0.01) < 0.0001;
                } else {
                    isMajor = std::fmod(time, 0.001) < 0.00001;
                }

                // Skip tick if it's at a loop marker position (loop markers draw their own ticks)
                bool isAtLoopMarker = (loopStartTime >= 0 && loopEndTime > loopStartTime) &&
                                      (timeToPixel(time) == timeToPixel(loopStartTime) ||
                                       timeToPixel(time) == timeToPixel(loopEndTime));

                int tickHeight = isMajor ? majorTickHeight : minorTickHeight;

                // Draw tick (unless at loop marker position)
                if (!isAtLoopMarker) {
                    g.setColour(DarkTheme::getColour(isMajor ? DarkTheme::TEXT_SECONDARY
                                                             : DarkTheme::TEXT_DIM));
                    g.drawVerticalLine(x, static_cast<float>(rulerBottom - tickHeight),
                                       static_cast<float>(rulerBottom));
                }

                if (isMajor) {
                    juce::String timeStr;
                    if (time >= 60.0) {
                        // Minutes:seconds format
                        int minutes = static_cast<int>(time) / 60;
                        int seconds = static_cast<int>(time) % 60;
                        timeStr = juce::String::formatted("%d:%02d", minutes, seconds);
                    } else if (markerInterval >= 1.0 || time >= 1.0) {
                        // Seconds with appropriate precision
                        if (markerInterval >= 1.0) {
                            timeStr = juce::String(static_cast<int>(time)) + "s";
                        } else {
                            timeStr = juce::String(time, 1) + "s";
                        }
                    } else if (markerInterval >= 0.01 || time >= 0.01) {
                        // Milliseconds (show as Xms)
                        int ms = static_cast<int>(time * 1000);
                        timeStr = juce::String(ms) + "ms";
                    } else {
                        // Sub-millisecond (show as X.Xms or Xμs)
                        double ms = time * 1000.0;
                        if (ms >= 0.1) {
                            timeStr = juce::String(ms, 1) + "ms";
                        } else {
                            int us = static_cast<int>(time * 1000000);
                            timeStr = juce::String(us) + "μs";
                        }
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

        // Define musical intervals: 64th, 32nd, 16th, 8th, quarter, half, bar, multi-bars
        // Beat fractions: 0.0625 = 64th, 0.125 = 32nd, 0.25 = 16th, 0.5 = 8th, 1.0 = quarter
        const double beatFractions[] = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0};
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

        // Draw ticks and labels
        for (double time = 0.0; time <= timelineLength; time += markerIntervalSeconds) {
            int x = timeToPixel(time) + LEFT_PADDING;
            if (x >= 0 && x < getWidth()) {
                double totalBeats = time / secondsPerBeat;
                int bar = static_cast<int>(totalBeats / timeSignatureNumerator) + 1;
                double beatInBarFractional = std::fmod(totalBeats, timeSignatureNumerator);
                int beatInBar = static_cast<int>(beatInBarFractional) + 1;

                bool isBarStart = beatInBarFractional < 0.001;
                bool isBeatStart = std::fmod(beatInBarFractional, 1.0) < 0.001;

                // Skip tick if it's at a loop marker position (loop markers draw their own ticks)
                bool isAtLoopMarker = (loopStartTime >= 0 && loopEndTime > loopStartTime) &&
                                      (timeToPixel(time) == timeToPixel(loopStartTime) ||
                                       timeToPixel(time) == timeToPixel(loopEndTime));

                bool isMajor = isBarStart;
                bool isMedium = !isBarStart && isBeatStart;
                int tickHeight = isMajor ? majorTickHeight
                                         : (isMedium ? (majorTickHeight * 2 / 3) : minorTickHeight);

                // Draw tick (unless at loop marker position)
                if (!isAtLoopMarker) {
                    if (isMajor) {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    } else if (isMedium) {
                        g.setColour(
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.7f));
                    } else {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                    }
                    g.drawVerticalLine(x, static_cast<float>(rulerBottom - tickHeight),
                                       static_cast<float>(rulerBottom));
                }

                // Draw label
                bool showLabel = false;
                juce::String labelStr;

                if (isBarStart) {
                    showLabel = true;
                    labelStr = juce::String(bar);
                } else if (isBeatStart && markerIntervalBeats <= 0.5) {
                    showLabel = true;
                    labelStr = juce::String(bar) + "." + juce::String(beatInBar);
                } else if (markerIntervalBeats <= 0.125) {
                    double subdivision = std::fmod(beatInBarFractional, 1.0);
                    if (std::fmod(subdivision, 0.25) < 0.001) {
                        showLabel = true;
                        int sixteenth = static_cast<int>(subdivision * 4) + 1;
                        labelStr = juce::String(bar) + "." + juce::String(beatInBar) + "." +
                                   juce::String(sixteenth);
                    }
                }

                if (showLabel) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.drawText(labelStr, x - 35, labelY, 70, labelHeight,
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

void TimelineComponent::setLoopRegion(double startTime, double endTime) {
    loopStartTime = juce::jmax(0.0, startTime);
    loopEndTime = juce::jmin(timelineLength, endTime);
    loopEnabled = (loopStartTime >= 0 && loopEndTime > loopStartTime);

    if (onLoopRegionChanged) {
        onLoopRegionChanged(loopStartTime, loopEndTime);
    }

    repaint();
}

void TimelineComponent::clearLoopRegion() {
    loopStartTime = -1.0;
    loopEndTime = -1.0;
    loopEnabled = false;

    if (onLoopRegionChanged) {
        onLoopRegionChanged(-1.0, -1.0);
    }

    repaint();
}

void TimelineComponent::setLoopEnabled(bool enabled) {
    if (loopStartTime >= 0 && loopEndTime > loopStartTime) {
        loopEnabled = enabled;
        repaint();
    }
}

void TimelineComponent::drawLoopMarkers(juce::Graphics& g) {
    // Draw background elements: shaded region and vertical lines
    // Time markers will be drawn on top of this
    if (loopStartTime < 0 || loopEndTime <= loopStartTime) {
        return;
    }

    // Get layout configuration - loop markers only cover the ruler area, not arrangement
    auto& layout = LayoutConfig::getInstance();
    int rulerTop = layout.arrangementBarHeight;
    int totalHeight = getHeight();

    int startX = timeToPixel(loopStartTime) + LEFT_PADDING;
    int endX = timeToPixel(loopEndTime) + LEFT_PADDING;

    // Skip if completely out of view
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    // Use different colors based on enabled state
    juce::Colour regionColour = loopEnabled
                                    ? DarkTheme::getColour(DarkTheme::LOOP_REGION)
                                    : juce::Colour(0x15808080);  // Light grey, very transparent

    // Draw shaded region covering only the ruler area (not arrangement bar)
    g.setColour(regionColour);
    g.fillRect(startX, rulerTop, endX - startX, totalHeight - rulerTop);

    // Note: Vertical lines are not drawn here - loop flags include tick-like lines
    // that replace the regular ticks at those positions (ticks are skipped at loop bounds)
}

void TimelineComponent::drawLoopMarkerFlags(juce::Graphics& g) {
    // Draw foreground elements: triangular flags, connecting line, and tick-like vertical lines
    // These replace the regular ticks at loop boundary positions
    if (loopStartTime < 0 || loopEndTime <= loopStartTime) {
        return;
    }

    // Get layout configuration
    auto& layout = LayoutConfig::getInstance();
    int arrangementHeight = layout.arrangementBarHeight;
    int majorTickHeight = layout.rulerMajorTickHeight;
    int rulerBottom = getHeight();

    // Position line on the separator (ruler top border), triangles just below in ruler area
    int lineY = arrangementHeight;        // Connecting line aligns with ruler top border
    int flagTop = arrangementHeight + 2;  // Triangles just below the line

    int startX = timeToPixel(loopStartTime) + LEFT_PADDING;
    int endX = timeToPixel(loopEndTime) + LEFT_PADDING;

    // Skip if completely out of view
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    // Use different colors based on enabled state
    juce::Colour markerColour = loopEnabled ? DarkTheme::getColour(DarkTheme::LOOP_MARKER)
                                            : juce::Colour(0xFF606060);  // Medium grey

    g.setColour(markerColour);

    // Draw connecting line at top border
    g.drawLine(static_cast<float>(startX), static_cast<float>(lineY), static_cast<float>(endX),
               static_cast<float>(lineY), 2.0f);

    // Draw tick-like vertical lines at loop boundaries (replaces the regular ticks)
    // Use drawVerticalLine for pixel-perfect alignment with regular ticks
    g.drawVerticalLine(startX, static_cast<float>(rulerBottom - majorTickHeight),
                       static_cast<float>(rulerBottom));
    g.drawVerticalLine(endX, static_cast<float>(rulerBottom - majorTickHeight),
                       static_cast<float>(rulerBottom));

    // Draw start flag (pointing down) at top
    juce::Path startFlag;
    startFlag.addTriangle(static_cast<float>(startX), static_cast<float>(flagTop),
                          static_cast<float>(startX), static_cast<float>(flagTop + 10),
                          static_cast<float>(startX + 7), static_cast<float>(flagTop + 5));
    g.fillPath(startFlag);

    // Draw end flag (pointing down) at top
    juce::Path endFlag;
    endFlag.addTriangle(static_cast<float>(endX), static_cast<float>(flagTop),
                        static_cast<float>(endX), static_cast<float>(flagTop + 10),
                        static_cast<float>(endX - 7), static_cast<float>(flagTop + 5));
    g.fillPath(endFlag);
}

bool TimelineComponent::isOnLoopMarker(int x, int y, bool& isStartMarker) const {
    // Allow detecting loop markers even when disabled (they're still visible in grey)
    if (loopStartTime < 0 || loopEndTime <= loopStartTime) {
        return false;
    }

    // Loop markers are visible in both arrangement bar and ruler area
    // No Y restriction - allow detection anywhere vertically

    int startX = timeToPixel(loopStartTime) + LEFT_PADDING;
    int endX = timeToPixel(loopEndTime) + LEFT_PADDING;

    const int markerThreshold = 8;  // Pixels from marker to trigger drag

    if (std::abs(x - startX) <= markerThreshold) {
        isStartMarker = true;
        return true;
    } else if (std::abs(x - endX) <= markerThreshold) {
        isStartMarker = false;
        return true;
    }

    return false;
}

double TimelineComponent::getSnapInterval() const {
    // Get the visible snap interval based on zoom level and display mode
    const int minPixelSpacing = 30;

    if (displayMode == TimeDisplayMode::Seconds) {
        // Seconds mode - snap to time divisions
        const double intervals[] = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1,  0.2, 0.25,
                                    0.5,   1.0,   2.0,   5.0,  10.0, 15.0, 30.0, 60.0};

        for (double interval : intervals) {
            if (timeDurationToPixels(interval) >= minPixelSpacing) {
                return interval;
            }
        }
        return 1.0;  // Default to 1 second
    } else {
        // Bars/beats mode - snap to beat divisions
        double secondsPerBeat = 60.0 / tempoBPM;

        // Beat fractions: 64th, 32nd, 16th, 8th, quarter, half, bar
        const double beatFractions[] = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0};

        for (double fraction : beatFractions) {
            double intervalSeconds = secondsPerBeat * fraction;
            if (timeDurationToPixels(intervalSeconds) >= minPixelSpacing) {
                return intervalSeconds;
            }
        }

        // If beats are too dense, snap to bars
        double secondsPerBar = secondsPerBeat * timeSignatureNumerator;
        return secondsPerBar;
    }
}

double TimelineComponent::snapTimeToGrid(double time) const {
    if (!snapEnabled) {
        return time;
    }

    double interval = getSnapInterval();
    if (interval <= 0) {
        return time;
    }

    // Round to nearest grid line
    return std::round(time / interval) * interval;
}

void TimelineComponent::setTimeSelection(double startTime, double endTime) {
    timeSelectionStart = startTime;
    timeSelectionEnd = endTime;
    repaint();
}

void TimelineComponent::clearTimeSelection() {
    timeSelectionStart = -1.0;
    timeSelectionEnd = -1.0;
    repaint();
}

void TimelineComponent::drawTimeSelection(juce::Graphics& g) {
    if (timeSelectionStart < 0 || timeSelectionEnd < 0 || timeSelectionEnd <= timeSelectionStart) {
        return;
    }

    int startX = timeToPixel(timeSelectionStart) + LEFT_PADDING;
    int endX = timeToPixel(timeSelectionEnd) + LEFT_PADDING;

    // Skip if out of view
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    // Draw selection highlight covering entire timeline height
    g.setColour(DarkTheme::getColour(DarkTheme::TIME_SELECTION));
    g.fillRect(startX, 0, endX - startX, getHeight());

    // Draw selection edges
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
    g.drawVerticalLine(startX, 0, static_cast<float>(getHeight()));
    g.drawVerticalLine(endX, 0, static_cast<float>(getHeight()));
}

}  // namespace magica
