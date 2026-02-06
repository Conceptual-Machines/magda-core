#include "GridOverlayComponent.hpp"

#include "../../layout/LayoutConfig.hpp"
#include "../../themes/DarkTheme.hpp"

namespace magda {

GridOverlayComponent::GridOverlayComponent() {
    setInterceptsMouseClicks(false, false);
}

GridOverlayComponent::~GridOverlayComponent() {
    if (timelineController) {
        timelineController->removeListener(this);
    }
}

void GridOverlayComponent::setController(TimelineController* controller) {
    if (timelineController) {
        timelineController->removeListener(this);
    }

    timelineController = controller;

    if (timelineController) {
        timelineController->addListener(this);

        // Sync initial state
        const auto& state = timelineController->getState();
        currentZoom = state.zoom.horizontalZoom;
        timelineLength = state.timelineLength;
        displayMode = state.display.timeDisplayMode;
        tempoBPM = state.tempo.bpm;
        timeSignatureNumerator = state.tempo.timeSignatureNumerator;
        timeSignatureDenominator = state.tempo.timeSignatureDenominator;

        repaint();
    }
}

void GridOverlayComponent::setZoom(double zoom) {
    if (currentZoom != zoom) {
        currentZoom = zoom;
        repaint();
    }
}

void GridOverlayComponent::setTimelineLength(double length) {
    if (timelineLength != length) {
        timelineLength = length;
        repaint();
    }
}

void GridOverlayComponent::setTimeDisplayMode(TimeDisplayMode mode) {
    if (displayMode != mode) {
        displayMode = mode;
        repaint();
    }
}

void GridOverlayComponent::setTempo(double bpm) {
    if (tempoBPM != bpm) {
        tempoBPM = bpm;
        repaint();
    }
}

void GridOverlayComponent::setTimeSignature(int numerator, int denominator) {
    if (timeSignatureNumerator != numerator || timeSignatureDenominator != denominator) {
        timeSignatureNumerator = numerator;
        timeSignatureDenominator = denominator;
        repaint();
    }
}

// ===== TimelineStateListener Implementation =====

void GridOverlayComponent::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    bool needsRepaint = false;

    // Zoom/scroll changes
    if (hasFlag(changes, ChangeFlags::Zoom) || hasFlag(changes, ChangeFlags::Scroll)) {
        currentZoom = state.zoom.horizontalZoom;
        needsRepaint = true;
    }

    // General cache sync with dirty checks
    if (timelineLength != state.timelineLength) {
        timelineLength = state.timelineLength;
        needsRepaint = true;
    }
    if (displayMode != state.display.timeDisplayMode) {
        displayMode = state.display.timeDisplayMode;
        needsRepaint = true;
    }

    // Tempo/time-sig affect grid line placement (seconds↔beats conversion)
    if (tempoBPM != state.tempo.bpm ||
        timeSignatureNumerator != state.tempo.timeSignatureNumerator ||
        timeSignatureDenominator != state.tempo.timeSignatureDenominator) {
        tempoBPM = state.tempo.bpm;
        timeSignatureNumerator = state.tempo.timeSignatureNumerator;
        timeSignatureDenominator = state.tempo.timeSignatureDenominator;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

// ===== Paint =====

void GridOverlayComponent::paint(juce::Graphics& g) {
    auto area = getLocalBounds();
    drawTimeGrid(g, area);
    drawBeatOverlay(g, area);
}

void GridOverlayComponent::drawTimeGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    if (displayMode == TimeDisplayMode::Seconds) {
        drawSecondsGrid(g, area);
    } else {
        drawBarsBeatsGrid(g, area);
    }
}

void GridOverlayComponent::drawSecondsGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    auto& layout = LayoutConfig::getInstance();
    const int minPixelSpacing = layout.minGridPixelSpacing;

    // currentZoom is ppb - convert to pps for seconds-mode grid calculation
    double pps = (tempoBPM > 0) ? currentZoom * tempoBPM / 60.0 : currentZoom;

    // Extended intervals for deep zoom
    const double intervals[] = {0.0001, 0.0002, 0.0005,                           // Sub-millisecond
                                0.001,  0.002,  0.005,                            // Milliseconds
                                0.01,   0.02,   0.05,                             // Centiseconds
                                0.1,    0.2,    0.25,   0.5,                      // Deciseconds
                                1.0,    2.0,    5.0,    10.0, 15.0, 30.0, 60.0};  // Seconds

    double gridInterval = 1.0;
    for (double interval : intervals) {
        if (static_cast<int>(interval * pps) >= minPixelSpacing) {
            gridInterval = interval;
            break;
        }
    }

    for (double time = 0.0; time <= timelineLength; time += gridInterval) {
        // Convert time to beats, then to pixels
        double beats = time * tempoBPM / 60.0;
        int x = static_cast<int>(beats * currentZoom) + leftPadding - scrollOffset;
        if (x >= area.getX() && x <= area.getRight()) {
            // Determine line brightness based on time hierarchy
            bool isMajor = false;
            if (gridInterval >= 1.0) {
                isMajor = true;
            } else if (gridInterval >= 0.1) {
                isMajor = std::fmod(time, 1.0) < 0.0001;
            } else if (gridInterval >= 0.01) {
                isMajor = std::fmod(time, 0.1) < 0.0001;
            } else if (gridInterval >= 0.001) {
                isMajor = std::fmod(time, 0.01) < 0.0001;
            } else {
                isMajor = std::fmod(time, 0.001) < 0.00001;
            }

            if (isMajor) {
                g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.3f));
                g.drawLine(static_cast<float>(x), static_cast<float>(area.getY()),
                           static_cast<float>(x), static_cast<float>(area.getBottom()), 1.0f);
            } else {
                g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.1f));
                g.drawLine(static_cast<float>(x), static_cast<float>(area.getY()),
                           static_cast<float>(x), static_cast<float>(area.getBottom()), 0.5f);
            }
        }
    }
}

void GridOverlayComponent::drawBarsBeatsGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    auto& layout = LayoutConfig::getInstance();
    const int minPixelSpacing = layout.minGridPixelSpacing;

    // Find grid interval using centralized power-of-2 logic
    double markerIntervalBeats = 1.0;

    double frac = GridConstants::findBeatSubdivision(currentZoom, minPixelSpacing);
    if (frac > 0) {
        markerIntervalBeats = frac;
    } else {
        // Bar multiples
        int mult =
            GridConstants::findBarMultiple(currentZoom, timeSignatureNumerator, minPixelSpacing);
        markerIntervalBeats = timeSignatureNumerator * mult;
    }

    // Convert timeline length to total beats for iteration
    double totalTimelineBeats = timelineLength * tempoBPM / 60.0;

    // Draw grid lines iterating in beats
    for (double beat = 0.0; beat <= totalTimelineBeats; beat += markerIntervalBeats) {
        int x = static_cast<int>(beat * currentZoom) + leftPadding - scrollOffset;
        if (x >= area.getX() && x <= area.getRight()) {
            // Determine line style based on musical position
            bool isBarLine = std::fmod(beat, static_cast<double>(timeSignatureNumerator)) < 0.001;
            bool isBeatLine = std::fmod(beat, 1.0) < 0.001;

            if (isBarLine) {
                g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.4f));
                g.drawLine(static_cast<float>(x), static_cast<float>(area.getY()),
                           static_cast<float>(x), static_cast<float>(area.getBottom()), 1.5f);
            } else if (isBeatLine) {
                g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.2f));
                g.drawLine(static_cast<float>(x), static_cast<float>(area.getY()),
                           static_cast<float>(x), static_cast<float>(area.getBottom()), 1.0f);
            } else {
                g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).brighter(0.05f));
                g.drawLine(static_cast<float>(x), static_cast<float>(area.getY()),
                           static_cast<float>(x), static_cast<float>(area.getBottom()), 0.5f);
            }
        }
    }
}

void GridOverlayComponent::drawBeatOverlay(juce::Graphics& g, juce::Rectangle<int> area) {
    // Only draw beat overlay in seconds mode (bars/beats mode handles this in drawBarsBeatsGrid)
    if (displayMode == TimeDisplayMode::BarsBeats) {
        return;
    }

    // Draw beat subdivisions using actual tempo
    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE).withAlpha(0.5f));

    // currentZoom is ppb - one beat = currentZoom pixels
    const int beatPixelSpacing = static_cast<int>(currentZoom);

    // Only draw beat grid if it's not too dense
    double totalTimelineBeats = timelineLength * tempoBPM / 60.0;
    if (beatPixelSpacing >= 10) {
        for (double beat = 0.0; beat <= totalTimelineBeats; beat += 1.0) {
            int x = static_cast<int>(beat * currentZoom) + leftPadding - scrollOffset;
            if (x >= area.getX() && x <= area.getRight()) {
                g.drawLine(static_cast<float>(x), static_cast<float>(area.getY()),
                           static_cast<float>(x), static_cast<float>(area.getBottom()), 0.5f);
            }
        }
    }
}

}  // namespace magda
