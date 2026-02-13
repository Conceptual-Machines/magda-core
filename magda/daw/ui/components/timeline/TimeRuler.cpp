#include "TimeRuler.hpp"

#include <cmath>

#include "CursorManager.hpp"
#include "DarkTheme.hpp"
#include "LayoutConfig.hpp"

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

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));

    // Border line above ticks (separates labels from ticks)
    int tickAreaTop = getHeight() - TICK_HEIGHT_MAJOR;
    g.fillRect(0, tickAreaTop, getWidth(), 1);

    // Bottom border line
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    // Draw based on mode
    if (displayMode == DisplayMode::Seconds) {
        drawSecondsMode(g);
    } else {
        drawBarsBeatsMode(g);
    }
}

void TimeRuler::resized() {
    // Nothing specific needed
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
    repaint();
}

void TimeRuler::setLoopPhaseMarker(double positionSeconds, bool visible) {
    loopPhasePosition = positionSeconds;
    loopPhaseVisible = visible;
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
        newZoom = juce::jlimit(1.0, 2000.0, newZoom);

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

void TimeRuler::mouseMove(const juce::MouseEvent& /*event*/) {
    setMouseCursor(CursorManager::getInstance().getZoomCursor());
}

void TimeRuler::mouseExit(const juce::MouseEvent& /*event*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
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
        g.drawVerticalLine(x, static_cast<float>(height - tickHeight), static_cast<float>(height));

        // Draw label for major ticks
        if (isMajor) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            juce::String label = formatTimeLabel(time, interval);
            g.drawText(label, x - 30, LABEL_MARGIN, 60,
                       height - TICK_HEIGHT_MAJOR - LABEL_MARGIN * 2, juce::Justification::centred,
                       false);
        }
    }
}

void TimeRuler::drawBarsBeatsMode(juce::Graphics& g) {
    const int height = getHeight();
    const int width = getWidth();

    // zoom is ppb, so pixelsPerBar = zoom * beatsPerBar (no BPM dependency!)
    double secondsPerBeat = 60.0 / tempo;
    double secondsPerBar = secondsPerBeat * timeSigNumerator;
    double pixelsPerBar = zoom * timeSigNumerator;
    bool showBeats = pixelsPerBar > 60;  // Only show beats if bars are wide enough

    // In ABS mode: bar numbers are absolute (1, 2, 3...), grid starts at project time 0
    // In REL mode: bar numbers relative to clip (1, 2, 3...), grid starts at clip time 0
    // No barOffset needed since grid coordinate system matches display

    // Find first visible bar (offset by barOriginSeconds)
    double startTime = pixelToTime(0);
    int startBar = juce::jmax(
        1, static_cast<int>(std::floor((startTime - barOriginSeconds) / secondsPerBar)) + 1);

    g.setFont(11.0f);

    // Draw bar lines and optionally beat lines
    for (int bar = startBar;; bar++) {
        double barTime = barOriginSeconds + (bar - 1) * secondsPerBar;
        int barX = timeToPixel(barTime);

        if (barX > width)
            break;
        if (barTime > timelineLength)
            break;

        if (barX >= 0) {
            // Draw bar line (major tick)
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            g.drawVerticalLine(barX, static_cast<float>(height - TICK_HEIGHT_MAJOR),
                               static_cast<float>(height));

            // Draw bar number (always 1, 2, 3... from the left edge)
            juce::String label = juce::String(bar);
            g.drawText(label, barX - 20, LABEL_MARGIN, 40,
                       height - TICK_HEIGHT_MAJOR - LABEL_MARGIN * 2, juce::Justification::centred,
                       false);
        }

        // Draw beat lines within this bar
        if (showBeats) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
            for (int beat = 2; beat <= timeSigNumerator; beat++) {
                double beatTime = barTime + (beat - 1) * secondsPerBeat;
                int beatX = timeToPixel(beatTime);

                if (beatX >= 0 && beatX <= width && beatTime <= timelineLength) {
                    g.drawVerticalLine(beatX, static_cast<float>(height - TICK_HEIGHT_MINOR),
                                       static_cast<float>(height));
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

    // Draw loop region markers (green when active, grey when disabled)
    if (loopEnabled && loopLength > 0.0) {
        double loopStartTime = relativeMode ? loopOffset : (timeOffset + loopOffset);
        double loopEndTime = loopStartTime + loopLength;

        int loopStartX = timeToPixel(loopStartTime);
        int loopEndX = timeToPixel(loopEndTime);

        auto markerColour = loopActive ? DarkTheme::getColour(DarkTheme::LOOP_MARKER)
                                       : DarkTheme::getColour(DarkTheme::TEXT_DISABLED);
        float stripeAlpha = loopActive ? 0.2f : 0.1f;

        if (loopEndX >= 0 && loopStartX <= width) {
            // Semi-transparent stripe across ruler height
            g.setColour(markerColour.withAlpha(stripeAlpha));
            g.fillRect(loopStartX, 0, loopEndX - loopStartX, height);

            // 2px vertical lines at boundaries
            g.setColour(markerColour.withAlpha(loopActive ? 1.0f : 0.5f));
            if (loopStartX >= 0 && loopStartX <= width) {
                g.fillRect(loopStartX - 1, 0, 2, height);
            }
            if (loopEndX >= 0 && loopEndX <= width) {
                g.fillRect(loopEndX - 1, 0, 2, height);
            }

            // Small triangular flags at top
            int flagTop = 2;
            if (loopStartX >= 0 && loopStartX <= width) {
                juce::Path startFlag;
                startFlag.addTriangle(
                    static_cast<float>(loopStartX), static_cast<float>(flagTop),
                    static_cast<float>(loopStartX), static_cast<float>(flagTop + 10),
                    static_cast<float>(loopStartX + 7), static_cast<float>(flagTop + 5));
                g.fillPath(startFlag);
            }
            if (loopEndX >= 0 && loopEndX <= width) {
                juce::Path endFlag;
                endFlag.addTriangle(static_cast<float>(loopEndX), static_cast<float>(flagTop),
                                    static_cast<float>(loopEndX), static_cast<float>(flagTop + 10),
                                    static_cast<float>(loopEndX - 7),
                                    static_cast<float>(flagTop + 5));
                g.fillPath(endFlag);
            }
        }
    }

    // Draw loop phase marker (yellow)
    if (loopPhaseVisible && loopEnabled) {
        double phaseTime = relativeMode ? loopPhasePosition : (timeOffset + loopPhasePosition);
        int phaseX = timeToPixel(phaseTime);
        if (phaseX >= 0 && phaseX <= width) {
            auto col = juce::Colour(0xFFCCAA44);  // OFFSET_MARKER yellow
            g.setColour(col);
            g.fillRect(phaseX - 1, 0, 2, height);
            // Downward triangle at top
            juce::Path flag;
            float fx = static_cast<float>(phaseX);
            flag.addTriangle(fx - 5.0f, 2.0f, fx + 5.0f, 2.0f, fx, 10.0f);
            g.fillPath(flag);
        }
    }

    // Draw playhead line if playing
    if (playheadPosition >= 0.0) {
        // In absolute mode, playhead is at absolute position
        // In relative mode, playhead is relative to timeOffset
        double displayTime = relativeMode ? (playheadPosition - timeOffset) : playheadPosition;
        int playheadX = timeToPixel(displayTime);
        if (playheadX >= 0 && playheadX <= width) {
            // Draw playhead line (red)
            g.setColour(juce::Colour(0xFFFF4444));
            g.fillRect(playheadX - 1, 0, 2, height);
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

}  // namespace magda
