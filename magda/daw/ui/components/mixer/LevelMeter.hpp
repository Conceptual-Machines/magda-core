#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

#include "DisplayListGraphics.hpp"
#include "LevelMeterClock.hpp"
#include "LevelMeterScale.hpp"
#include "magda/sdk/meter/MeterPainter.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/SdkColourRoles.hpp"

namespace magda {

/**
 * @brief Stereo level meter component (L/R bars)
 *
 * Shared between MixerView channel strips and SessionView mini channel strips. The state is the
 * SDK meter model and the drawing its display list (magda-sdk docs/meter.md), so a browser shell
 * renders the same meter.
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

    // Anchor the vertical meter's 0 dB point to a component-local Y position.
    // The scale is remapped above and below that point so the fill, gradient,
    // peak marker, and reference tick continue to agree.
    void setVerticalZeroDbY(float y) {
        if (std::abs(verticalZeroDbY_ - y) < 0.01f)
            return;

        verticalZeroDbY_ = y;
        repaint();
    }

    void setLevel(float newLevel) {
        setLevels(newLevel, newLevel);
    }

    void setLevels(float left, float right) {
        const std::array<float, 2> gains{left, right};
        model_.setTargets(gains);

        if (!isTimerRunning()) {
            lastUpdateMs_ = level_meter_clock::restart();
            startTimerHz(60);
        }
    }

    float getLevel() const {
        return model_.loudestDisplayGain();
    }

    /** Highest peak-hold value across L/R channels, in dB.
     *  Returns MIN_DB (~-60) when there has been no signal. */
    float getPeakDb() const {
        return model_.loudestPeakDb();
    }

    void resetPeak() {
        model_.resetPeaks();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const sdk::MeterLayout layout{
            static_cast<float>(getWidth()), static_cast<float>(getHeight()),
            orientation_ == Orientation::Horizontal ? sdk::MeterLayout::Orientation::Horizontal
                                                    : sdk::MeterLayout::Orientation::Vertical,
            verticalZeroDbY_};
        sdk::paintMeter(model_, layout, displayList_);
        sdk::juce_host::drawDisplayList(g, displayList_, resolveColour);
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
    float verticalZeroDbY_ = -1.0f;
    sdk::MeterModel model_;
    sdk::display::DisplayList displayList_;
    double lastUpdateMs_ = 0.0;

    // The opaque backing matches the track header, so a container fill does not show through.
    static juce::Colour resolveColour(sdk::display::ColourRole role) {
        if (role == sdk::display::ColourRole::Background)
            return ActiveTheme::getColour(ActiveTheme::TRACK_BACKGROUND);
        return sdkRoleColour(role);
    }

    void timerCallback() override {
        if (model_.advance(level_meter_clock::elapsedMs(lastUpdateMs_))) {
            repaint();
        } else if (model_.isIdle()) {
            stopTimer();
            lastUpdateMs_ = 0.0;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

}  // namespace magda
