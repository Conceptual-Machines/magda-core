#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

#include "ui/themes/DarkTheme.hpp"

namespace magda {

/**
 * @brief Stereo level meter component (L/R bars)
 *
 * Shared between MixerView channel strips and SessionView mini channel strips.
 * Displays two vertical bars representing left and right audio levels with
 * green/yellow/red colour gradient based on dB level.
 */
class LevelMeter : public juce::Component {
  public:
    LevelMeter() = default;

    void setLevel(float newLevel) {
        setLevels(newLevel, newLevel);
    }

    void setLevels(float left, float right) {
        leftLevel_ = juce::jlimit(0.0f, 2.0f, left);
        rightLevel_ = juce::jlimit(0.0f, 2.0f, right);
        repaint();
    }

    float getLevel() const {
        return std::max(leftLevel_, rightLevel_);
    }

    void paint(juce::Graphics& g) override {
        auto effectiveBounds = getLocalBounds().toFloat();

        const float gap = 1.0f;
        float barWidth = (effectiveBounds.getWidth() - gap) / 2.0f;

        auto leftBounds = effectiveBounds.withWidth(barWidth);
        auto rightBounds =
            effectiveBounds.withWidth(barWidth).withX(effectiveBounds.getX() + barWidth + gap);

        drawMeterBar(g, leftBounds, leftLevel_);
        drawMeterBar(g, rightBounds, rightLevel_);

        // 0dB tick mark
        float zeroDbPos = dbToMeterPos(0.0f);
        float tickY = effectiveBounds.getBottom() - effectiveBounds.getHeight() * zeroDbPos;
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        g.drawHorizontalLine(static_cast<int>(tickY), effectiveBounds.getX(),
                             effectiveBounds.getRight());
    }

  private:
    float leftLevel_ = 0.0f;
    float rightLevel_ = 0.0f;

    // dB conversion helpers (self-contained for the meter display)
    static constexpr float MIN_DB = -60.0f;
    static constexpr float MAX_DB = 6.0f;
    static constexpr float METER_CURVE_EXPONENT = 2.0f;

    static float gainToDb(float gain) {
        if (gain <= 0.0f)
            return MIN_DB;
        return 20.0f * std::log10(gain);
    }

    static float dbToMeterPos(float db) {
        if (db <= MIN_DB)
            return 0.0f;
        if (db >= MAX_DB)
            return 1.0f;
        float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);
        return std::pow(normalized, METER_CURVE_EXPONENT);
    }

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        float displayLevel = dbToMeterPos(gainToDb(level));
        float meterHeight = bounds.getHeight() * displayLevel;
        if (meterHeight < 1.0f)
            return;

        auto fillBounds = bounds;
        fillBounds = fillBounds.removeFromBottom(meterHeight);

        g.setGradientFill(createMeterGradient(bounds));
        g.fillRoundedRectangle(fillBounds, 1.0f);
    }

    // Vertical gradient: green at bottom, yellow at -12dB, red at 0dB, with short fades
    static juce::ColourGradient createMeterGradient(juce::Rectangle<float> bounds) {
        const juce::Colour green(0xFF55AA55);
        const juce::Colour yellow(0xFFAAAA55);
        const juce::Colour red(0xFFAA5555);

        // Normalized positions along the gradient (0 = bottom, 1 = top)
        float yellowPos = dbToMeterPos(-12.0f);
        float redPos = dbToMeterPos(0.0f);
        constexpr float fade = 0.03f;

        // Gradient runs bottom to top
        juce::ColourGradient grad(green, 0.0f, bounds.getBottom(), red, 0.0f, bounds.getY(), false);
        // Green solid, then short fade to yellow around -12dB
        grad.addColour(std::max(0.0, (double)yellowPos - fade), green);
        grad.addColour(std::min(1.0, (double)yellowPos + fade), yellow);
        // Yellow solid, then short fade to red around 0dB
        grad.addColour(std::max(0.0, (double)redPos - fade), yellow);
        grad.addColour(std::min(1.0, (double)redPos + fade), red);
        return grad;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

}  // namespace magda
