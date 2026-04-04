#include "RampCurveDisplay.hpp"

#include "audio/StepClock.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

void RampCurveDisplay::paint(juce::Graphics& g) {
    auto outerBounds = getLocalBounds().toFloat();
    if (outerBounds.getWidth() < 8.0f || outerBounds.getHeight() < 8.0f)
        return;

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.08f));
    g.fillRoundedRectangle(outerBounds, 2.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
    g.drawRoundedRectangle(outerBounds.reduced(0.5f), 2.0f, 0.5f);

    // Inset for curve content (padding inside the border)
    auto bounds = outerBounds.reduced(8.0f);
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float x0 = bounds.getX();
    float y0 = bounds.getY();

    // Grid lines (4x4)
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
    for (int i = 1; i < 4; ++i) {
        float fx = x0 + w * (static_cast<float>(i) / 4.0f);
        float fy = y0 + h * (static_cast<float>(i) / 4.0f);
        g.drawLine(fx, y0, fx, y0 + h, 1.0f);
        g.drawLine(x0, fy, x0 + w, fy, 1.0f);
    }

    // Diagonal reference line (linear)
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
    g.drawLine(x0, y0 + h, x0 + w, y0, 0.5f);

    // Tick distribution — overlaid at bottom edge, only when large enough
    if (h > 60.0f && numTicks_ > 0) {
        constexpr float TICK_H = 10.0f;
        float tickY0 = y0 + h - TICK_H;
        float tickY1 = y0 + h;
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.35f));
        int c = std::max(1, cycles_);
        for (int i = 0; i < numTicks_; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(numTicks_);
            double curved;
            if (c <= 1) {
                curved = daw::audio::StepClock::applyRampCurve(t, depth_, skew_, hardAngle_);
            } else {
                double segLen = 1.0 / static_cast<double>(c);
                int seg = std::min(static_cast<int>(t / segLen), c - 1);
                double tLocal = (t - seg * segLen) / segLen;
                double tLocalCurved =
                    daw::audio::StepClock::applyRampCurve(tLocal, depth_, skew_, hardAngle_);
                curved = (seg + tLocalCurved) * segLen;
            }
            curved = juce::jlimit(0.0, 1.0, curved);
            float tx = x0 + static_cast<float>(curved) * w;
            g.drawLine(tx, tickY0, tx, tickY1, 1.0f);
        }
    }

    // Playback sweep on tick strip
    if (h > 60.0f && playbackPos_ >= 0.0f && playbackPos_ <= 1.0f) {
        constexpr float TICK_H = 10.0f;
        float tickY0 = y0 + h - TICK_H;
        float tickY1 = y0 + h;

        // Apply curve with cycles to get the x position
        float pos = playbackPos_;
        float curvedPos;
        if (cycles_ <= 1) {
            curvedPos = static_cast<float>(
                daw::audio::StepClock::applyRampCurve(pos, depth_, skew_, hardAngle_));
        } else {
            float segLen = 1.0f / static_cast<float>(cycles_);
            int seg = std::min(static_cast<int>(pos / segLen), cycles_ - 1);
            float tLocal = (pos - seg * segLen) / segLen;
            float tLocalCurved = static_cast<float>(
                daw::audio::StepClock::applyRampCurve(tLocal, depth_, skew_, hardAngle_));
            curvedPos = (seg + tLocalCurved) * segLen;
        }
        curvedPos = juce::jlimit(0.0f, 1.0f, curvedPos);
        float sweepX = x0 + curvedPos * w;

        // Fading trail (gradient from transparent to green)
        constexpr float TRAIL_W = 30.0f;
        float trailLeft = std::max(x0, sweepX - TRAIL_W);
        auto trailColour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
        g.setGradientFill(juce::ColourGradient(trailColour.withAlpha(0.0f), trailLeft, tickY0,
                                               trailColour.withAlpha(0.25f), sweepX, tickY0,
                                               false));
        g.fillRect(trailLeft, tickY0, sweepX - trailLeft, TICK_H);

        // Bright sweep line
        g.setColour(trailColour.withAlpha(0.8f));
        g.drawLine(sweepX, tickY0, sweepX, tickY1, 2.0f);
    }

    // Clip graphics to curve bounds so the curve never draws outside
    g.reduceClipRegion(bounds.toNearestInt());

    // Draw the curve
    juce::Path curvePath;
    constexpr int NUM_POINTS = 48;
    for (int i = 0; i <= NUM_POINTS; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(NUM_POINTS);
        double curved = daw::audio::StepClock::applyRampCurve(t, depth_, skew_, hardAngle_);
        float px = x0 + static_cast<float>(t) * w;
        float py = y0 + h - static_cast<float>(curved) * h;
        if (i == 0)
            curvePath.startNewSubPath(px, py);
        else
            curvePath.lineTo(px, py);
    }
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    g.strokePath(curvePath, juce::PathStrokeType(1.5f));

    // Handle at the control point: (s, s+depth) in graph space.
    // Square when hard angle, circle when smooth bezier.
    constexpr float HANDLE_R = 4.0f;
    float s = 0.5f + skew_ * 0.49f;
    float hx = x0 + s * w;
    float hy = y0 + h - (s + depth_) * h;
    hx = juce::jlimit(x0 + HANDLE_R, x0 + w - HANDLE_R, hx);
    hy = juce::jlimit(y0 + HANDLE_R, y0 + h - HANDLE_R, hy);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    if (hardAngle_) {
        g.fillRect(hx - HANDLE_R, hy - HANDLE_R, HANDLE_R * 2.0f, HANDLE_R * 2.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        g.drawRect(hx - HANDLE_R, hy - HANDLE_R, HANDLE_R * 2.0f, HANDLE_R * 2.0f, 1.5f);
    } else {
        g.fillEllipse(hx - HANDLE_R, hy - HANDLE_R, HANDLE_R * 2.0f, HANDLE_R * 2.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        g.drawEllipse(hx - HANDLE_R, hy - HANDLE_R, HANDLE_R * 2.0f, HANDLE_R * 2.0f, 1.5f);
    }
}

void RampCurveDisplay::mouseDown(const juce::MouseEvent& e) {
    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float x0 = bounds.getX();
    float y0 = bounds.getY();
    float s = 0.5f + skew_ * 0.49f;
    float handleX = x0 + s * w;
    float handleY = y0 + h - (s + depth_) * h;

    // Right-click on control point toggles hard angle
    if (e.mods.isPopupMenu()) {
        constexpr float HIT_R = 10.0f;
        float dx = e.position.x - handleX;
        float dy = e.position.y - handleY;
        if (dx * dx + dy * dy <= HIT_R * HIT_R) {
            hardAngle_ = !hardAngle_;
            repaint();
            if (onHardAngleChanged)
                onHardAngleChanged(hardAngle_);
            return;
        }
    }

    handleOffsetX_ = handleX - e.position.x;
    handleOffsetY_ = handleY - e.position.y;
}

void RampCurveDisplay::mouseDrag(const juce::MouseEvent& e) {
    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float x0 = bounds.getX();
    float y0 = bounds.getY();

    float cx = e.position.x + handleOffsetX_;
    float cy = e.position.y + handleOffsetY_;

    float sRaw = (cx - x0) / w;
    float s = juce::jlimit(0.01f, 0.99f, sRaw);
    float newDepth = (y0 + h - cy) / h - s;
    float newSkew = (s - 0.5f) / 0.49f;

    newSkew = juce::jlimit(-1.0f, 1.0f, newSkew);
    newDepth = juce::jlimit(-1.0f, 1.0f, newDepth);

    depth_ = newDepth;
    skew_ = newSkew;
    repaint();
    if (onCurveChanged)
        onCurveChanged(newDepth, newSkew);
}

void RampCurveDisplay::mouseDoubleClick(const juce::MouseEvent&) {
    depth_ = 0.0f;
    skew_ = 0.0f;
    repaint();
    if (onCurveChanged)
        onCurveChanged(0.0f, 0.0f);
}

}  // namespace magda::daw::ui
