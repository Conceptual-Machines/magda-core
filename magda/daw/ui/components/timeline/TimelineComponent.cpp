#include "TimelineComponent.hpp"

#include <algorithm>
#include <cmath>

#include "../../themes/CursorManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "Config.hpp"
#include "core/TempoUtils.hpp"

namespace magda {

namespace {

juce::Range<int> getVisibleXRange(const juce::Graphics& g, int componentWidth) {
    auto clip = g.getClipBounds();
    int left = juce::jlimit(0, componentWidth, clip.getX());
    int right = juce::jlimit(0, componentWidth, clip.getRight());
    return {left, right};
}

juce::Rectangle<int> getVisibleRect(const juce::Graphics& g, int componentWidth, int startX,
                                    int endX, int y, int height) {
    auto visibleX = getVisibleXRange(g, componentWidth);
    int left = juce::jmax(startX, visibleX.getStart());
    int right = juce::jmin(endX, visibleX.getEnd());

    if (right <= left || height <= 0)
        return {};

    return {left, y, right - left, height};
}

bool isXVisible(const juce::Graphics& g, int componentWidth, int x) {
    auto visibleX = getVisibleXRange(g, componentWidth);
    return x >= visibleX.getStart() && x <= visibleX.getEnd();
}

}  // namespace

TimelineComponent::TimelineComponent() {
    // Load configuration, converting bars → seconds at default tempo
    auto& config = magda::Config::getInstance();
    TempoState defaultTempo;
    timelineLength = defaultTempo.barsToTime(config.getDefaultTimelineLengthBars());

    setMouseCursor(juce::MouseCursor::NormalCursor);
    setWantsKeyboardFocus(false);
    setSize(800, 40);

    // Arrangement sections are empty by default - can be added via addSectionBeats()
    arrangementLocked = true;
}

TimelineComponent::~TimelineComponent() = default;

void TimelineComponent::setController(TimelineController* controller) {
    timelineListener_.reset(controller);

    if (controller) {
        // Sync initial state
        const auto& state = controller->getState();
        timelineLength = state.timelineLength;
        pixelsPerBeat = state.zoom.horizontalZoom;
        playheadPositionBeats = state.playhead.getCurrentPositionBeats();
        displayMode = state.display.timeDisplayMode;
        tempoBPM = state.tempo.bpm;
        timeSignatureNumerator = state.tempo.timeSignatureNumerator;
        timeSignatureDenominator = state.tempo.timeSignatureDenominator;
        snapEnabled = state.display.snapEnabled;
        arrangementLocked = state.display.arrangementLocked;
        gridQuantize = state.display.gridQuantize;

        // Sync loop region
        if (state.loop.isValid()) {
            loopInteraction_.setLoopRange(state.loop.startBeats, state.loop.endBeats,
                                          state.loop.enabled);
        }

        // Initialize loop interaction helper
        initLoopInteraction();

        // Sync time selection (only if visually active)
        if (state.selection.isVisuallyActive()) {
            timeSelectionStartBeats = state.selection.startBeats;
            timeSelectionEndBeats = state.selection.endBeats;
        } else {
            timeSelectionStartBeats = -1.0;
            timeSelectionEndBeats = -1.0;
        }

        repaint();
    }
}

// ===== TimelineStateListener Implementation =====

void TimelineComponent::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    bool needsRepaint = false;

    // Zoom/scroll changes
    if (hasFlag(changes, ChangeFlags::Zoom) || hasFlag(changes, ChangeFlags::Scroll)) {
        pixelsPerBeat = state.zoom.horizontalZoom;
        needsRepaint = true;
    }

    // Loop changes
    if (hasFlag(changes, ChangeFlags::Loop)) {
        if (state.loop.isValid()) {
            loopInteraction_.setLoopRange(state.loop.startBeats, state.loop.endBeats,
                                          state.loop.enabled);
        } else {
            loopInteraction_.setLoopRange(-1.0, -1.0, false);
        }
        needsRepaint = true;
    }

    // Selection changes
    if (hasFlag(changes, ChangeFlags::Selection)) {
        if (state.selection.isVisuallyActive()) {
            timeSelectionStartBeats = state.selection.startBeats;
            timeSelectionEndBeats = state.selection.endBeats;
        } else {
            timeSelectionStartBeats = -1.0;
            timeSelectionEndBeats = -1.0;
        }
        needsRepaint = true;
    }

    // General cache sync (timeline length, display mode, snap, arrangement lock, grid quantize)
    if (timelineLength != state.timelineLength) {
        timelineLength = state.timelineLength;
        needsRepaint = true;
    }
    if (displayMode != state.display.timeDisplayMode) {
        displayMode = state.display.timeDisplayMode;
        needsRepaint = true;
    }
    if (snapEnabled != state.display.snapEnabled) {
        snapEnabled = state.display.snapEnabled;
        needsRepaint = true;
    }
    if (arrangementLocked != state.display.arrangementLocked) {
        arrangementLocked = state.display.arrangementLocked;
        needsRepaint = true;
    }
    if (gridQuantize.autoGrid != state.display.gridQuantize.autoGrid ||
        gridQuantize.numerator != state.display.gridQuantize.numerator ||
        gridQuantize.denominator != state.display.gridQuantize.denominator) {
        gridQuantize = state.display.gridQuantize;
        needsRepaint = true;
    }

    // Playhead and tempo/time-sig don't affect cached pixel positions (ppb zoom) directly.
    if (hasFlag(changes, ChangeFlags::Playhead)) {
        playheadPositionBeats = state.playhead.getCurrentPositionBeats();
        needsRepaint = true;
    }
    tempoBPM = state.tempo.bpm;
    timeSignatureNumerator = state.tempo.timeSignatureNumerator;
    timeSignatureDenominator = state.tempo.timeSignatureDenominator;

    if (needsRepaint)
        repaint();
}

void TimelineComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND));

    // Get layout configuration
    auto& layout = LayoutConfig::getInstance();
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    int arrangementTop = chordHeight;  // Arrangement starts below chord row

    auto visibleX = getVisibleXRange(g, getWidth());
    const float visibleLeft = static_cast<float>(visibleX.getStart());
    const float visibleRight = static_cast<float>(visibleX.getEnd());

    // Draw border for the visible slice only. At deep zoom the timeline component can be
    // millions of pixels wide, and sending that full rectangle to JUCE's software renderer
    // can build pathological edge tables on Linux.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    if (visibleX.getLength() > 0) {
        g.drawLine(visibleLeft, 0.0f, visibleRight, 0.0f, 1.0f);
        g.drawLine(visibleLeft, static_cast<float>(getHeight() - 1), visibleRight,
                   static_cast<float>(getHeight() - 1), 1.0f);
    }
    if (isXVisible(g, getWidth(), 0))
        g.drawLine(0.0f, 0.0f, 0.0f, static_cast<float>(getHeight()), 1.0f);
    if (isXVisible(g, getWidth(), getWidth() - 1)) {
        float rightEdge = static_cast<float>(getWidth() - 1);
        g.drawLine(rightEdge, 0.0f, rightEdge, static_cast<float>(getHeight()), 1.0f);
    }

    // Show visual feedback when actively zooming
    if (isZooming) {
        // Slightly brighten the background when zooming
        g.setColour(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND).brighter(0.1f));
        g.fillRect(g.getClipBounds());
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
    g.drawLine(visibleLeft, static_cast<float>(arrangementTop + arrangementHeight), visibleRight,
               static_cast<float>(arrangementTop + arrangementHeight), 1.0f);

    // Draw separator line above ticks
    int tickAreaTop =
        arrangementTop + arrangementHeight + layout.timeRulerHeight - layout.rulerMajorTickHeight;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(visibleLeft, static_cast<float>(tickAreaTop), visibleRight,
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
    setPlayheadPositionBeats(secondsToBeats(position));
}

void TimelineComponent::setPlayheadPositionBeats(double positionBeats) {
    playheadPositionBeats = juce::jlimit(0.0, getTimelineLengthBeats(), positionBeats);
    // Don't repaint - timeline doesn't draw playhead anymore
}

void TimelineComponent::setZoom(double pixelsPerBeatIn) {
    pixelsPerBeat = pixelsPerBeatIn;
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
    tempoBPM = clampBpm(bpm);
}

void TimelineComponent::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = clampTimeSignatureValue(numerator);
    timeSignatureDenominator = clampTimeSignatureValue(denominator);
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
    zoomStartValue = pixelsPerBeat;
    isZooming = false;
    isPendingPlayheadClick = false;
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

    // Check for loop marker dragging — only within the loop strip
    int rulerBottom = arrangementBottom + timeRulerHeight;
    int tickAreaTop = rulerBottom - layout.rulerMajorTickHeight;
    int loopStripTop = tickAreaTop - LayoutConfig::loopStripHeight;
    if (event.y >= loopStripTop && event.y < tickAreaTop) {
        if (loopInteraction_.mouseDown(event.x, event.y))
            return;
    }

    // Zone 1a: Lower ruler area (near tick labels) - start time selection
    if (inTimeSelectionZone) {
        isDraggingTimeSelection = true;
        double startBeats = pixelToBeats(event.x);
        startBeats = juce::jlimit(0.0, getTimelineLengthBeats(), startBeats);
        if (snapEnabled) {
            startBeats = snapBeatsToGrid(startBeats);
        }
        timeSelectionDragStartBeats = startBeats;
        timeSelectionStartBeats = startBeats;
        timeSelectionEndBeats = startBeats;
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

    int rulerBottom = chordHeight + arrangementHeight + layout.timeRulerHeight;
    int tickAreaTop = rulerBottom - layout.rulerMajorTickHeight;
    int loopStripTop = tickAreaTop - LayoutConfig::loopStripHeight;

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
        // In time ruler area — check loop markers only within the loop strip
        if (event.y >= loopStripTop && event.y < tickAreaTop) {
            auto loopCursor = loopInteraction_.getCursor(event.x, event.y);
            if (loopCursor != juce::MouseCursor::NormalCursor) {
                setMouseCursor(loopCursor);
                return;
            }
        }

        // Upper half: zoom (crosshair), Lower half: time selection (I-beam)
        int rulerMidpoint = layout.getRulerZoneSplitY();

        if (event.y < rulerMidpoint) {
            setMouseCursor(CursorManager::getInstance().getZoomCursor());
        } else {
            setMouseCursor(juce::MouseCursor::IBeamCursor);
        }
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& event) {
    // Handle time selection dragging first
    if (isDraggingTimeSelection) {
        double currentBeats = pixelToBeats(event.x);
        currentBeats = juce::jlimit(0.0, getTimelineLengthBeats(), currentBeats);
        if (snapEnabled) {
            currentBeats = snapBeatsToGrid(currentBeats);
        }

        // Update selection based on drag direction
        if (currentBeats < timeSelectionDragStartBeats) {
            timeSelectionStartBeats = currentBeats;
            timeSelectionEndBeats = timeSelectionDragStartBeats;
        } else {
            timeSelectionStartBeats = timeSelectionDragStartBeats;
            timeSelectionEndBeats = currentBeats;
        }

        if (onTimeSelectionBeatsChanged) {
            onTimeSelectionBeatsChanged(timeSelectionStartBeats, timeSelectionEndBeats);
        }
        repaint();
        return;
    }

    // Handle loop marker dragging
    if (loopInteraction_.mouseDrag(event.x, event.y))
        return;

    // Handle section dragging
    if (!arrangementLocked && isDraggingSection && selectedSectionIndex >= 0) {
        // Move entire section
        auto& section = *sections[selectedSectionIndex];
        double sectionDurationBeats = section.getDurationBeats();
        double newStartBeats = juce::jmax(0.0, pixelToBeats(event.x));
        double newEndBeats =
            juce::jmin(getTimelineLengthBeats(), newStartBeats + sectionDurationBeats);

        section.setFromBeats(newStartBeats, newEndBeats, tempoBPM);

        if (onSectionChanged) {
            onSectionChanged(selectedSectionIndex, section);
        }
        repaint();
        return;
    }

    if (!arrangementLocked && isDraggingEdge && selectedSectionIndex >= 0) {
        // Resize section
        auto& section = *sections[selectedSectionIndex];
        double newBeats =
            juce::jmax(0.0, juce::jmin(getTimelineLengthBeats(), pixelToBeats(event.x)));
        constexpr double minDurationBeats = 1.0;

        if (isDraggingStart) {
            section.setFromBeats(juce::jmin(newBeats, section.endBeats - minDurationBeats),
                                 section.endBeats, tempoBPM);
        } else {
            section.setFromBeats(section.startBeats,
                                 juce::jmax(newBeats, section.startBeats + minDurationBeats),
                                 tempoBPM);
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
            isZooming = true;
            isPendingPlayheadClick = false;  // Cancel any pending playhead click
            // Capture the beat position under the mouse at zoom start (using initial zoom level)
            zoomAnchorBeats = pixelToBeats(mouseDownX);
            zoomAnchorBeats = juce::jlimit(0.0, getTimelineLengthBeats(), zoomAnchorBeats);
            // Capture the screen X position where the mouse is (relative to this component)
            zoomAnchorScreenX = mouseDownX;
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
            sensitivity = deltaY >= 0 ? config.getZoomInSensitivityShift()
                                      : config.getZoomOutSensitivityShift();
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
        // Allow zooming out to 1/4 of the fit-to-viewport level
        double minZoom = minZoomLevel;
        if (timelineLength > 0 && viewportWidth > 0) {
            double availableWidth = viewportWidth - 50.0;
            minZoom = (availableWidth / timelineLength) * 0.25;
            minZoom = juce::jmax(minZoom, minZoomLevel);
        }

        // Apply limits
        if (std::isnan(newZoom) || newZoom < minZoom) {
            newZoom = minZoom;
        } else if (newZoom > maxZoomLevel) {
            newZoom = maxZoomLevel;
        }

        // Call the callback with zoom value, anchor beat, and screen position
        if (onZoomChanged) {
            onZoomChanged(newZoom, zoomAnchorBeats, zoomAnchorScreenX);
        }
    }
}

void TimelineComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    auto& layout = LayoutConfig::getInstance();
    int rulerTop = layout.chordRowHeight + layout.arrangementBarHeight;

    // Check if double-click is in the ruler area (below chord and arrangement)
    if (event.y >= rulerTop) {
        // Double-click in ruler area - zoom to fit loop if enabled
        if (loopInteraction_.isEnabled() && loopInteraction_.getStartPosition() >= 0 &&
            loopInteraction_.getEndPosition() > loopInteraction_.getStartPosition()) {
            if (onZoomToFitBeatsRequested) {
                onZoomToFitBeatsRequested(loopInteraction_.getStartPosition(),
                                          loopInteraction_.getEndPosition());
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
        if (std::abs(timeSelectionEndBeats - timeSelectionStartBeats) < 0.01) {
            // Clear the selection
            timeSelectionStartBeats = -1.0;
            timeSelectionEndBeats = -1.0;
            if (onTimeSelectionBeatsChanged) {
                onTimeSelectionBeatsChanged(-1.0, -1.0);
            }
            // Move playhead to click position
            double clickBeats = pixelToBeats(event.x);
            clickBeats = juce::jlimit(0.0, getTimelineLengthBeats(), clickBeats);
            if (snapEnabled) {
                clickBeats = snapBeatsToGrid(clickBeats);
            }
            setPlayheadPositionBeats(clickBeats);
            if (onPlayheadPositionBeatsChanged) {
                onPlayheadPositionBeatsChanged(clickBeats);
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
    loopInteraction_.mouseUp(event.x, event.y);

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
            double clickBeats = pixelToBeats(mouseDownX);
            clickBeats = juce::jlimit(0.0, getTimelineLengthBeats(), clickBeats);
            setPlayheadPositionBeats(clickBeats);

            if (onPlayheadPositionBeatsChanged) {
                onPlayheadPositionBeatsChanged(clickBeats);
            }
        }
    }

    isPendingPlayheadClick = false;
    isZooming = false;

    repaint();
}

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel) {
    juce::ignoreUnused(event);

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

void TimelineComponent::addSectionBeats(const juce::String& name, double startBeats,
                                        double endBeats, juce::Colour colour) {
    auto section = std::make_unique<ArrangementSection>(0.0, 0.0, name, colour);
    section->setFromBeats(startBeats, endBeats, tempoBPM);
    sections.push_back(std::move(section));
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

int TimelineComponent::beatsToPixel(double beats) const {
    return static_cast<int>(std::round(beats * pixelsPerBeat));
}

double TimelineComponent::pixelToBeats(int pixel) const {
    if (pixelsPerBeat > 0)
        return static_cast<double>(pixel - LayoutConfig::TIMELINE_LEFT_PADDING) / pixelsPerBeat;
    return 0.0;
}

double TimelineComponent::secondsToBeats(double timeInSeconds) const {
    return timeInSeconds * tempoBPM / 60.0;
}

double TimelineComponent::beatsToSeconds(double beats) const {
    if (tempoBPM > 0)
        return beats * 60.0 / tempoBPM;
    return 0.0;
}

double TimelineComponent::getTimelineLengthBeats() const {
    return secondsToBeats(timelineLength);
}

double TimelineComponent::snapBeatsToGrid(double beats) const {
    if (!snapEnabled)
        return beats;

    double intervalSeconds = getSnapInterval();
    double intervalBeats = secondsToBeats(intervalSeconds);
    if (intervalBeats <= 0.0)
        return beats;

    return std::round(beats / intervalBeats) * intervalBeats;
}

int TimelineComponent::secondsDurationToPixels(double durationSeconds) const {
    double beats = secondsToBeats(durationSeconds);
    return static_cast<int>(std::round(beats * pixelsPerBeat));
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
    int tickBottom = rulerBottom;

    // Cache loop region pixel bounds for tick coloring
    double loopStartBeats = loopInteraction_.getStartPosition();
    double loopEndBeats = loopInteraction_.getEndPosition();
    bool hasLoop = loopStartBeats >= 0 && loopEndBeats > loopStartBeats;
    int loopStartPx =
        hasLoop ? (beatsToPixel(loopStartBeats) + LayoutConfig::TIMELINE_LEFT_PADDING) : -1;
    int loopEndPx =
        hasLoop ? (beatsToPixel(loopEndBeats) + LayoutConfig::TIMELINE_LEFT_PADDING) : -1;
    auto loopTickColour = DarkTheme::getColour(
        loopInteraction_.isEnabled() ? DarkTheme::LOOP_MARKER : DarkTheme::TEXT_DISABLED);

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
            if (secondsDurationToPixels(interval) >= minPixelSpacing) {
                markerInterval = interval;
                break;
            }
        }

        // Compute visible time range from clip bounds to avoid iterating the entire timeline
        auto clipBounds = g.getClipBounds();
        double visibleStartTime = juce::jmax(
            0.0, static_cast<double>(clipBounds.getX() - LayoutConfig::TIMELINE_LEFT_PADDING) /
                     (tempoBPM / 60.0 * pixelsPerBeat));
        double visibleEndTime =
            juce::jmin(timelineLength, static_cast<double>(clipBounds.getRight() -
                                                           LayoutConfig::TIMELINE_LEFT_PADDING) /
                                           (tempoBPM / 60.0 * pixelsPerBeat));
        double startTime = std::floor(visibleStartTime / markerInterval) * markerInterval;
        double endTime = juce::jmin(timelineLength, visibleEndTime + markerInterval);

        // Draw ticks and labels
        for (double time = startTime; time <= endTime; time += markerInterval) {
            int x = beatsToPixel(secondsToBeats(time)) + LayoutConfig::TIMELINE_LEFT_PADDING;
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

                int tickHeight = isMajor ? majorTickHeight : minorTickHeight;

                // Draw tick — use loop color at loop boundaries
                bool atLoopBorder = hasLoop && (x == loopStartPx || x == loopEndPx);
                if (atLoopBorder) {
                    g.setColour(loopTickColour);
                    g.fillRect(x - 1, tickBottom - majorTickHeight, 2, majorTickHeight);
                } else {
                    g.setColour(DarkTheme::getColour(isMajor ? DarkTheme::TEXT_SECONDARY
                                                             : DarkTheme::TEXT_DIM));
                    g.drawLine(static_cast<float>(x), static_cast<float>(tickBottom - tickHeight),
                               static_cast<float>(x), static_cast<float>(tickBottom), 1.0f);
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
                               juce::Justification::centredTop);
                }
            }
        }
    } else {
        // ===== BARS/BEATS MODE =====
        // Everything in beats — zoom is pixels per beat (ppb)
        double markerIntervalBeats = GridConstants::computeGridInterval(
            gridQuantize, pixelsPerBeat, timeSignatureNumerator, minPixelSpacing);

        double barLengthBeats = static_cast<double>(timeSignatureNumerator);

        // Check if grid interval aligns with bar and beat boundaries
        bool alignsWithBars =
            GridConstants::gridAlignsWithBars(markerIntervalBeats, barLengthBeats);
        bool alignsWithBeats = GridConstants::gridAlignsWithBeats(markerIntervalBeats);
        bool gridAligned = alignsWithBars && alignsWithBeats;

        // Pixel spacings directly from zoom (no seconds conversion)
        double beatPixelSpacing = pixelsPerBeat;
        double pixelsPerBar = pixelsPerBeat * barLengthBeats;
        double pixelsPerSubdiv = pixelsPerBeat * markerIntervalBeats;

        // Determine bar label interval: show a label at every grid line that
        // falls on a bar boundary.  When the grid interval spans multiple bars
        // (e.g. 2 bars, 4 bars) we use that as the label interval so every
        // visible grid line gets a label.
        int gridBarMultiple = 1;
        if (markerIntervalBeats >= barLengthBeats)
            gridBarMultiple = static_cast<int>(std::round(markerIntervalBeats / barLengthBeats));

        int barLabelInterval = gridBarMultiple;
        // If individual bars are very narrow and the grid is at single-bar
        // resolution, thin out labels to avoid overlap
        if (gridBarMultiple <= 1) {
            if (pixelsPerBar < 20)
                barLabelInterval = 8;
            else if (pixelsPerBar < 30)
                barLabelInterval = 4;
            else if (pixelsPerBar < 40)
                barLabelInterval = 2;
        }

        // Compute visible beat range from clip bounds — native beats, no seconds
        auto clipBoundsRect = g.getClipBounds();
        double totalTimelineBeats = timelineLength * tempoBPM / 60.0;
        double visStartBeat = juce::jmax(
            0.0, static_cast<double>(clipBoundsRect.getX() - LayoutConfig::TIMELINE_LEFT_PADDING) /
                     pixelsPerBeat);
        double visEndBeat = juce::jmin(
            totalTimelineBeats,
            static_cast<double>(clipBoundsRect.getRight() - LayoutConfig::TIMELINE_LEFT_PADDING) /
                pixelsPerBeat);
        double startBeat = std::floor(visStartBeat / markerIntervalBeats) * markerIntervalBeats;
        double endBeat = juce::jmin(totalTimelineBeats, visEndBeat + markerIntervalBeats);

        // Pass 1: Draw grid ticks — iterate in beats
        for (double beat = startBeat; beat <= endBeat; beat += markerIntervalBeats) {
            int x = beatsToPixel(beat) + LayoutConfig::TIMELINE_LEFT_PADDING;
            if (x < 0 || x >= getWidth())
                continue;

            if (gridAligned) {
                // Grid aligns — classify and draw with hierarchy
                double totalBeats = beat;
                double beatInBarFractional = std::fmod(totalBeats, barLengthBeats);

                auto [isBarStart, isBeatStart] =
                    GridConstants::classifyBeatPosition(beatInBarFractional, barLengthBeats);

                int bar = static_cast<int>(totalBeats / timeSignatureNumerator) + 1;
                int beatInBar = static_cast<int>(beatInBarFractional) + 1;
                if (beatInBarFractional > (barLengthBeats - 0.001)) {
                    bar += 1;
                    beatInBar = 1;
                }

                bool isMajor = isBarStart;
                bool isMedium = !isBarStart && isBeatStart;
                int tickHeight = isMajor ? majorTickHeight
                                         : (isMedium ? (majorTickHeight * 2 / 3) : minorTickHeight);

                // Use loop color at loop boundaries
                bool atLoopBorder = hasLoop && (x == loopStartPx || x == loopEndPx);
                if (atLoopBorder) {
                    g.setColour(loopTickColour);
                    g.fillRect(x - 1, tickBottom - majorTickHeight, 2, majorTickHeight);
                } else {
                    if (isMajor) {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    } else if (isMedium) {
                        g.setColour(
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.7f));
                    } else {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                    }
                    g.drawLine(static_cast<float>(x), static_cast<float>(tickBottom - tickHeight),
                               static_cast<float>(x), static_cast<float>(tickBottom), 1.0f);
                }

                // Labels: bar.beat.16th — fixed 16th-note resolution
                // Only bars, beats, and 16th notes get labels. Finer ticks = no label.
                constexpr double k16th = 0.25;
                constexpr double eps = 0.001;
                double subdivInBeat = std::fmod(beatInBarFractional, 1.0);

                // Check if this tick falls on a 16th-note boundary
                double pos16th = subdivInBeat / k16th;
                int sixteenth = static_cast<int>(std::round(pos16th));
                bool isOn16th = std::abs(pos16th - sixteenth) < eps && sixteenth > 0;

                if (isBarStart && (bar - 1) % barLabelInterval == 0) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                    g.setFont(FontManager::getInstance().getUIFont(12.0f).boldened());
                    g.drawText(juce::String(bar), x - 35, labelY, 70, labelHeight,
                               juce::Justification::centredTop);
                } else if (isBeatStart && !isBarStart && beatPixelSpacing >= 50) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.setFont(FontManager::getInstance().getUIFont(10.0f));
                    g.drawText(juce::String(bar) + "." + juce::String(beatInBar), x - 25, labelY,
                               50, labelHeight, juce::Justification::centredTop);
                } else if (isOn16th && pixelsPerSubdiv >= 30) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                    g.setFont(FontManager::getInstance().getUIFont(8.0f));
                    g.drawText(juce::String(bar) + "." + juce::String(beatInBar) + "." +
                                   juce::String(sixteenth + 1),
                               x - 30, labelY + 2, 60, labelHeight,
                               juce::Justification::centredTop);
                }
                // Finer ticks (32nd, 64th, etc.) get tick marks but no labels
            } else {
                // Grid doesn't align with bars/beats — draw minor ticks only
                bool atLoopBorder = hasLoop && (x == loopStartPx || x == loopEndPx);
                if (atLoopBorder) {
                    g.setColour(loopTickColour);
                    g.fillRect(x - 1, tickBottom - majorTickHeight, 2, majorTickHeight);
                } else {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                    g.drawLine(static_cast<float>(x),
                               static_cast<float>(tickBottom - minorTickHeight),
                               static_cast<float>(x), static_cast<float>(tickBottom), 1.0f);
                }
            }
        }

        // Pass 2: For non-aligned grids, draw bar/beat reference ticks and labels on top
        if (!gridAligned) {
            double refStartBeat = std::floor(visStartBeat);
            double refEndBeat = std::ceil(visEndBeat);
            for (double beat = refStartBeat; beat <= refEndBeat; beat += 1.0) {
                int x = beatsToPixel(beat) + LayoutConfig::TIMELINE_LEFT_PADDING;
                if (x < 0 || x >= getWidth())
                    continue;

                double barRemainder = std::fmod(beat, barLengthBeats);
                bool isBarStart = barRemainder < 0.001;
                int bar = static_cast<int>(beat / timeSignatureNumerator) + 1;
                int beatInBar = static_cast<int>(barRemainder) + 1;

                bool atLoopBorder = hasLoop && (x == loopStartPx || x == loopEndPx);
                if (atLoopBorder) {
                    g.setColour(loopTickColour);
                    g.fillRect(x - 1, tickBottom - majorTickHeight, 2, majorTickHeight);
                } else if (isBarStart) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.drawLine(static_cast<float>(x),
                               static_cast<float>(tickBottom - majorTickHeight),
                               static_cast<float>(x), static_cast<float>(tickBottom), 1.0f);
                } else {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.7f));
                    int mediumTickH = majorTickHeight * 2 / 3;
                    g.drawLine(static_cast<float>(x), static_cast<float>(tickBottom - mediumTickH),
                               static_cast<float>(x), static_cast<float>(tickBottom), 1.0f);
                }
                // Labels
                if (isBarStart) {
                    if ((bar - 1) % barLabelInterval == 0) {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                        g.setFont(FontManager::getInstance().getUIFont(12.0f).boldened());
                        g.drawText(juce::String(bar), x - 35, labelY, 70, labelHeight,
                                   juce::Justification::centredTop);
                    }
                } else {
                    if (pixelsPerBeat >= 50) {
                        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                        g.setFont(FontManager::getInstance().getUIFont(10.0f));
                        g.drawText(juce::String(bar) + "." + juce::String(beatInBar), x - 25,
                                   labelY, 50, labelHeight, juce::Justification::centredTop);
                    }
                }
            }
        }
    }
}

void TimelineComponent::drawPlayhead(juce::Graphics& g) {
    int playheadX = beatsToPixel(playheadPositionBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;
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
    int startX = beatsToPixel(section.startBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = beatsToPixel(section.endBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;

    if (endX <= startX) {
        return;
    }

    // Draw section background using arrangement bar height from LayoutConfig
    auto& layout = LayoutConfig::getInstance();
    int chordHeight = layout.chordRowHeight;
    int arrangementHeight = layout.arrangementBarHeight;
    auto sectionArea = getVisibleRect(g, getWidth(), startX, endX, chordHeight, arrangementHeight);

    if (sectionArea.isEmpty())
        return;

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
        auto sectionBottom = static_cast<float>(sectionArea.getBottom());

        if (isXVisible(g, getWidth(), startX)) {
            g.drawDashedLine(juce::Line<float>(static_cast<float>(startX), sectionTop,
                                               static_cast<float>(startX), sectionBottom),
                             dashLengths, 2, 1.0f);
        }
        if (isXVisible(g, getWidth(), endX)) {
            g.drawDashedLine(juce::Line<float>(static_cast<float>(endX), sectionTop,
                                               static_cast<float>(endX), sectionBottom),
                             dashLengths, 2, 1.0f);
        }

        g.drawDashedLine(juce::Line<float>(static_cast<float>(sectionArea.getX()), sectionTop,
                                           static_cast<float>(sectionArea.getRight()), sectionTop),
                         dashLengths, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(static_cast<float>(sectionArea.getX()), sectionBottom,
                                           static_cast<float>(sectionArea.getRight()),
                                           sectionBottom),
                         dashLengths, 2, 1.0f);
    } else {
        g.setColour(isSelected ? section.colour.brighter(0.5f) : section.colour);
        g.drawRect(sectionArea, isSelected ? 2 : 1);
    }

    // Section name
    if (sectionArea.getWidth() > 40) {  // Only draw text if there's enough space
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

    double beats = pixelToBeats(x);
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& section = *sections[i];
        if (beats >= section.startBeats && beats <= section.endBeats) {
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
    int startX = beatsToPixel(section.startBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = beatsToPixel(section.endBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;

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

void TimelineComponent::setLoopRegionBeats(double startBeats, double endBeats) {
    double s = juce::jmax(0.0, startBeats);
    double e = juce::jmin(getTimelineLengthBeats(), endBeats);
    bool valid = (s >= 0 && e > s);
    loopInteraction_.setLoopRange(s, e, valid);

    if (onLoopRegionBeatsChanged) {
        onLoopRegionBeatsChanged(s, e);
    }

    repaint();
}

void TimelineComponent::clearLoopRegion() {
    loopInteraction_.setLoopRange(-1.0, -1.0, false);

    if (onLoopRegionBeatsChanged) {
        onLoopRegionBeatsChanged(-1.0, -1.0);
    }

    repaint();
}

void TimelineComponent::setLoopEnabled(bool enabled) {
    double s = loopInteraction_.getStartPosition();
    double e = loopInteraction_.getEndPosition();
    if (s >= 0 && e > s) {
        loopInteraction_.setLoopRange(s, e, enabled);
        repaint();
    }
}

void TimelineComponent::drawLoopMarkers(juce::Graphics& g) {
    // Draw background elements: shaded region and vertical lines
    // Time markers will be drawn on top of this
    double loopStartBeats = loopInteraction_.getStartPosition();
    double loopEndBeats = loopInteraction_.getEndPosition();
    bool loopEnabled = loopInteraction_.isEnabled();

    if (loopStartBeats < 0 || loopEndBeats <= loopStartBeats) {
        return;
    }

    // Get layout configuration - loop strip sits above the tick area
    auto& layout = LayoutConfig::getInstance();
    int rulerBottom = layout.chordRowHeight + layout.arrangementBarHeight + layout.timeRulerHeight;
    int tickAreaTop = rulerBottom - layout.rulerMajorTickHeight;
    static constexpr int LOOP_STRIP_HEIGHT = LayoutConfig::loopStripHeight;
    int stripTop = tickAreaTop - LOOP_STRIP_HEIGHT;

    int startX = beatsToPixel(loopStartBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = beatsToPixel(loopEndBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;

    auto loopStripArea = getVisibleRect(g, getWidth(), startX, endX, stripTop, LOOP_STRIP_HEIGHT);
    if (loopStripArea.isEmpty())
        return;

    // Use different colors based on enabled state
    juce::Colour regionColour =
        loopEnabled ? DarkTheme::getColour(DarkTheme::LOOP_REGION) : juce::Colour(0x15808080);

    // Draw shaded region in the loop strip area (above ticks)
    g.setColour(regionColour);
    g.fillRect(loopStripArea);

    // Draw vertical lines at loop boundaries in the strip area
    juce::Colour markerColour =
        loopEnabled ? DarkTheme::getColour(DarkTheme::LOOP_MARKER) : juce::Colour(0xFF606060);
    g.setColour(markerColour);
    if (isXVisible(g, getWidth(), startX)) {
        g.drawLine(static_cast<float>(startX), static_cast<float>(stripTop),
                   static_cast<float>(startX), static_cast<float>(stripTop + LOOP_STRIP_HEIGHT),
                   2.0f);
    }
    if (isXVisible(g, getWidth(), endX)) {
        g.drawLine(static_cast<float>(endX), static_cast<float>(stripTop), static_cast<float>(endX),
                   static_cast<float>(stripTop + LOOP_STRIP_HEIGHT), 2.0f);
    }
}

void TimelineComponent::drawLoopMarkerFlags(juce::Graphics& g) {
    // Draw loop strip at the very bottom of the ruler area
    double loopStartBeats = loopInteraction_.getStartPosition();
    double loopEndBeats = loopInteraction_.getEndPosition();
    bool loopEnabled = loopInteraction_.isEnabled();

    if (loopStartBeats < 0 || loopEndBeats <= loopStartBeats) {
        return;
    }

    // Get layout configuration — strip sits above the tick area
    auto& layout = LayoutConfig::getInstance();
    int rulerBottom = layout.chordRowHeight + layout.arrangementBarHeight + layout.timeRulerHeight;
    int tickAreaTop = rulerBottom - layout.rulerMajorTickHeight;
    static constexpr int LOOP_STRIP_HEIGHT = LayoutConfig::loopStripHeight;
    int stripTop = tickAreaTop - LOOP_STRIP_HEIGHT;

    int startX = beatsToPixel(loopStartBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = beatsToPixel(loopEndBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;

    auto loopStripArea = getVisibleRect(g, getWidth(), startX, endX, stripTop, LOOP_STRIP_HEIGHT);
    if (loopStripArea.isEmpty())
        return;

    // Use different colors based on enabled state
    juce::Colour markerColour =
        loopEnabled ? DarkTheme::getColour(DarkTheme::LOOP_MARKER) : juce::Colour(0xFF606060);

    // Fill the loop strip area with vertical gradient
    juce::Colour flagFill =
        loopEnabled ? DarkTheme::getColour(DarkTheme::LOOP_MARKER) : juce::Colour(0xFF808080);
    g.setGradientFill(juce::ColourGradient(
        flagFill.withAlpha(0.45f), 0.0f, static_cast<float>(stripTop), flagFill.withAlpha(0.1f),
        0.0f, static_cast<float>(stripTop + LOOP_STRIP_HEIGHT), false));
    g.fillRect(loopStripArea);

    // Connecting lines at top and bottom of strip
    g.setColour(markerColour);
    g.fillRect(loopStripArea.getX(), stripTop, loopStripArea.getWidth(), 2);
    g.fillRect(loopStripArea.getX(), stripTop + LOOP_STRIP_HEIGHT - 1, loopStripArea.getWidth(), 1);

    // 2px vertical marker lines spanning the strip
    if (isXVisible(g, getWidth(), startX)) {
        g.fillRect(startX - 1, stripTop, 2, LOOP_STRIP_HEIGHT);
    }
    if (isXVisible(g, getWidth(), endX)) {
        g.fillRect(endX - 1, stripTop, 2, LOOP_STRIP_HEIGHT);
    }

    // Triangular flags
    int flagTop = stripTop + 1;
    int loopPixelWidth = endX - startX;
    int maxFlagW = juce::jmax(4, loopPixelWidth / 2);
    int flagH = juce::jlimit(6, LOOP_STRIP_HEIGHT - 2, maxFlagW);
    int flagW = juce::jlimit(4, 8, maxFlagW);

    g.setColour(markerColour);
    if (isXVisible(g, getWidth(), startX)) {
        juce::Path startFlag;
        startFlag.addTriangle(static_cast<float>(startX), static_cast<float>(flagTop),
                              static_cast<float>(startX), static_cast<float>(flagTop + flagH),
                              static_cast<float>(startX + flagW),
                              static_cast<float>(flagTop + flagH / 2));
        g.fillPath(startFlag);
    }

    if (isXVisible(g, getWidth(), endX)) {
        juce::Path endFlag;
        endFlag.addTriangle(static_cast<float>(endX), static_cast<float>(flagTop),
                            static_cast<float>(endX), static_cast<float>(flagTop + flagH),
                            static_cast<float>(endX - flagW),
                            static_cast<float>(flagTop + flagH / 2));
        g.fillPath(endFlag);
    }
}

void TimelineComponent::initLoopInteraction() {
    auto& layout = LayoutConfig::getInstance();

    LoopMarkerInteraction::Host host;
    host.pixelToPosition = [this](int pixel) { return pixelToBeats(pixel); };
    host.positionToPixel = [this](double beats) {
        return beatsToPixel(beats) + LayoutConfig::TIMELINE_LEFT_PADDING;
    };
    host.snapPosition = [this](double beats) -> double {
        if (snapEnabled)
            return snapBeatsToGrid(beats);
        return beats;
    };
    host.onLoopChanged = [this](double start, double end) {
        if (onLoopRegionBeatsChanged)
            onLoopRegionBeatsChanged(start, end);
    };
    host.onRepaint = [this]() { repaint(); };
    host.maxPosition = getTimelineLengthBeats();
    int rulerBottom = layout.chordRowHeight + layout.arrangementBarHeight + layout.timeRulerHeight;
    int tickAreaTop = rulerBottom - layout.rulerMajorTickHeight;
    host.topBorderY = tickAreaTop - LayoutConfig::loopStripHeight;
    host.topBorderThreshold = LayoutConfig::loopStripHeight;
    loopInteraction_.setHost(std::move(host));
}

double TimelineComponent::getSnapInterval() const {
    // If grid override is active, return the fixed interval
    if (!gridQuantize.autoGrid) {
        double secondsPerBeat = 60.0 / tempoBPM;
        double beatFraction = gridQuantize.toBeatFraction();
        return secondsPerBeat * beatFraction;
    }

    // Get the visible snap interval based on zoom level and display mode
    auto& layout = LayoutConfig::getInstance();
    const int minPixelSpacing = layout.minGridPixelSpacing;

    if (displayMode == TimeDisplayMode::Seconds) {
        // Seconds mode - snap to time divisions
        const double intervals[] = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1,  0.2, 0.25,
                                    0.5,   1.0,   2.0,   5.0,  10.0, 15.0, 30.0, 60.0};

        for (double interval : intervals) {
            if (secondsDurationToPixels(interval) >= minPixelSpacing) {
                return interval;
            }
        }
        return 1.0;  // Default to 1 second
    } else {
        // Bars/beats mode - find first power-of-2 beat fraction that fits
        // zoom is in pixels per beat
        double secondsPerBeat = 60.0 / tempoBPM;

        double frac = GridConstants::findBeatSubdivision(pixelsPerBeat, minPixelSpacing);
        if (frac > 0) {
            return secondsPerBeat * frac;
        }

        // Fall back to bar multiples
        int mult =
            GridConstants::findBarMultiple(pixelsPerBeat, timeSignatureNumerator, minPixelSpacing);
        return secondsPerBeat * timeSignatureNumerator * mult;
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

void TimelineComponent::setTimeSelectionBeats(double startBeats, double endBeats) {
    timeSelectionStartBeats = startBeats;
    timeSelectionEndBeats = endBeats;
    repaint();
}

void TimelineComponent::clearTimeSelection() {
    timeSelectionStartBeats = -1.0;
    timeSelectionEndBeats = -1.0;
    repaint();
}

void TimelineComponent::drawTimeSelection(juce::Graphics& g) {
    if (timeSelectionStartBeats < 0 || timeSelectionEndBeats < 0 ||
        timeSelectionEndBeats <= timeSelectionStartBeats) {
        return;
    }

    int startX = beatsToPixel(timeSelectionStartBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = beatsToPixel(timeSelectionEndBeats) + LayoutConfig::TIMELINE_LEFT_PADDING;

    // Selection only in content area (below the ruler)
    auto& layout = LayoutConfig::getInstance();
    int rulerBottom = layout.chordRowHeight + layout.arrangementBarHeight + layout.timeRulerHeight;
    auto selectionArea =
        getVisibleRect(g, getWidth(), startX, endX, rulerBottom, getHeight() - rulerBottom);

    if (selectionArea.isEmpty())
        return;

    // Draw selection highlight covering content area only
    g.setColour(DarkTheme::getColour(DarkTheme::TIME_SELECTION));
    g.fillRect(selectionArea);

    // Draw selection edges
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
    if (isXVisible(g, getWidth(), startX)) {
        g.drawLine(static_cast<float>(startX), static_cast<float>(rulerBottom),
                   static_cast<float>(startX), static_cast<float>(getHeight()), 1.0f);
    }
    if (isXVisible(g, getWidth(), endX)) {
        g.drawLine(static_cast<float>(endX), static_cast<float>(rulerBottom),
                   static_cast<float>(endX), static_cast<float>(getHeight()), 1.0f);
    }
}

}  // namespace magda
