#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/common/SvgButton.hpp"
#include "ui/components/mixer/LevelMeter.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui::node_header {

// Flat-thumb LookAndFeel for the device/rack gain slider: draws no track,
// just a thin horizontal bar at the slider position. Designed to overlay
// on a LevelMeter so the meter strip remains visible behind the thumb.
class FlatGainSliderLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawLinearSlider(juce::Graphics& g, int x, int /*y*/, int width, int /*height*/,
                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                          const juce::Slider::SliderStyle /*style*/,
                          juce::Slider& /*slider*/) override {
        constexpr float thumbHeight = 2.0f;
        const float thumbY = sliderPos - thumbHeight * 0.5f;
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.fillRect((float)x, thumbY, (float)width, thumbHeight);
    }

    int getSliderThumbRadius(juce::Slider&) override {
        return 1;
    }

    static FlatGainSliderLookAndFeel& getInstance() {
        static FlatGainSliderLookAndFeel instance;
        return instance;
    }
};

// Compact rotary used for the device wet/dry mix knob at the top of the
// meter strip. Draws a small filled circle with a single pointer line —
// no track, no labels — so it reads at ~16px.
class MixKnobLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float /*rotaryStartAngle*/,
                          float /*rotaryEndAngle*/, juce::Slider& /*slider*/) override {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
        if (radius <= 0.0f)
            return;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

        // Body
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.0f);

        // Pointer: -135deg = fully dry, +135deg = fully wet (standard knob sweep)
        constexpr float startAngle = -2.356194f;  // -3π/4
        constexpr float endAngle = 2.356194f;     // +3π/4
        const float angle = startAngle + sliderPosProportional * (endAngle - startAngle);
        const float pointerR = radius - 2.0f;
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        const float px = cx + std::sin(angle) * pointerR;
        const float py = cy - std::cos(angle) * pointerR;
        g.drawLine(cx, cy, px, py, 1.5f);
    }

    int getSliderThumbRadius(juce::Slider&) override {
        return 6;
    }

    static MixKnobLookAndFeel& getInstance() {
        static MixKnobLookAndFeel instance;
        return instance;
    }
};

// Slider subclass that returns a dynamic tooltip showing both the current
// gain value and the meter's peak-hold dB level.
class GainSliderWithMeterTooltip : public juce::Slider {
  public:
    GainSliderWithMeterTooltip(juce::Slider::SliderStyle style,
                               juce::Slider::TextEntryBoxPosition textPos,
                               const magda::LevelMeter& meter)
        : juce::Slider(style, textPos), meter_(meter) {}

    juce::String getTooltip() override {
        const double gainDb = getValue();
        const float peakDb = meter_.getPeakDb();
        const juce::String peakStr =
            peakDb <= -59.5f ? juce::String("-inf") : juce::String::formatted("%+.1f", peakDb);
        juce::String tip = juce::String::formatted("Gain: %+.1f dB    Peak: ", gainDb) + peakStr +
                           juce::String(" dB");
        if (stagingInfo_.isNotEmpty())
            tip += "\n" + stagingInfo_;
        return tip;
    }

    // Extra tooltip line describing the most recent gain-staging move on this
    // device. Set by DeviceSlotComponent; empty when not in a staging pass.
    void setStagingInfo(juce::String info) {
        stagingInfo_ = std::move(info);
    }

  private:
    const magda::LevelMeter& meter_;
    juce::String stagingInfo_;
};

// Unified visual recipe for all node-header SvgButtons. Pass the accent
// colour used as the active-state pill background. Set toggling=false for
// stateless buttons (menus, one-shots).
inline void applyHeaderIconStyle(magda::SvgButton& btn, juce::Colour activeBg,
                                 bool toggling = true) {
    btn.setIconPadding(2.0f);
    btn.setOriginalColor(juce::Colour(0xFFB3B3B3));
    btn.setNormalColor(juce::Colour(0xFFB3B3B3).withAlpha(0.5f));
    btn.setActiveColor(juce::Colours::white);
    btn.setActiveBackgroundColor(activeBg);
    if (toggling)
        btn.setClickingTogglesState(true);
}

}  // namespace magda::daw::ui::node_header
