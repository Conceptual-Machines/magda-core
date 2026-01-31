#include "TimelineComponent.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "Config.hpp"

namespace magda {

TimelineComponent::TimelineComponent() {
    // Load configuration
    auto& config = magda::Config::getInstance();
    timelineLength = config.getDefaultTimelineLength();

    setMouseCursor(juce::MouseCursor::NormalCursor);
    setWantsKeyboardFocus(false);
    setSize(800, 40);

    // Arrangement sections are empty by default - can be added via addSection()
    arrangementLocked = true;
}

TimelineComponent::~TimelineComponent() {
    // Unregister from controller if we have one
    if (timelineController) {
        timelineController->removeListener(this);
    }
}

void TimelineComponent::setController(TimelineController* controller) {
    // Unregister from old controller
    if (timelineController) {
        timelineController->removeListener(this);
    }

    timelineController = controller;

    // Register with new controller
    if (timelineController) {
        timelineController->addListener(this);

        // Sync initial state
        const auto& state = timelineController->getState();
        timelineLength = state.timelineLength;
        zoom = state.zoom.horizontalZoom;
        playheadPosition = state.playhead.getPosition();
        displayMode = state.display.timeDisplayMode;
        tempoBPM = state.tempo.bpm;
        timeSignatureNumerator = state.tempo.timeSignatureNumerator;
        timeSignatureDenominator = state.tempo.timeSignatureDenominator;
        snapEnabled = state.display.snapEnabled;
        arrangementLocked = state.display.arrangementLocked;

        // Sync loop region
        if (state.loop.isValid()) {
            loopStartTime = state.loop.startTime;
            loopEndTime = state.loop.endTime;
            loopEnabled = state.loop.enabled;
        }

        // Sync time selection (only if visually active)
        if (state.selection.isVisuallyActive()) {
            timeSelectionStart = state.selection.startTime;
            timeSelectionEnd = state.selection.endTime;
        } else {
            timeSelectionStart = -1.0;
            timeSelectionEnd = -1.0;
        }

        repaint();
    }
}

// ===== TimelineStateListener Implementation =====

void TimelineComponent::timelineStateChanged(const TimelineState& state) {
    // General state change - sync all cached values
    timelineLength = state.timelineLength;
    displayMode = state.display.timeDisplayMode;
    tempoBPM = state.tempo.bpm;
    timeSignatureNumerator = state.tempo.timeSignatureNumerator;
    timeSignatureDenominator = state.tempo.timeSignatureDenominator;
    snapEnabled = state.display.snapEnabled;
    arrangementLocked = state.display.arrangementLocked;
    repaint();
}

void TimelineComponent::zoomStateChanged(const TimelineState& state) {
    zoom = state.zoom.horizontalZoom;
    repaint();
}

void TimelineComponent::loopStateChanged(const TimelineState& state) {
    if (state.loop.isValid()) {
        loopStartTime = state.loop.startTime;
        loopEndTime = state.loop.endTime;
        loopEnabled = state.loop.enabled;
    } else {
        loopStartTime = -1.0;
        loopEndTime = -1.0;
        loopEnabled = false;
    }
    repaint();
}

void TimelineComponent::selectionStateChanged(const TimelineState& state) {
    // Only show selection visually if not hidden (e.g., when loop is created from it)
    if (state.selection.isVisuallyActive()) {
        timeSelectionStart = state.selection.startTime;
        timeSelectionEnd = state.selection.endTime;
    } else {
        timeSelectionStart = -1.0;
        timeSelectionEnd = -1.0;
    }
    repaint();
}

void TimelineComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND));

    // Get layout configuration
    auto& layout = LayoutConfig::getInstance();
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    int arrangementTop = chordHeight;  // Arrangement starts below chord row

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Show visual feedback when actively zooming
    if (isZooming) {
        // Slightly brighten the background when zooming
        g.setColour(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND).brighter(0.1f));
        g.fillRect(getLocalBounds().reduced(1));
    }

    // Draw time selection (background layer)
    drawTimeSelection(g);

    // Draw loop markers (background - shaded region behind time labels)
    drawLoopMarkers(g);

    // Draw arrangement sections
    drawArrangementSections(g);

    // Draw time markers (in time ruler section) - ON TOP of loop region
    drawTimeMarkers(g);

    // Draw separator line between arrangement and time ruler
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
    g.drawLine(0, static_cast<float>(arrangementTop + arrangementHeight),
               static_cast<float>(getWidth()),
               static_cast<float>(arrangementTop + arrangementHeight), 1.0f);

    // Draw separator line above ticks (separates time labels from tick area)
    int tickAreaTop =
        arrangementTop + arrangementHeight + layout.timeRulerHeight - layout.rulerMajorTickHeight;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(0, static_cast<float>(tickAreaTop), static_cast<float>(getWidth()),
               static_cast<float>(tickAreaTop), 1.0f);

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
    // Give keyboard focus to viewport so shortcuts work after clicking timeline
    // Timeline is inside a viewport, so we need to go up to find a focusable parent
    auto* parent = getParentComponent();
    while (parent != nullptr) {
        if (parent->getWantsKeyboardFocus()) {
            parent->grabKeyboardFocus();
            break;
        }
        parent = parent->getParentComponent();
    }

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
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    int timeRulerHeight = layout.timeRulerHeight;
    int arrangementBottom = chordHeight + arrangementHeight;
    int timeRulerEnd = arrangementBottom + timeRulerHeight;
    // Split ruler: upper 2/3 for zoom, lower 1/3 for time selection
    int rulerMidpoint = arrangementBottom + (timeRulerHeight * 2 / 3);

    // Define zones based on LayoutConfig
    bool inSectionsArea = event.y >= chordHeight && event.y <= arrangementBottom;
    bool inTimeRulerArea = event.y > arrangementBottom && event.y <= timeRulerEnd;
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

    // Check for loop top border (drag entire loop region)
    if (isOnLoopTopBorder(event.x, event.y)) {
        isDraggingLoopRegion = true;
        double clickTime = pixelToTime(event.x);
        loopDragOffset = clickTime - loopStartTime;
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
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    int arrangementBottom = chordHeight + arrangementHeight;

    // Check for loop markers first - they span both arrangement and ruler areas
    bool isStartMarker;
    if (isOnLoopMarker(event.x, event.y, isStartMarker)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    // Check for loop top border (for dragging entire region)
    if (isOnLoopTopBorder(event.x, event.y)) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    if (event.y >= chordHeight && event.y <= arrangementBottom) {
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
        int rulerMidpoint = layout.getRulerZoneSplitY();

        if (event.y < rulerMidpoint) {
            // Upper ruler area - zoom cursor (left/right drag to zoom)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
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

    // Handle loop marker dragging (edges)
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

    // Handle loop region dragging (entire loop)
    if (isDraggingLoopRegion) {
        double loopDuration = loopEndTime - loopStartTime;
        double clickTime = pixelToTime(event.x);
        double newStartTime = clickTime - loopDragOffset;

        // Apply snap to grid if enabled
        if (snapEnabled) {
            newStartTime = snapTimeToGrid(newStartTime);
        }

        // Clamp to valid range
        newStartTime = juce::jmax(0.0, newStartTime);
        double newEndTime = newStartTime + loopDuration;

        // Don't let end go past timeline length
        if (newEndTime > timelineLength) {
            newEndTime = timelineLength;
            newStartTime = newEndTime - loopDuration;
        }

        loopStartTime = newStartTime;
        loopEndTime = newEndTime;

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

        // Check for modifier keys for zoom speed control
        bool isShiftHeld = event.mods.isShiftDown();
        bool isAltHeld = event.mods.isAltDown();

        // Zoom-level-dependent sensitivity (Bitwig-like behavior):
        // - At low zoom (zoomed out): more responsive (less drag needed)
        // - At high zoom (zoomed in): finer control (more drag needed)
        // This makes zooming feel natural at all levels
        auto& config = magda::Config::getInstance();
        double minZoomLevel = config.getMinZoomLevel();
        double maxZoomLevel = config.getMaxZoomLevel();

        // Calculate where we are in the zoom range (0 = min, 1 = max)
        // Use log scale since zoom is exponential
        double logMin = std::log(minZoomLevel);
        double logMax = std::log(maxZoomLevel);
        double logCurrent = std::log(zoomStartValue);
        double zoomPosition = (logCurrent - logMin) / (logMax - logMin);
        zoomPosition = juce::jlimit(0.0, 1.0, zoomPosition);

        // Get sensitivity from Config
        // zoomInSensitivity: pixels to double when zoomed out (lower = faster)
        // zoomOutSensitivity: pixels to double when zoomed in (higher = finer control)
        double minZoomSensitivity = config.getZoomInSensitivity();   // 25.0 - fast when zoomed out
        double maxZoomSensitivity = config.getZoomOutSensitivity();  // 40.0 - finer when zoomed in

        // Scale sensitivity based on zoom position (interpolate between config values)
        double baseSensitivity =
            minZoomSensitivity + zoomPosition * (maxZoomSensitivity - minZoomSensitivity);

        double sensitivity = baseSensitivity;
        if (isShiftHeld) {
            sensitivity = config.getZoomInSensitivityShift();  // 8.0 - turbo fast
        } else if (isAltHeld) {
            sensitivity = baseSensitivity * 3.0;  // Alt/Option: fine zoom (slower)
        }

        // Progressive acceleration: the further you drag, the faster it goes
        // This helps when you need to zoom very far
        double absDeltaY = std::abs(static_cast<double>(deltaY));
        if (absDeltaY > 80.0) {
            // After 80px of drag, progressively reduce sensitivity (faster zoom)
            double accelerationFactor = 1.0 + (absDeltaY - 80.0) / 150.0;
            sensitivity /= accelerationFactor;
        }

        // Exponential zoom: drag up doubles, drag down halves
        // This feels natural because zoom is multiplicative
        double exponent = static_cast<double>(deltaY) / sensitivity;
        double newZoom = zoomStartValue * std::pow(2.0, exponent);

        // Calculate minimum zoom based on timeline length and viewport width
        double minZoom = minZoomLevel;
        if (timelineLength > 0 && viewportWidth > 0) {
            double availableWidth = viewportWidth - 50.0;
            minZoom = availableWidth / timelineLength;
            minZoom = juce::jmax(minZoom, minZoomLevel);
        }

        // Apply limits
        if (std::isnan(newZoom) || newZoom < minZoom) {
            newZoom = minZoom;
        } else if (newZoom > maxZoomLevel) {
            newZoom = maxZoomLevel;
        }

        // Call the callback with zoom value, anchor time, and screen position
        if (onZoomChanged) {
            onZoomChanged(newZoom, zoomAnchorTime, zoomAnchorScreenX);
        }
    }
}

void TimelineComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    auto& layout = LayoutConfig::getInstance();
    int rulerTop = layout.chordRowHeight + layout.arrangementBarHeight;

    // Check if double-click is in the ruler area (below chord and arrangement)
    if (event.y >= rulerTop) {
        // Double-click in ruler area - zoom to fit loop if enabled
        if (loopEnabled && loopStartTime >= 0 && loopEndTime > loopStartTime) {
            if (onZoomToFitRequested) {
                onZoomToFitRequested(loopStartTime, loopEndTime);
            }
            return;
        }
    }

    // Handle section editing in arrangement bar
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
        // If selection is too small (just a click), move playhead instead
        if (std::abs(timeSelectionEnd - timeSelectionStart) < 0.01) {
            // Clear the selection
            timeSelectionStart = -1.0;
            timeSelectionEnd = -1.0;
            if (onTimeSelectionChanged) {
                onTimeSelectionChanged(-1.0, -1.0);
            }
            // Move playhead to click position
            double clickTime = pixelToTime(event.x);
            clickTime = juce::jlimit(0.0, timelineLength, clickTime);
            if (snapEnabled) {
                clickTime = snapTimeToGrid(clickTime);
            }
            setPlayheadPosition(clickTime);
            if (onPlayheadPositionChanged) {
                onPlayheadPositionChanged(clickTime);
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
    isDraggingLoopRegion = false;

    // End zoom operation
    if (isZooming && onZoomEnd) {
        onZoomEnd();
    }

    // Handle pending playhead click - if we didn't zoom, set the playhead
    // Skip if this is a double-click (getNumberOfClicks() > 1) to allow zoom-to-fit
    if (isPendingPlayheadClick && !isZooming && event.getNumberOfClicks() == 1) {
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
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    int timeRulerHeight = layout.timeRulerHeight;

    // Time ruler area starts after chord row and arrangement bar
    int rulerTop = chordHeight + arrangementHeight;
    int rulerBottom = rulerTop + timeRulerHeight;

    // Tick and label sizing from config
    int majorTickHeight = layout.rulerMajorTickHeight;
    int minorTickHeight = layout.rulerMinorTickHeight;
    int labelFontSize = layout.rulerLabelFontSize;
    int labelY = rulerTop + layout.rulerLabelTopMargin;
    int labelHeight = timeRulerHeight - majorTickHeight - layout.rulerLabelTopMargin - 2;

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(static_cast<float>(labelFontSize)));

    const int minPixelSpacing = layout.minGridPixelSpacing;

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
                // Use drawLine with 1.0f width to match grid lines in TrackContentPanel
                if (!isAtLoopMarker) {
                    g.setColour(DarkTheme::getColour(isMajor ? DarkTheme::TEXT_SECONDARY
                                                             : DarkTheme::TEXT_DIM));
                    g.drawLine(static_cast<float>(x), static_cast<float>(rulerBottom - tickHeight),
                               static_cast<float>(x), static_cast<float>(rulerBottom), 1.0f);
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

        // Define musical intervals from 1/512 to 1/4 note
        // Beat fractions: 1/512, 1/256, 1/128, 1/64, 1/32, 1/16, 1/8, 1/4
        const double beatFractions[] = {0.0078125, 0.015625, 0.03125, 0.0625,
                                        0.125,     0.25,     0.5,     1.0};
        const int barMultiples[] = {1, 2, 4, 8, 16, 32};

        // Find appropriate tick interval based on zoom level
        double markerIntervalBeats = 1.0;  // Default to quarter note
        bool foundInterval = false;

        // First try beat subdivisions
        for (double fraction : beatFractions) {
            double intervalSeconds = secondsPerBeat * fraction;
            if (timeDurationToPixels(intervalSeconds) >= minPixelSpacing) {
                markerIntervalBeats = fraction;
                foundInterval = true;
                break;
            }
        }

        // If no beat subdivision fits, use bar multiples (for zoomed out views)
        if (!foundInterval) {
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
                // Use drawLine with 1.0f width to match grid lines in TrackContentPanel
                if (!isAtLoopMarker) {
                    if (isMajor) {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    } else if (isMedium) {
                        g.setColour(
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.7f));
                    } else {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                    }
                    g.drawLine(static_cast<float>(x), static_cast<float>(rulerBottom - tickHeight),
                               static_cast<float>(x), static_cast<float>(rulerBottom), 1.0f);
                }

                // Draw labels with three distinct styles: bars, beats, subdivisions
                // Calculate pixel spacings for label decisions
                double pixelsPerBeat = timeDurationToPixels(secondsPerBeat);
                double pixelsPerBar = timeDurationToPixels(secondsPerBar);
                double pixelsPerSubdiv = timeDurationToPixels(markerIntervalSeconds);

                // Determine bar label interval (show every Nth bar when zoomed out)
                int barLabelInterval = 1;
                if (pixelsPerBar < 40) {
                    barLabelInterval = 8;
                } else if (pixelsPerBar < 60) {
                    barLabelInterval = 4;
                } else if (pixelsPerBar < 90) {
                    barLabelInterval = 2;
                }

                // Calculate subdivision within beat (1-indexed position)
                double subdivInBeat = std::fmod(beatInBarFractional, 1.0);
                int subdivsPerBeat = static_cast<int>(std::round(1.0 / markerIntervalBeats));
                int subdivIndex = static_cast<int>(std::round(subdivInBeat * subdivsPerBeat)) + 1;

                // Subdivision 1 is the beat itself, so only show subdiv labels for 2+
                bool isSubdivisionNotBeat = !isBeatStart && subdivIndex > 1;

                if (isBarStart && (bar - 1) % barLabelInterval == 0) {
                    // === BAR LABEL: Large, bright ===
                    // Format: "1", "2", "3"
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                    g.setFont(FontManager::getInstance().getUIFont(12.0f).boldened());
                    g.drawText(juce::String(bar), x - 35, labelY, 70, labelHeight,
                               juce::Justification::centred);
                } else if (isBeatStart && !isBarStart && pixelsPerBeat >= 50) {
                    // === BEAT LABEL: Medium, secondary color ===
                    // Format: "1.2", "1.3", "1.4"
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.setFont(FontManager::getInstance().getUIFont(10.0f));
                    juce::String labelStr = juce::String(bar) + "." + juce::String(beatInBar);
                    g.drawText(labelStr, x - 25, labelY, 50, labelHeight,
                               juce::Justification::centred);
                } else if (isSubdivisionNotBeat && pixelsPerSubdiv >= 30) {
                    // === SUBDIVISION LABEL: Small, dim ===
                    // Format: "1.1.2", "1.1.3", "1.2.2"
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                    g.setFont(FontManager::getInstance().getUIFont(8.0f));
                    juce::String labelStr = juce::String(bar) + "." + juce::String(beatInBar) +
                                            "." + juce::String(subdivIndex);
                    g.drawText(labelStr, x - 30, labelY + 2, 60, labelHeight,
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
    auto& layout = LayoutConfig::getInstance();
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    auto sectionArea = juce::Rectangle<int>(startX, chordHeight, width, arrangementHeight);

    // Section background - dimmed if locked
    float alpha = arrangementLocked ? 0.2f : 0.3f;
    g.setColour(section.colour.withAlpha(alpha));
    g.fillRect(sectionArea);

    // Section border - different style if locked
    if (arrangementLocked) {
        g.setColour(section.colour.withAlpha(0.5f));
        // Draw dotted border to indicate locked state
        const float dashLengths[] = {2.0f, 2.0f};
        float sectionTop = static_cast<float>(chordHeight);
        g.drawDashedLine(juce::Line<float>(startX, sectionTop, startX, sectionArea.getBottom()),
                         dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(endX, sectionTop, endX, sectionArea.getBottom()),
                         dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(startX, sectionTop, endX, sectionTop), dashLengths, 2,
                         1.0f);
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
    auto& layout = LayoutConfig::getInstance();
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    // Section area is between chord row and time ruler
    if (y < chordHeight || y > chordHeight + arrangementHeight) {
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

    // Get layout configuration - loop markers only cover the ruler area, not arrangement or chord
    auto& layout = LayoutConfig::getInstance();
    int rulerTop = layout.chordRowHeight + layout.arrangementBarHeight;
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
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    int majorTickHeight = layout.rulerMajorTickHeight;
    int rulerBottom = getHeight();

    // Position line on the separator (ruler top border), triangles just below in ruler area
    int lineY = chordHeight + arrangementHeight;  // Connecting line aligns with ruler top border
    int flagTop = chordHeight + arrangementHeight + 2;  // Triangles just below the line

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
    // Slightly thicker (2.0f) to visually connect with the flags
    g.drawLine(static_cast<float>(startX), static_cast<float>(rulerBottom - majorTickHeight),
               static_cast<float>(startX), static_cast<float>(rulerBottom), 2.0f);
    g.drawLine(static_cast<float>(endX), static_cast<float>(rulerBottom - majorTickHeight),
               static_cast<float>(endX), static_cast<float>(rulerBottom), 2.0f);

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

bool TimelineComponent::isOnLoopTopBorder(int x, int y) const {
    // Check if mouse is on the top connecting line of the loop region
    if (loopStartTime < 0 || loopEndTime <= loopStartTime) {
        return false;
    }

    auto& layout = LayoutConfig::getInstance();
    // Top border is at the bottom of arrangement row (below chord row)
    int lineY = layout.chordRowHeight + layout.arrangementBarHeight;
    const int verticalThreshold = 6;  // Pixels above/below line to trigger

    // Check Y is near the top border line
    if (std::abs(y - lineY) > verticalThreshold) {
        return false;
    }

    // Check X is between the loop markers
    int startX = timeToPixel(loopStartTime) + LEFT_PADDING;
    int endX = timeToPixel(loopEndTime) + LEFT_PADDING;
    const int horizontalMargin = 10;  // Don't trigger too close to the edges (those are resize)

    return x > (startX + horizontalMargin) && x < (endX - horizontalMargin);
}

double TimelineComponent::getSnapInterval() const {
    // Get the visible snap interval based on zoom level and display mode
    auto& layout = LayoutConfig::getInstance();
    const int minPixelSpacing = layout.minGridPixelSpacing;

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

    // Get ruler top (below chord row and arrangement bar) to match loop region
    auto& layout = LayoutConfig::getInstance();
    int rulerTop = layout.chordRowHeight + layout.arrangementBarHeight;

    // Draw selection highlight covering ruler area (not arrangement bar)
    g.setColour(DarkTheme::getColour(DarkTheme::TIME_SELECTION));
    g.fillRect(startX, rulerTop, endX - startX, getHeight() - rulerTop);

    // Draw selection edges (use drawLine for consistency with grid)
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
    g.drawLine(static_cast<float>(startX), static_cast<float>(rulerTop), static_cast<float>(startX),
               static_cast<float>(getHeight()), 1.0f);
    g.drawLine(static_cast<float>(endX), static_cast<float>(rulerTop), static_cast<float>(endX),
               static_cast<float>(getHeight()), 1.0f);
}

}  // namespace magda
