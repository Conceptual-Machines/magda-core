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
        auto fillBounds = bounds;
        fillBounds = fillBounds.removeFromBottom(meterHeight);

        g.setColour(getMeterColour(level));
        g.fillRoundedRectangle(fillBounds, 1.0f);
    }

    static juce::Colour getMeterColour(float level) {
        float dbLevel = gainToDb(level);
        juce::Colour green(0xFF55AA55);
        juce::Colour yellow(0xFFAAAA55);
        juce::Colour red(0xFFAA5555);

        if (dbLevel < -12.0f) {
            return green;
        } else if (dbLevel < 0.0f) {
            float t = (dbLevel + 12.0f) / 12.0f;
            return green.interpolatedWith(yellow, t);
        } else if (dbLevel < 3.0f) {
            float t = dbLevel / 3.0f;
            return yellow.interpolatedWith(red, t);
        } else {
            return red;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

}  // namespace magda
