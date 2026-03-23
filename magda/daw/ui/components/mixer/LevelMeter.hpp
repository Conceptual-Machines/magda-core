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
 *
 * Features smooth ballistics (fast attack, exponential decay) and peak hold.
 */
class LevelMeter : public juce::Component, private juce::Timer {
  public:
    LevelMeter() = default;

    ~LevelMeter() override {
        stopTimer();
    }

    void setLevel(float newLevel) {
        setLevels(newLevel, newLevel);
    }

    void setLevels(float left, float right) {
        targetLeftLevel_ = juce::jlimit(0.0f, 2.0f, left);
        targetRightLevel_ = juce::jlimit(0.0f, 2.0f, right);

        // Update peak hold
        float leftDb = gainToDb(targetLeftLevel_);
        float rightDb = gainToDb(targetRightLevel_);

        if (leftDb > peakLeftDb_) {
            peakLeftDb_ = leftDb;
            peakLeftHoldTime_ = PEAK_HOLD_MS;
        }
        if (rightDb > peakRightDb_) {
            peakRightDb_ = rightDb;
            peakRightHoldTime_ = PEAK_HOLD_MS;
        }

        if (!isTimerRunning())
            startTimerHz(60);
    }

    float getLevel() const {
        return std::max(displayLeftLevel_, displayRightLevel_);
    }

    void paint(juce::Graphics& g) override {
        auto effectiveBounds = getLocalBounds().toFloat();

        const float gap = 1.0f;
        float barWidth = (effectiveBounds.getWidth() - gap) / 2.0f;

        auto leftBounds = effectiveBounds.withWidth(barWidth);
        auto rightBounds =
            effectiveBounds.withWidth(barWidth).withX(effectiveBounds.getX() + barWidth + gap);

        drawMeterBar(g, leftBounds, displayLeftLevel_, peakLeftDb_);
        drawMeterBar(g, rightBounds, displayRightLevel_, peakRightDb_);

        // 0dB tick mark
        float zeroDbPos = dbToMeterPos(0.0f);
        float tickY = effectiveBounds.getBottom() - effectiveBounds.getHeight() * zeroDbPos;
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        g.drawHorizontalLine(static_cast<int>(tickY), effectiveBounds.getX(),
                             effectiveBounds.getRight());
    }

  private:
    // Target levels (raw input)
    float targetLeftLevel_ = 0.0f;
    float targetRightLevel_ = 0.0f;

    // Smoothed display levels (after ballistics)
    float displayLeftLevel_ = 0.0f;
    float displayRightLevel_ = 0.0f;

    // Peak hold state (in dB)
    float peakLeftDb_ = MIN_DB;
    float peakRightDb_ = MIN_DB;
    float peakLeftHoldTime_ = 0.0f;
    float peakRightHoldTime_ = 0.0f;

    // dB conversion helpers
    static constexpr float MIN_DB = -60.0f;
    static constexpr float MAX_DB = 6.0f;
    static constexpr float METER_CURVE_EXPONENT = 2.0f;

    // Ballistics (tuned for 60Hz refresh)
    static constexpr float ATTACK_COEFF = 0.9f;    // Near-instant attack for transients
    static constexpr float RELEASE_COEFF = 0.05f;  // Slow decay — smooth falloff

    // Peak hold
    static constexpr float PEAK_HOLD_MS = 1500.0f;
    static constexpr float PEAK_DECAY_DB_PER_FRAME = 0.8f;

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

    void timerCallback() override {
        bool changed = false;
        changed |= updateLevel(displayLeftLevel_, targetLeftLevel_);
        changed |= updateLevel(displayRightLevel_, targetRightLevel_);
        changed |= updatePeak(peakLeftDb_, peakLeftHoldTime_, gainToDb(targetLeftLevel_));
        changed |= updatePeak(peakRightDb_, peakRightHoldTime_, gainToDb(targetRightLevel_));

        if (changed) {
            repaint();
        } else if (displayLeftLevel_ < 0.001f && displayRightLevel_ < 0.001f &&
                   peakLeftDb_ <= MIN_DB && peakRightDb_ <= MIN_DB) {
            stopTimer();
        }
    }

    bool updateLevel(float& display, float target) {
        float prev = display;
        if (target > display) {
            // Attack: fast rise
            display += (target - display) * ATTACK_COEFF;
        } else {
            // Release: slow decay
            display += (target - display) * RELEASE_COEFF;
        }
        // Snap to zero when very small
        if (display < 0.001f)
            display = 0.0f;
        return std::abs(display - prev) > 0.0001f;
    }

    bool updatePeak(float& peakDb, float& holdTime, float currentDb) {
        float prev = peakDb;
        if (currentDb > peakDb) {
            peakDb = currentDb;
            holdTime = PEAK_HOLD_MS;
        } else if (holdTime > 0.0f) {
            holdTime -= 1000.0f / 60.0f;  // ~16.7ms per frame at 60Hz
        } else {
            peakDb -= PEAK_DECAY_DB_PER_FRAME;
            if (peakDb < MIN_DB)
                peakDb = MIN_DB;
        }
        return std::abs(peakDb - prev) > 0.01f;
    }

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level, float peakDb) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        float displayLevel = dbToMeterPos(gainToDb(level));
        float meterHeight = bounds.getHeight() * displayLevel;

        if (meterHeight >= 1.0f) {
            auto fillBounds = bounds;
            fillBounds = fillBounds.removeFromBottom(meterHeight);

            g.setGradientFill(createMeterGradient(bounds));
            g.fillRoundedRectangle(fillBounds, 1.0f);
        }

        // Peak hold indicator
        float peakPos = dbToMeterPos(peakDb);
        if (peakPos > 0.01f) {
            float peakY = bounds.getBottom() - bounds.getHeight() * peakPos;
            auto peakColour = peakDb >= 0.0f     ? juce::Colour(0xFFAA5555)
                              : peakDb >= -12.0f ? juce::Colour(0xFFAAAA55)
                                                 : juce::Colour(0xFF55AA55);
            g.setColour(peakColour.withAlpha(0.9f));
            g.fillRect(bounds.getX(), peakY, bounds.getWidth(), 1.5f);
        }
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
