#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

#include "LevelMeterBallistics.hpp"
#include "LevelMeterScale.hpp"
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
    enum class Orientation { Vertical, Horizontal };

    LevelMeter() = default;

    ~LevelMeter() override {
        stopTimer();
    }

    void setOrientation(Orientation orientation) {
        if (orientation_ == orientation)
            return;

        orientation_ = orientation;
        repaint();
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
            peakLeftHoldTime_ = level_meter_ballistics::peakHoldMs;
        }
        if (rightDb > peakRightDb_) {
            peakRightDb_ = rightDb;
            peakRightHoldTime_ = level_meter_ballistics::peakHoldMs;
        }

        if (!isTimerRunning()) {
            lastUpdateMs_ = level_meter_ballistics::restartClock();
            startTimerHz(60);
        }
    }

    float getLevel() const {
        return std::max(displayLeftLevel_, displayRightLevel_);
    }

    /** Highest peak-hold value across L/R channels, in dB.
     *  Returns MIN_DB (~-60) when there has been no signal. */
    float getPeakDb() const {
        return std::max(peakLeftDb_, peakRightDb_);
    }

    void resetPeak() {
        peakLeftDb_ = MIN_DB;
        peakRightDb_ = MIN_DB;
        peakLeftHoldTime_ = 0.0f;
        peakRightHoldTime_ = 0.0f;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto effectiveBounds = getLocalBounds().toFloat();

        const float gap = 1.0f;
        if (orientation_ == Orientation::Horizontal) {
            float barHeight = (effectiveBounds.getHeight() - gap) / 2.0f;
            auto leftBounds = effectiveBounds.removeFromTop(barHeight);
            effectiveBounds.removeFromTop(gap);
            auto rightBounds = effectiveBounds.removeFromTop(barHeight);

            drawMeterBar(g, leftBounds, displayLeftLevel_, peakLeftDb_);
            drawMeterBar(g, rightBounds, displayRightLevel_, peakRightDb_);

            float zeroDbPos = dbToMeterPos(0.0f);
            float tickX = getLocalBounds().toFloat().getWidth() * zeroDbPos;
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
            g.drawVerticalLine(static_cast<int>(tickX), 0.0f, static_cast<float>(getHeight()));
            return;
        }

        float barWidth = (effectiveBounds.getWidth() - gap) / 2.0f;
        auto leftBounds = effectiveBounds.withWidth(barWidth);
        auto rightBounds =
            effectiveBounds.withWidth(barWidth).withX(effectiveBounds.getX() + barWidth + gap);

        drawMeterBar(g, leftBounds, displayLeftLevel_, peakLeftDb_);
        drawMeterBar(g, rightBounds, displayRightLevel_, peakRightDb_);

        float zeroDbPos = dbToMeterPos(0.0f);
        float tickY = effectiveBounds.getBottom() - effectiveBounds.getHeight() * zeroDbPos;
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        g.drawHorizontalLine(static_cast<int>(tickY), effectiveBounds.getX(),
                             effectiveBounds.getRight());
    }

    static constexpr float MIN_DB = level_meter_scale::minDb;
    static constexpr float MAX_DB = level_meter_scale::maxDb;
    static constexpr float METER_CURVE_EXPONENT = level_meter_scale::meterCurveExponent;

    static float gainToDb(float gain) {
        return level_meter_scale::gainToDb(gain);
    }

    static float dbToMeterPos(float db) {
        return level_meter_scale::dbToMeterPos(db);
    }

    static float meterPosToDb(float pos) {
        return level_meter_scale::meterPosToDb(pos);
    }

  private:
    Orientation orientation_ = Orientation::Vertical;

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
    double lastUpdateMs_ = 0.0;

    void timerCallback() override {
        const float elapsedMs = level_meter_ballistics::getElapsedMs(lastUpdateMs_);
        bool changed = false;
        changed |=
            level_meter_ballistics::updateLevel(displayLeftLevel_, targetLeftLevel_, elapsedMs);
        changed |=
            level_meter_ballistics::updateLevel(displayRightLevel_, targetRightLevel_, elapsedMs);
        changed |= level_meter_ballistics::updatePeak(
            peakLeftDb_, peakLeftHoldTime_, gainToDb(targetLeftLevel_), MIN_DB, elapsedMs);
        changed |= level_meter_ballistics::updatePeak(
            peakRightDb_, peakRightHoldTime_, gainToDb(targetRightLevel_), MIN_DB, elapsedMs);

        if (changed) {
            repaint();
        } else if (displayLeftLevel_ < 0.001f && displayRightLevel_ < 0.001f &&
                   peakLeftDb_ <= MIN_DB && peakRightDb_ <= MIN_DB) {
            stopTimer();
            lastUpdateMs_ = 0.0;
        }
    }

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level, float peakDb) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        float displayLevel = dbToMeterPos(gainToDb(level));

        if (orientation_ == Orientation::Horizontal) {
            float meterWidth = bounds.getWidth() * displayLevel;
            if (meterWidth >= 1.0f) {
                auto fillBounds = bounds.withWidth(meterWidth);
                g.setGradientFill(createMeterGradient(bounds, orientation_));
                g.fillRoundedRectangle(fillBounds, 1.0f);
            }

            float peakPos = dbToMeterPos(peakDb);
            if (peakPos > 0.01f) {
                float peakX = bounds.getX() + bounds.getWidth() * peakPos;
                auto peakColour = peakDb >= 0.0f     ? juce::Colour(0xFFAA5555)
                                  : peakDb >= -12.0f ? juce::Colour(0xFFAAAA55)
                                                     : juce::Colour(0xFF55AA55);
                g.setColour(peakColour.withAlpha(0.9f));
                g.fillRect(peakX, bounds.getY(), 1.5f, bounds.getHeight());
            }
            return;
        }

        float meterHeight = bounds.getHeight() * displayLevel;
        if (meterHeight >= 1.0f) {
            auto fillBounds = bounds;
            fillBounds = fillBounds.removeFromBottom(meterHeight);

            g.setGradientFill(createMeterGradient(bounds, orientation_));
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
    static juce::ColourGradient createMeterGradient(juce::Rectangle<float> bounds,
                                                    Orientation orientation) {
        const juce::Colour green(0xFF55AA55);
        const juce::Colour yellow(0xFFAAAA55);
        const juce::Colour red(0xFFAA5555);

        // Normalized positions along the gradient (0 = bottom, 1 = top)
        float yellowPos = dbToMeterPos(-12.0f);
        float redPos = dbToMeterPos(0.0f);
        constexpr float fade = 0.03f;

        juce::ColourGradient grad = orientation == Orientation::Horizontal
                                        ? juce::ColourGradient(green, bounds.getX(), 0.0f, red,
                                                               bounds.getRight(), 0.0f, false)
                                        : juce::ColourGradient(green, 0.0f, bounds.getBottom(), red,
                                                               0.0f, bounds.getY(), false);
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
