#include "TimeRuler.hpp"

#include <cmath>

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

void TimeRuler::setZoom(double pixelsPerSecond) {
    zoom = pixelsPerSecond;
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
    tempo = bpm;
    repaint();
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

void TimeRuler::setRelativeMode(bool relative) {
    relativeMode = relative;
    repaint();
}

void TimeRuler::setClipLength(double lengthSeconds) {
    clipLength = lengthSeconds;
    repaint();
}

void TimeRuler::setLeftPadding(int padding) {
    leftPadding = padding;
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

        // Clamp zoom to reasonable limits (pixels per second)
        newZoom = juce::jlimit(5.0, 2000.0, newZoom);

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

    // Calculate seconds per beat and per bar
    double secondsPerBeat = 60.0 / tempo;
    double secondsPerBar = secondsPerBeat * timeSigNumerator;

    // Determine what to show based on zoom level
    // At low zoom, show only bars; at high zoom, show beats too
    double pixelsPerBar = secondsPerBar * zoom;
    bool showBeats = pixelsPerBar > 60;  // Only show beats if bars are wide enough

    // In ABS mode: bar numbers are absolute (1, 2, 3...), grid starts at project time 0
    // In REL mode: bar numbers relative to clip (1, 2, 3...), grid starts at clip time 0
    // No barOffset needed since grid coordinate system matches display

    // Find first visible bar
    double startTime = pixelToTime(0);
    int startBar = static_cast<int>(std::floor(startTime / secondsPerBar));
    if (startBar < 1)
        startBar = 1;

    g.setFont(11.0f);

    // Draw bar lines and optionally beat lines
    for (int bar = startBar;; bar++) {
        double barTime = (bar - 1) * secondsPerBar;
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

    // Draw clip boundary markers
    if (clipLength > 0) {
        if (!relativeMode) {
            // ABS mode: draw start and end boundaries at absolute positions
            int clipStartX = timeToPixel(timeOffset);
            if (clipStartX >= 0 && clipStartX <= width) {
                g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
                g.fillRect(clipStartX - 1, 0, 2, height);
            }

            int clipEndX = timeToPixel(timeOffset + clipLength);
            if (clipEndX >= 0 && clipEndX <= width) {
                g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
                g.fillRect(clipEndX - 1, 0, 3, height);
            }
        } else {
            // REL mode: draw end boundary relative to clip start (which is at time 0)
            int clipEndX = timeToPixel(clipLength);
            if (clipEndX >= 0 && clipEndX <= width) {
                g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
                g.fillRect(clipEndX - 1, 0, 3, height);
            }
        }
    }
}

double TimeRuler::calculateMarkerInterval() const {
    // Target roughly 80-120 pixels between major markers
    double targetPixels = 100.0;
    double targetInterval = targetPixels / zoom;

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
    int currentScrollOffset = linkedViewport ? linkedViewport->getViewPositionX() : scrollOffset;
    return (pixel + currentScrollOffset - leftPadding) / zoom;
}

int TimeRuler::timeToPixel(double time) const {
    // Use linked viewport's position for real-time scroll sync
    int currentScrollOffset = linkedViewport ? linkedViewport->getViewPositionX() : scrollOffset;
    return static_cast<int>(time * zoom) - currentScrollOffset + leftPadding;
}

}  // namespace magda
