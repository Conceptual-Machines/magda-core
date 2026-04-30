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
        return juce::String::formatted("Gain: %+.1f dB    Peak: ", gainDb) + peakStr +
               juce::String(" dB");
    }

  private:
    const magda::LevelMeter& meter_;
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
