#include "TimeRuler.hpp"

#include <cmath>

#include "CursorManager.hpp"
#include "DarkTheme.hpp"
#include "FontManager.hpp"
#include "LayoutConfig.hpp"
#include "TimelineState.hpp"

namespace magda {

TimeRuler::TimeRuler() {
    setOpaque(true);
}

TimeRuler::~TimeRuler() {
    stopTimer();
}

void TimeRuler::paint(juce::Graphics& g) {
    // Background
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));

    int height = getHeight();
    int tickAreaTop = height - TICK_HEIGHT_MAJOR;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));

    // Border line above ticks
    g.fillRect(0, tickAreaTop, getWidth(), 1);

    // Bottom border line
    g.fillRect(0, height - 1, getWidth(), 1);

    // Draw based on mode
    if (displayMode == DisplayMode::Seconds) {
        drawSecondsMode(g);
    } else {
        drawBarsBeatsMode(g);
    }
}

void TimeRuler::resized() {
    initLoopInteraction();
}

void TimeRuler::setZoom(double pixelsPerBeat) {
    zoom = pixelsPerBeat;
    repaint();
}

void TimeRuler::setTimelineLength(double lengthInSeconds) {
    timelineLength = lengthInSeconds;
    repaint();
}

void TimeRuler::setDisplayMode(DisplayMode mode) {
    displayMode = mode;
    repaint();
}

void TimeRuler::setScrollOffset(int offsetPixels) {
    scrollOffset = offsetPixels;
    repaint();
}

void TimeRuler::setTempo(double bpm) {
    // No repaint — callers (e.g. WaveformEditorContent) already repaint
    // after adjusting zoom/scroll in response to tempo changes.
    tempo = bpm;
}

void TimeRuler::setTimeSignature(int numerator, int denominator) {
    timeSigNumerator = numerator;
    timeSigDenominator = denominator;
    repaint();
}

void TimeRuler::setGridResolution(double beatsPerGridLine) {
    gridResolutionBeats = beatsPerGridLine;
    repaint();
}

void TimeRuler::setTimeOffset(double offsetSeconds) {
    timeOffset = offsetSeconds;
    repaint();
}

void TimeRuler::setBarOrigin(double originSeconds) {
    if (barOriginSeconds != originSeconds) {
        barOriginSeconds = originSeconds;
        repaint();
    }
}

void TimeRuler::setRelativeMode(bool relative) {
    relativeMode = relative;
    repaint();
}

void TimeRuler::setClipLength(double lengthSeconds) {
    clipLength = lengthSeconds;
    repaint();
}

void TimeRuler::setClipContentOffset(double offsetSeconds) {
    clipContentOffset = offsetSeconds;
    repaint();
}

void TimeRuler::setPlayheadPosition(double positionSeconds) {
    if (playheadPosition != positionSeconds) {
        playheadPosition = positionSeconds;
        repaint();
    }
}

void TimeRuler::setEditCursorPosition(double positionSeconds, bool blinkVisible) {
    editCursorPosition_ = positionSeconds;
    editCursorVisible_ = blinkVisible;
    repaint();
}

void TimeRuler::setLeftPadding(int padding) {
    leftPadding = padding;
    repaint();
}

void TimeRuler::setLoopRegion(double offsetSeconds, double lengthSeconds, bool enabled,
                              bool active) {
    loopOffset = offsetSeconds;
    loopLength = lengthSeconds;
    loopEnabled = enabled;
    loopActive = active;

    // Sync loop interaction helper with absolute pixel coordinates
    // Don't reset during an active drag — it causes feedback loops
    if (!loopInteraction_.isDragging()) {
        if (enabled && lengthSeconds > 0.0) {
            double loopStartTime = relativeMode ? loopOffset : (timeOffset + loopOffset);
            double loopEndTime = loopStartTime + loopLength;
            loopInteraction_.setLoopRegion(loopStartTime, loopEndTime, true);
            initLoopInteraction();
        } else {
            loopInteraction_.setLoopRegion(-1.0, -1.0, false);
        }
    }

    repaint();
}

void TimeRuler::setLoopPhaseMarker(double positionSeconds, bool visible) {
    loopPhasePosition = positionSeconds;
    loopPhaseVisible = visible;
    loopPhaseHoverOnly = !visible && loopEnabled && positionSeconds == 0.0;
    repaint();
}

void TimeRuler::setLinkedViewport(juce::Viewport* viewport) {
    linkedViewport = viewport;
    if (linkedViewport) {
        // Start timer for real-time scroll sync (60fps)
        startTimerHz(60);
        lastViewportX = linkedViewport->getViewPositionX();
    } else {
        stopTimer();
    }
}

void TimeRuler::timerCallback() {
    if (linkedViewport) {
        int currentX = linkedViewport->getViewPositionX();
        if (currentX != lastViewportX) {
            lastViewportX = currentX;
            repaint();
        }
    }
}

int TimeRuler::getPreferredHeight() const {
    return LayoutConfig::getInstance().timeRulerHeight;
}

void TimeRuler::mouseDown(const juce::MouseEvent& event) {
    // Alt+click near phase marker → start phase drag
    if (event.mods.isAltDown() && loopEnabled && (loopPhaseVisible || loopPhaseHoverOnly)) {
        double phaseTime = relativeMode ? loopPhasePosition : (timeOffset + loopPhasePosition);
        int phaseX = timeToPixel(phaseTime);
        if (std::abs(event.x - phaseX) <= 8) {
            dragMode = DragMode::PhaseDrag;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    // Try loop marker interaction first
    if (loopInteraction_.mouseDown(event.x, event.y))
        return;

    mouseDownX = event.x;
    mouseDownY = event.y;
    lastDragX = event.x;
    zoomStartValue = zoom;
    dragMode = DragMode::None;

    // Capture anchor time at mouse position
    zoomAnchorTime = pixelToTime(event.x);
    zoomAnchorTime = juce::jlimit(0.0, timelineLength, zoomAnchorTime);
}

void TimeRuler::mouseDrag(const juce::MouseEvent& event) {
    // Phase marker drag
    if (dragMode == DragMode::PhaseDrag) {
        double newPhaseTime = pixelToTime(event.x);
        // Convert to relative time if needed
        if (!relativeMode) {
            newPhaseTime -= timeOffset;
        }
        newPhaseTime = juce::jmax(0.0, newPhaseTime);
        // Clamp to loop length
        if (loopLength > 0.0) {
            newPhaseTime = juce::jmin(newPhaseTime, loopOffset + loopLength);
        }
        if (onPhaseMarkerChanged) {
            onPhaseMarkerChanged(newPhaseTime);
        }
        return;
    }

    // Try loop marker interaction first
    if (loopInteraction_.mouseDrag(event.x, event.y))
        return;

    int deltaX = std::abs(event.x - mouseDownX);
    int deltaY = std::abs(event.y - mouseDownY);

    // Determine drag mode if not yet set
    if (dragMode == DragMode::None) {
        if (deltaX > DRAG_THRESHOLD || deltaY > DRAG_THRESHOLD) {
            // Horizontal drag = scroll, vertical drag = zoom
            dragMode = (deltaX > deltaY) ? DragMode::Scrolling : DragMode::Zooming;
        }
    }

    if (dragMode == DragMode::Zooming) {
        // Drag up = zoom in, drag down = zoom out
        int yDelta = mouseDownY - event.y;

        // Exponential zoom for smooth feel
        double sensitivity = 30.0;  // pixels to double/halve zoom
        double exponent = static_cast<double>(yDelta) / sensitivity;
        double newZoom = zoomStartValue * std::pow(2.0, exponent);

        // Clamp zoom to reasonable limits (pixels per beat)
        newZoom = juce::jlimit(1.0, 50000.0, newZoom);

        if (yDelta > 0) {
            setMouseCursor(CursorManager::getInstance().getZoomInCursor());
        } else if (yDelta < 0) {
            setMouseCursor(CursorManager::getInstance().getZoomOutCursor());
        }

        if (onZoomChanged) {
            onZoomChanged(newZoom, zoomAnchorTime, mouseDownX);
        }
    } else if (dragMode == DragMode::Scrolling) {
        // Calculate scroll delta (inverted - drag right scrolls left)
        int scrollDelta = lastDragX - event.x;
        lastDragX = event.x;

        if (onScrollRequested && scrollDelta != 0) {
            onScrollRequested(scrollDelta);
        }
    }
}

void TimeRuler::mouseUp(const juce::MouseEvent& event) {
    if (dragMode == DragMode::PhaseDrag) {
        if (onPhaseDragEnded) {
            double finalPhaseTime = pixelToTime(event.x);
            if (!relativeMode)
                finalPhaseTime -= timeOffset;
            finalPhaseTime = juce::jmax(0.0, finalPhaseTime);
            if (loopLength > 0.0)
                finalPhaseTime = juce::jmin(finalPhaseTime, loopOffset + loopLength);
            onPhaseDragEnded(finalPhaseTime);
        }
        dragMode = DragMode::None;
        setMouseCursor(CursorManager::getInstance().getZoomCursor());
        return;
    }

    // Complete loop marker interaction
    if (loopInteraction_.mouseUp(event.x, event.y)) {
        if (onLoopDragEnded)
            onLoopDragEnded(loopInteraction_.getStartTime(), loopInteraction_.getEndTime());
        return;
    }

    // If it was a click (not a drag), handle playhead positioning
    if (dragMode == DragMode::None) {
        int deltaX = std::abs(event.x - mouseDownX);
        int deltaY = std::abs(event.y - mouseDownY);

        if (deltaX <= DRAG_THRESHOLD && deltaY <= DRAG_THRESHOLD) {
            if (onPositionClicked) {
                double time = pixelToTime(event.x);
                if (time >= 0.0 && time <= timelineLength) {
                    onPositionClicked(time);
                }
            }
        }
    }

    dragMode = DragMode::None;
    setMouseCursor(CursorManager::getInstance().getZoomCursor());
}

void TimeRuler::mouseDoubleClick(const juce::MouseEvent& event) {
    // Double-click on loop strip → zoom to loop region
    if (loopEnabled && loopLength > 0.0 && onZoomToLoopRequested) {
        double loopStartTime = relativeMode ? loopOffset : (timeOffset + loopOffset);
        double loopEndTime = loopStartTime + loopLength;

        int loopStartX = timeToPixel(loopStartTime);
        int loopEndX = timeToPixel(loopEndTime);

        // Hit-test: click is within the loop strip area only (both X and Y)
        const int height = getHeight();
        const int loopStripTop = height - TICK_HEIGHT_MAJOR - LOOP_STRIP_HEIGHT;
        const int loopStripBottom = height - TICK_HEIGHT_MAJOR;

        if (event.x >= loopStartX && event.x <= loopEndX && event.y >= loopStripTop &&
            event.y <= loopStripBottom) {
            onZoomToLoopRequested(loopStartTime, loopEndTime);
            return;
        }
    }

    // Fall through: treat as click for playhead positioning
    if (onPositionClicked) {
        double time = pixelToTime(event.x);
        if (time >= 0.0 && time <= timelineLength) {
            onPositionClicked(time);
        }
    }
}

void TimeRuler::mouseMove(const juce::MouseEvent& event) {
    // Alt+hover near phase marker shows resize cursor
    if (event.mods.isAltDown() && loopEnabled && (loopPhaseVisible || loopPhaseHoverOnly)) {
        double phaseTime = relativeMode ? loopPhasePosition : (timeOffset + loopPhasePosition);
        int phaseX = timeToPixel(phaseTime);
        if (std::abs(event.x - phaseX) <= 8) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            // Still update hover state below
        } else {
            auto loopCursor = loopInteraction_.getCursor(event.x, event.y);
            setMouseCursor(loopCursor != juce::MouseCursor::NormalCursor
                               ? loopCursor
                               : CursorManager::getInstance().getZoomCursor());
        }
    } else {
        auto loopCursor = loopInteraction_.getCursor(event.x, event.y);
        if (loopCursor != juce::MouseCursor::NormalCursor) {
            setMouseCursor(loopCursor);
        } else {
            setMouseCursor(CursorManager::getInstance().getZoomCursor());
        }
    }

    // Check proximity to phase marker for hover display
    if (loopPhaseHoverOnly) {
        double phaseTime = relativeMode ? loopPhasePosition : (timeOffset + loopPhasePosition);
        int phaseX = timeToPixel(phaseTime);
        bool wasHovered = loopPhaseHovered;
        loopPhaseHovered = std::abs(event.x - phaseX) <= 8;
        if (loopPhaseHovered != wasHovered) {
            repaint();
        }
    }
}

void TimeRuler::mouseExit(const juce::MouseEvent& /*event*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (loopPhaseHovered) {
        loopPhaseHovered = false;
        repaint();
    }
}

void TimeRuler::mouseWheelMove(const juce::MouseEvent& /*event*/,
                               const juce::MouseWheelDetails& wheel) {
    // Scroll horizontally when wheel is used over the ruler
    if (onScrollRequested) {
        // Use deltaX if available (trackpad horizontal swipe), otherwise use deltaY (mouse wheel)
        float delta = (wheel.deltaX != 0.0f) ? wheel.deltaX : wheel.deltaY;
        int scrollAmount = static_cast<int>(-delta * 100.0f);
        if (scrollAmount != 0) {
            onScrollRequested(scrollAmount);
        }
    }
}

void TimeRuler::drawSecondsMode(juce::Graphics& g) {
    const int height = getHeight();
    const int width = getWidth();
    int tickBottom = height;

    // Calculate marker interval based on zoom
    double interval = calculateMarkerInterval();

    // Find first visible time
    double startTime = pixelToTime(0);
    startTime = std::floor(startTime / interval) * interval;
    if (startTime < 0)
        startTime = 0;

    // Draw markers
    g.setFont(11.0f);

    for (double time = startTime; time <= timelineLength; time += interval) {
        int x = timeToPixel(time);

        if (x < 0)
            continue;
        if (x > width)
            break;

        // Determine if this is a major marker (every 5 intervals or at round numbers)
        bool isMajor = std::fmod(time, interval * 5) < 0.001 || std::fmod(time, 1.0) < 0.001;

        int tickHeight = isMajor ? TICK_HEIGHT_MAJOR : TICK_HEIGHT_MINOR;

        // Draw tick
        g.setColour(
            DarkTheme::getColour(isMajor ? DarkTheme::TEXT_SECONDARY : DarkTheme::TEXT_DIM));
        g.drawVerticalLine(x, static_cast<float>(tickBottom - tickHeight),
                           static_cast<float>(tickBottom));

        // Draw label for major ticks
        if (isMajor) {
            bool hasLoop = loopEnabled && loopLength > 0.0;
            int loopSpace = hasLoop ? LOOP_STRIP_HEIGHT : 0;
            int lblBottom = tickBottom - TICK_HEIGHT_MAJOR - loopSpace;
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            juce::String label = formatTimeLabel(time, interval);
            g.drawText(label, x - 30, 1, 60, lblBottom - 1, juce::Justification::centredTop, false);
        }
    }
}

void TimeRuler::drawBarsBeatsMode(juce::Graphics& g) {
    const int height = getHeight();
    const int width = getWidth();

    // Use grid resolution if provided, otherwise compute from zoom level
    double intervalBeats = 1.0;

    if (gridResolutionBeats > 0.0) {
        intervalBeats = gridResolutionBeats;
    } else {
        // Auto-compute from zoom (same logic as TimelineComponent)
        const double beatFractions[] = {0.0078125, 0.015625, 0.03125, 0.0625,
                                        0.125,     0.25,     0.5,     1.0};
        const int barMultiples[] = {1, 2, 4, 8, 16, 32};
        const int minPixelSpacing = 12;
        bool foundInterval = false;

        for (double fraction : beatFractions) {
            if (fraction * zoom >= minPixelSpacing) {
                intervalBeats = fraction;
                foundInterval = true;
                break;
            }
        }

        if (!foundInterval) {
            for (int mult : barMultiples) {
                if (static_cast<double>(timeSigNumerator * mult) * zoom >= minPixelSpacing) {
                    intervalBeats = timeSigNumerator * mult;
                    break;
                }
            }
        }
    }

    double pixelsPerBeat = zoom;
    double pixelsPerBar = zoom * timeSigNumerator;
    double pixelsPerSubdiv = intervalBeats * zoom;
    double barLengthBeats = static_cast<double>(timeSigNumerator);

    // Check if grid interval aligns with bar and beat boundaries
    bool alignsWithBars = GridConstants::gridAlignsWithBars(intervalBeats, barLengthBeats);
    bool alignsWithBeats = GridConstants::gridAlignsWithBeats(intervalBeats);
    bool gridAligned = alignsWithBars && alignsWithBeats;

    int tickBottom = height;
    bool hasLoopStrip = loopEnabled && loopLength > 0.0;
    int loopStripSpace = hasLoopStrip ? LOOP_STRIP_HEIGHT : 0;
    int labelY = 1;
    int labelBottom = tickBottom - TICK_HEIGHT_MAJOR - loopStripSpace;
    int labelHeight = labelBottom - labelY;
    int mediumTickHeight = TICK_HEIGHT_MAJOR * 2 / 3;

    // Determine bar label interval (show every Nth bar when zoomed out)
    int barLabelInterval = 1;
    if (pixelsPerBar < 40)
        barLabelInterval = 8;
    else if (pixelsPerBar < 60)
        barLabelInterval = 4;
    else if (pixelsPerBar < 90)
        barLabelInterval = 2;

    int currentScrollOffset = linkedViewport ? linkedViewport->getViewPositionX() : scrollOffset;
    double barOriginBeats = barOriginSeconds * tempo / 60.0;

    double firstVisibleBeat = (currentScrollOffset - leftPadding) / zoom + barOriginBeats;
    if (firstVisibleBeat < barOriginBeats)
        firstVisibleBeat = barOriginBeats;

    long long startStep =
        static_cast<long long>(std::floor((firstVisibleBeat - barOriginBeats) / intervalBeats));
    if (startStep < 0)
        startStep = 0;

    double totalTimelineBeats = timelineLength * tempo / 60.0;

    // Pass 1: Draw grid ticks
    for (long long step = startStep;; ++step) {
        double beat = barOriginBeats + step * intervalBeats;
        if (beat > barOriginBeats + totalTimelineBeats)
            break;

        int x = static_cast<int>(beat * zoom) - currentScrollOffset + leftPadding;
        if (x > width)
            break;
        if (x < 0)
            continue;

        if (gridAligned) {
            // Grid aligns with musical structure — classify normally
            double beatsFromOrigin = step * intervalBeats;
            int bar = static_cast<int>(beatsFromOrigin / timeSigNumerator) + 1;
            double beatsInBar = std::fmod(beatsFromOrigin, barLengthBeats);
            if (beatsInBar < 0)
                beatsInBar += timeSigNumerator;

            auto [isBarStart, isBeatStart] =
                GridConstants::classifyBeatPosition(beatsFromOrigin, barLengthBeats);

            if (beatsInBar > (barLengthBeats - 0.001))
                bar += 1;

            int beatInBar = isBarStart ? 1 : (static_cast<int>(beatsInBar) + 1);

            // Tick drawing
            if (isBarStart) {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawVerticalLine(x, static_cast<float>(tickBottom - TICK_HEIGHT_MAJOR),
                                   static_cast<float>(tickBottom));
            } else if (isBeatStart) {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.7f));
                g.drawVerticalLine(x, static_cast<float>(tickBottom - mediumTickHeight),
                                   static_cast<float>(tickBottom));
            } else {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                g.drawVerticalLine(x, static_cast<float>(tickBottom - TICK_HEIGHT_MINOR),
                                   static_cast<float>(tickBottom));
            }

            // Label drawing
            double subdivInBeat = std::fmod(beatsInBar, 1.0);
            int subdivsPerBeat = std::max(1, static_cast<int>(std::round(1.0 / intervalBeats)));
            int subdivIndex = static_cast<int>(std::round(subdivInBeat * subdivsPerBeat)) + 1;
            bool isPow2Subdiv = subdivsPerBeat > 0 && (subdivsPerBeat & (subdivsPerBeat - 1)) == 0;
            bool isSubdivisionNotBeat = !isBeatStart && subdivIndex > 1 && isPow2Subdiv;

            if (isBarStart && (bar - 1) % barLabelInterval == 0) {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                g.setFont(FontManager::getInstance().getUIFont(12.0f).boldened());
                g.drawText(juce::String(bar), x - 35, labelY, 70, labelHeight,
                           juce::Justification::centred);
            } else if (isBeatStart && !isBarStart && pixelsPerBeat >= 30) {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.setFont(FontManager::getInstance().getUIFont(10.0f));
                g.drawText(juce::String(bar) + "." + juce::String(beatInBar), x - 25, labelY, 50,
                           labelHeight, juce::Justification::centred);
            } else if (isSubdivisionNotBeat && pixelsPerSubdiv >= 18) {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
                g.setFont(FontManager::getInstance().getUIFont(8.0f));
                g.drawText(juce::String(bar) + "." + juce::String(beatInBar) + "." +
                               juce::String(subdivIndex),
                           x - 30, labelY, 60, labelHeight, juce::Justification::centred);
            }
        } else {
            // Grid doesn't align — draw minor ticks only
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
            g.drawVerticalLine(x, static_cast<float>(tickBottom - TICK_HEIGHT_MINOR),
                               static_cast<float>(tickBottom));
        }
    }

    // Pass 2: For non-aligned grids, draw bar/beat reference ticks and labels on top
    if (!gridAligned) {
        // Find first visible beat boundary
        long long startBeatStep =
            static_cast<long long>(std::floor((firstVisibleBeat - barOriginBeats)));
        if (startBeatStep < 0)
            startBeatStep = 0;

        for (long long beatStep = startBeatStep;; ++beatStep) {
            double beat = barOriginBeats + static_cast<double>(beatStep);
            if (beat > barOriginBeats + totalTimelineBeats)
                break;

            int x = static_cast<int>(beat * zoom) - currentScrollOffset + leftPadding;
            if (x > width)
                break;
            if (x < 0)
                continue;

            double beatsFromOrigin = static_cast<double>(beatStep);
            double barRemainder = std::fmod(beatsFromOrigin, barLengthBeats);
            bool isBarStart = barRemainder < 0.001;
            int bar = static_cast<int>(beatsFromOrigin / timeSigNumerator) + 1;
            int beatInBar = static_cast<int>(barRemainder) + 1;

            if (isBarStart) {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawVerticalLine(x, static_cast<float>(tickBottom - TICK_HEIGHT_MAJOR),
                                   static_cast<float>(tickBottom));
                if ((bar - 1) % barLabelInterval == 0) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                    g.setFont(FontManager::getInstance().getUIFont(12.0f).boldened());
                    g.drawText(juce::String(bar), x - 35, labelY, 70, labelHeight,
                               juce::Justification::centred);
                }
            } else {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.7f));
                g.drawVerticalLine(x, static_cast<float>(tickBottom - mediumTickHeight),
                                   static_cast<float>(tickBottom));
                if (pixelsPerBeat >= 30) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.setFont(FontManager::getInstance().getUIFont(10.0f));
                    g.drawText(juce::String(bar) + "." + juce::String(beatInBar), x - 25, labelY,
                               50, labelHeight, juce::Justification::centred);
                }
            }
        }
    }

    // Draw clip boundary markers (shifted by content offset in source file)
    // In loop mode, hide clip boundary markers (arrangement length is irrelevant in source editor)
    if (clipLength > 0) {
        if (!loopActive) {
            if (!relativeMode) {
                int clipStartX = timeToPixel(timeOffset + clipContentOffset);
                if (clipStartX >= 0 && clipStartX <= width) {
                    g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
                    g.fillRect(clipStartX - 1, 0, 2, height);
                }

                int clipEndX = timeToPixel(timeOffset + clipContentOffset + clipLength);
                if (clipEndX >= 0 && clipEndX <= width) {
                    g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
                    g.fillRect(clipEndX - 1, 0, 3, height);
                }
            } else {
                int clipStartX = timeToPixel(clipContentOffset);
                if (clipStartX >= 0 && clipStartX <= width) {
                    g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
                    g.fillRect(clipStartX - 1, 0, 2, height);
                }

                int clipEndX = timeToPixel(clipContentOffset + clipLength);
                if (clipEndX >= 0 && clipEndX <= width) {
                    g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
                    g.fillRect(clipEndX - 1, 0, 3, height);
                }
            }
        }
    }

    // Draw loop region strip between labels and ticks
    if (loopEnabled && loopLength > 0.0) {
        double loopStartTime = relativeMode ? loopOffset : (timeOffset + loopOffset);
        double loopEndTime = loopStartTime + loopLength;

        int loopStartX = timeToPixel(loopStartTime);
        int loopEndX = timeToPixel(loopEndTime);

        auto markerColour = loopActive ? DarkTheme::getColour(DarkTheme::LOOP_MARKER)
                                       : DarkTheme::getColour(DarkTheme::TEXT_DISABLED);

        if (loopEndX >= 0 && loopStartX <= width) {
            int tickAreaTop = height - TICK_HEIGHT_MAJOR;
            int stripTop = tickAreaTop - LOOP_STRIP_HEIGHT;

            // Fill the loop strip region with vertical gradient (above tick area)
            auto flagFill = loopActive ? DarkTheme::getColour(DarkTheme::LOOP_MARKER)
                                       : juce::Colour(0xFF808080);
            g.setGradientFill(
                juce::ColourGradient(flagFill.withAlpha(0.45f), 0.0f, static_cast<float>(stripTop),
                                     flagFill.withAlpha(0.1f), 0.0f,
                                     static_cast<float>(stripTop + LOOP_STRIP_HEIGHT), false));
            g.fillRect(loopStartX, stripTop, loopEndX - loopStartX, LOOP_STRIP_HEIGHT);

            // Connecting lines at top and bottom of strip
            g.setColour(markerColour.withAlpha(loopActive ? 1.0f : 0.5f));
            g.fillRect(loopStartX, stripTop, loopEndX - loopStartX, 2);
            g.fillRect(loopStartX, stripTop + LOOP_STRIP_HEIGHT - 1, loopEndX - loopStartX, 1);

            // 2px vertical marker lines spanning the strip
            if (loopStartX >= 0 && loopStartX <= width) {
                g.fillRect(loopStartX - 1, stripTop, 2, LOOP_STRIP_HEIGHT);
            }
            if (loopEndX >= 0 && loopEndX <= width) {
                g.fillRect(loopEndX - 1, stripTop, 2, LOOP_STRIP_HEIGHT);
            }

            // Border ticks extending into the tick area
            if (loopStartX >= 0 && loopStartX <= width) {
                g.fillRect(loopStartX - 1, tickAreaTop, 2, TICK_HEIGHT_MAJOR);
            }
            if (loopEndX >= 0 && loopEndX <= width) {
                g.fillRect(loopEndX - 1, tickAreaTop, 2, TICK_HEIGHT_MAJOR);
            }

            // Triangular flags — size adapts to zoom
            int flagTop = stripTop + 1;
            int loopPixelWidth = loopEndX - loopStartX;
            int maxFlagW = juce::jmax(4, loopPixelWidth / 2);
            int flagH = juce::jlimit(6, LOOP_STRIP_HEIGHT - 2, maxFlagW);
            int flagW = juce::jlimit(4, 8, maxFlagW);

            g.setColour(markerColour.withAlpha(loopActive ? 1.0f : 0.5f));
            if (loopStartX >= 0 && loopStartX <= width) {
                juce::Path startFlag;
                startFlag.addTriangle(static_cast<float>(loopStartX), static_cast<float>(flagTop),
                                      static_cast<float>(loopStartX),
                                      static_cast<float>(flagTop + flagH),
                                      static_cast<float>(loopStartX + flagW),
                                      static_cast<float>(flagTop + flagH / 2));
                g.fillPath(startFlag);
            }
            if (loopEndX >= 0 && loopEndX <= width) {
                juce::Path endFlag;
                endFlag.addTriangle(
                    static_cast<float>(loopEndX), static_cast<float>(flagTop),
                    static_cast<float>(loopEndX), static_cast<float>(flagTop + flagH),
                    static_cast<float>(loopEndX - flagW), static_cast<float>(flagTop + flagH / 2));
                g.fillPath(endFlag);
            }
        }
    }

    // Draw loop phase marker (yellow)
    if ((loopPhaseVisible || loopPhaseHovered) && loopEnabled) {
        double phaseTime = relativeMode ? loopPhasePosition : (timeOffset + loopPhasePosition);
        int phaseX = timeToPixel(phaseTime);
        if (phaseX >= 0 && phaseX <= width) {
            auto col = juce::Colour(0xFFCCAA44);  // OFFSET_MARKER yellow
            float alpha = loopPhaseVisible ? 1.0f : 0.4f;
            int tickAreaTop = height - TICK_HEIGHT_MAJOR;
            g.setColour(col.withAlpha(alpha));
            g.fillRect(phaseX - 1, tickAreaTop, 2, TICK_HEIGHT_MAJOR);
            // Downward triangle at top of tick area
            juce::Path flag;
            float fx = static_cast<float>(phaseX);
            float triTop = static_cast<float>(tickAreaTop);
            flag.addTriangle(fx - 5.0f, triTop, fx + 5.0f, triTop, fx, triTop + 8.0f);
            g.fillPath(flag);
        }
    }

    // Draw edit cursor line (blinking white)
    if (editCursorPosition_ >= 0.0 && editCursorVisible_) {
        double displayTime =
            relativeMode ? (editCursorPosition_ - timeOffset) : editCursorPosition_;
        int cursorX = timeToPixel(displayTime);
        if (cursorX >= 0 && cursorX <= width) {
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawLine(float(cursorX - 1), 0.f, float(cursorX - 1), float(height), 1.f);
            g.drawLine(float(cursorX + 1), 0.f, float(cursorX + 1), float(height), 1.f);
            g.setColour(juce::Colours::white);
            g.drawLine(float(cursorX), 0.f, float(cursorX), float(height), 2.f);
        }
    }

    // Draw playhead line if playing — only when within the clip's time range
    if (playheadPosition >= 0.0 && clipLength > 0.0) {
        double relPos = playheadPosition - timeOffset;
        if (relPos >= 0.0 && relPos <= clipLength) {
            double displayTime = relativeMode ? (playheadPosition - timeOffset) : playheadPosition;

            // Wrap playhead within loop region when looping is active
            if (loopEnabled && loopActive && loopLength > 0.0) {
                double loopStart = relativeMode ? loopOffset : (timeOffset + loopOffset);
                double wrapped = std::fmod(displayTime - loopStart, loopLength);
                if (wrapped < 0.0)
                    wrapped += loopLength;
                displayTime = loopStart + wrapped;
            }

            int playheadX = timeToPixel(displayTime);
            if (playheadX >= 0 && playheadX <= width) {
                int tickAreaTop = height - TICK_HEIGHT_MAJOR;
                g.setColour(juce::Colour(0xFFFF4444));
                g.fillRect(playheadX - 1, tickAreaTop, 2, TICK_HEIGHT_MAJOR);
            }
        }
    }
}

double TimeRuler::calculateMarkerInterval() const {
    // Target roughly 80-120 pixels between major markers
    // zoom is ppb, convert to pps for seconds-mode interval calculation
    double pps = zoom * tempo / 60.0;
    double targetPixels = 100.0;
    double targetInterval = (pps > 0) ? targetPixels / pps : 1.0;

    // Round to nice intervals: 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, etc.
    static const double niceIntervals[] = {0.01, 0.02, 0.05, 0.1,  0.2,  0.5,   1.0,   2.0,
                                           5.0,  10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0};

    for (double interval : niceIntervals) {
        if (interval >= targetInterval * 0.5) {
            return interval;
        }
    }

    return 600.0;  // 10 minutes
}

juce::String TimeRuler::formatTimeLabel(double time, double interval) const {
    int totalSeconds = static_cast<int>(time);
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;

    if (interval < 1.0) {
        // Show milliseconds
        int ms = static_cast<int>((time - totalSeconds) * 1000);
        if (minutes > 0) {
            return juce::String::formatted("%d:%02d.%03d", minutes, seconds, ms);
        }
        return juce::String::formatted("%d.%03d", seconds, ms);
    } else if (interval < 60.0) {
        // Show seconds
        if (minutes > 0) {
            return juce::String::formatted("%d:%02d", minutes, seconds);
        }
        return juce::String::formatted("%ds", seconds);
    } else {
        // Show minutes
        return juce::String::formatted("%d:%02d", minutes, seconds);
    }
}

juce::String TimeRuler::formatBarsBeatsLabel(double time) const {
    double secondsPerBeat = 60.0 / tempo;
    double secondsPerBar = secondsPerBeat * timeSigNumerator;

    int bar = static_cast<int>(time / secondsPerBar) + 1;
    double remainder = std::fmod(time, secondsPerBar);
    int beat = static_cast<int>(remainder / secondsPerBeat) + 1;

    return juce::String::formatted("%d.%d", bar, beat);
}

double TimeRuler::pixelToTime(int pixel) const {
    // Use linked viewport's position for real-time scroll sync
    // zoom is ppb, so pixel→beats→seconds
    int currentScrollOffset = linkedViewport ? linkedViewport->getViewPositionX() : scrollOffset;
    if (zoom > 0 && tempo > 0) {
        double beats = (pixel + currentScrollOffset - leftPadding) / zoom;
        return beats * 60.0 / tempo;
    }
    return 0.0;
}

int TimeRuler::timeToPixel(double time) const {
    // Use linked viewport's position for real-time scroll sync
    // zoom is ppb, so seconds→beats→pixel
    int currentScrollOffset = linkedViewport ? linkedViewport->getViewPositionX() : scrollOffset;
    double beats = time * tempo / 60.0;
    return static_cast<int>(beats * zoom) - currentScrollOffset + leftPadding;
}

void TimeRuler::initLoopInteraction() {
    LoopMarkerInteraction::Host host;
    host.pixelToTime = [this](int pixel) { return pixelToTime(pixel); };
    host.timeToPixel = [this](double time) { return timeToPixel(time); };
    host.snapToGrid = [this](double time) -> double {
        if (tempo <= 0.0)
            return time;
        double secondsPerBeat = 60.0 / tempo;
        return std::round(time / secondsPerBeat) * secondsPerBeat;
    };
    host.onLoopChanged = [this](double start, double end) {
        if (onLoopRegionChanged)
            onLoopRegionChanged(start, end);
    };
    host.onRepaint = [this]() { repaint(); };
    host.maxTime = timelineLength;
    host.topBorderY = getHeight() - TICK_HEIGHT_MAJOR - LOOP_STRIP_HEIGHT;
    host.topBorderThreshold = LOOP_STRIP_HEIGHT;
    loopInteraction_.setHost(std::move(host));
}

}  // namespace magda
