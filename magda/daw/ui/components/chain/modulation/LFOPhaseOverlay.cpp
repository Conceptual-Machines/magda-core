#include "modulation/LFOPhaseOverlay.hpp"

#include <cmath>

namespace magda {

LFOPhaseOverlay::LFOPhaseOverlay() {
    setInterceptsMouseClicks(false, false);  // Click-through to editor components
    setOpaque(true);                         // Opaque to prevent flickering
    // 60 FPS so the phase dot stays readable for fast sync divisions —
    // 30 FPS aliases past ~1/16 (8 Hz) and the dot looks chaotically fast
    // even though the underlying rate is correct.
    startTimer(16);
}

LFOPhaseOverlay::~LFOPhaseOverlay() {
    stopTimer();
}

void LFOPhaseOverlay::timerCallback() {
    repaint();
}

bool LFOPhaseOverlay::hitTest(int /*x*/, int /*y*/) {
    return false;  // Always click-through
}

void LFOPhaseOverlay::paint(juce::Graphics& g) {
    // Background (opaque)
    g.fillAll(juce::Colour(0xFF1A1A1A));

    if (!modInfo_ || getWidth() <= 0 || getHeight() <= 0)
        return;

    paintGrid(g);
    paintCurve(g);
    paintPhaseIndicator(g);
}

void LFOPhaseOverlay::paintGrid(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Horizontal center line (0.5 value)
    g.setColour(juce::Colour(0x20FFFFFF));
    int centerY = bounds.getHeight() / 2;
    g.drawHorizontalLine(centerY, 0.0f, static_cast<float>(bounds.getWidth()));

    // Quarter lines (0.25, 0.75 value)
    g.setColour(juce::Colour(0x10FFFFFF));
    g.drawHorizontalLine(bounds.getHeight() / 4, 0.0f, static_cast<float>(bounds.getWidth()));
    g.drawHorizontalLine(bounds.getHeight() * 3 / 4, 0.0f, static_cast<float>(bounds.getWidth()));

    // Vertical quarter lines (phase 0.25, 0.5, 0.75)
    for (int i = 1; i < 4; ++i) {
        int x = bounds.getWidth() * i / 4;
        g.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getHeight()));
    }

    // Phase 0.5 line (center) slightly brighter
    g.setColour(juce::Colour(0x20FFFFFF));
    g.drawVerticalLine(bounds.getWidth() / 2, 0.0f, static_cast<float>(bounds.getHeight()));
}

void LFOPhaseOverlay::paintCurve(juce::Graphics& g) {
    const auto* mod = modInfo_;
    if (!mod)
        return;

    if (mod->curvePoints.empty())
        return;

    const auto bounds = getLocalBounds();
    const float width = static_cast<float>(bounds.getWidth());
    const float height = static_cast<float>(bounds.getHeight());

    juce::Path curvePath;
    const auto& points = mod->curvePoints;

    // Start at first point
    float startX = points[0].phase * width;
    float startY = (1.0f - points[0].value) * height;
    curvePath.startNewSubPath(startX, startY);

    // Draw segments between points
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& p1 = points[i - 1];
        const auto& p2 = points[i];

        float x1 = p1.phase * width;
        float y1 = (1.0f - p1.value) * height;
        float x2 = p2.phase * width;
        float y2 = (1.0f - p2.value) * height;

        double tension = static_cast<double>(p1.tension);

        if (std::abs(tension) < 0.001) {
            // Pure linear
            curvePath.lineTo(x2, y2);
        } else {
            // Tension-based curve
            constexpr int NUM_SEGMENTS = 16;
            for (int seg = 1; seg <= NUM_SEGMENTS; ++seg) {
                double t = static_cast<double>(seg) / NUM_SEGMENTS;
                double curvedT = applyTension(t, tension);

                float segX = x1 + static_cast<float>(t) * (x2 - x1);
                float segY = y1 + static_cast<float>(curvedT) * (y2 - y1);
                curvePath.lineTo(segX, segY);
            }
        }
    }

    // Draw the curve
    g.setColour(curveColour_);
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));

    // Fill under curve
    juce::Path fillPath = curvePath;
    fillPath.lineTo(width, height);
    fillPath.lineTo(0.0f, height);
    fillPath.closeSubPath();
    g.setColour(curveColour_.withAlpha(0.13f));
    g.fillPath(fillPath);
}

void LFOPhaseOverlay::paintPhaseIndicator(juce::Graphics& g) {
    const auto* mod = modInfo_;
    if (!mod)
        return;

    auto bounds = getLocalBounds();

    float phase = mod->phase;
    float value = mod->value;

    int x = static_cast<int>(phase * bounds.getWidth());
    int y = static_cast<int>((1.0f - value) * bounds.getHeight());

    // Draw crosshair lines (toggle with 'C' key)
    if (showCrosshair_) {
        g.setColour(curveColour_.withAlpha(0.4f));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getHeight()));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getWidth()));
    }

    // Draw indicator dot
    constexpr float dotSize = 5.0f;
    constexpr float dotRadius = dotSize / 2.0f;
    g.setColour(curveColour_);
    g.fillEllipse(static_cast<float>(x) - dotRadius, static_cast<float>(y) - dotRadius, dotSize,
                  dotSize);

    // Draw white outline
    g.setColour(juce::Colours::white);
    g.drawEllipse(static_cast<float>(x) - dotRadius, static_cast<float>(y) - dotRadius, dotSize,
                  dotSize, 1.0f);
}

double LFOPhaseOverlay::applyTension(double t, double tension) const {
    if (tension > 0) {
        return std::pow(t, 1.0 + tension * 2.0);
    } else {
        return 1.0 - std::pow(1.0 - t, 1.0 - tension * 2.0);
    }
}

}  // namespace magda
