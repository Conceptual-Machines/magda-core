#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/common/SvgButton.hpp"
#include "ui/components/mixer/LevelMeter.hpp"
#include "ui/components/mixer/LevelMeterScale.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui::node_header {

inline SmallButtonLookAndFeel& getDeltaSoloButtonLookAndFeel() {
    static SmallButtonLookAndFeel instance(10.5f);
    return instance;
}

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
        g.setColour(ActiveTheme::getSecondaryTextColour());
        g.fillRect(static_cast<float>(x), thumbY, static_cast<float>(width), thumbHeight);
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
    // The side strip's MIX box: caption over the wet percentage, dragged
    // vertically like the knob it replaced.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float /*rotaryStartAngle*/,
                          float /*rotaryEndAngle*/, juce::Slider& /*slider*/) override {
        auto box = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(0.5f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.drawRoundedRectangle(box, 4.0f, 1.0f);

        auto& fonts = FontManager::getInstance();
        auto area = box.toNearestInt().reduced(2, 3);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getMonoFont(9.0f));
        g.drawText("MIX", area.removeFromTop(area.getHeight() / 2), juce::Justification::centred,
                   false);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
        g.setFont(fonts.getMonoFont(11.0f).boldened());
        g.drawText(juce::String(juce::roundToInt(sliderPosProportional * 100.0f)), area,
                   juce::Justification::centred, false);
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
        : juce::Slider(style, textPos), meter_(meter) {
        // The slider overlays the device/rack meter inside a scrolling panel.
        // Keep passive wheel/trackpad gestures scrolling that panel until the
        // user deliberately grabs this slider.
        setScrollWheelEnabled(false);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isLeftButtonDown())
            setScrollWheelEnabled(true);
        juce::Slider::mouseDown(event);
    }

    void mouseExit(const juce::MouseEvent& event) override {
        setScrollWheelEnabled(false);
        juce::Slider::mouseExit(event);
    }

    double valueToProportionOfLength(double value) override {
        const auto minDb = getMinimum();
        const auto maxDb = getMaximum();
        value = juce::jlimit(minDb, maxDb, value);

        if (maxDb <= magda::level_meter_scale::maxDb || value <= 0.0)
            return magda::level_meter_scale::dbFillProportion(value);

        const auto zeroDbPos = magda::level_meter_scale::dbFillProportion(0.0);
        const auto headroomNorm = juce::jlimit(0.0, 1.0, value / maxDb);
        return zeroDbPos + headroomNorm * (1.0 - zeroDbPos);
    }

    double proportionOfLengthToValue(double proportion) override {
        const auto minDb = getMinimum();
        const auto maxDb = getMaximum();
        proportion = juce::jlimit(0.0, 1.0, proportion);

        const auto zeroDbPos = magda::level_meter_scale::dbFillProportion(0.0);
        if (maxDb <= magda::level_meter_scale::maxDb || proportion <= zeroDbPos) {
            const auto db = magda::level_meter_scale::meterPosToDb(static_cast<float>(proportion));
            return juce::jlimit(minDb, maxDb, static_cast<double>(db));
        }

        const auto headroomNorm = (proportion - zeroDbPos) / (1.0 - zeroDbPos);
        return juce::jlimit(minDb, maxDb, headroomNorm * maxDb);
    }

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
    btn.setNormalColor(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY).withAlpha(0.5f));
    btn.setActiveColor(juce::Colours::white);
    btn.setActiveBackgroundColor(activeBg);
    if (toggling)
        btn.setClickingTogglesState(true);
}

/** How a v1 device-shell icon button behaves: a plain action, a toggle that
 *  turns blue while on, power (green while on), or close (red on hover). */
enum class DeviceIcon { Action, Toggle, Power, Close, Window };

/** The v1 device icon button: 30x26 hit area, 5px radius, a 12px glyph (10px for
 *  close, 11px for the plug-in window), grey at rest, light on a dark fill when
 *  hovered. @p glyphKey is the colour the asset draws its glyph in. */
inline void applyDeviceIconStyle(magda::SvgButton& btn, DeviceIcon kind,
                                 juce::Colour glyphKey = juce::Colour(0xFFB3B3B3),
                                 ColourRole activeRole = ActiveTheme::DEVICE_BLUE,
                                 float buttonHeight = 26.0f) {
    // Glyph sizes are for the 26px device header; smaller buttons scale them.
    const float scale = buttonHeight / 26.0f;
    // The spec's 17 / 14 / 16px glyphs were drawn for a 1000px device; a slot
    // here is about half that, so the icons scale down with it.
    const float glyph = kind == DeviceIcon::Close    ? 10.0f
                        : kind == DeviceIcon::Window ? 11.0f
                                                     : 12.0f;
    btn.setIconPadding((26.0f - glyph) * scale / 2.0f);
    btn.setCornerRadius(5.0f * scale);
    btn.setOriginalColor(glyphKey);
    btn.setNormalColor(ActiveTheme::DEVICE_ICON);
    btn.setHoverColor(kind == DeviceIcon::Close ? ActiveTheme::DEVICE_RED
                                                : ActiveTheme::DEVICE_ICON_HOVER);
    btn.setPressedColor(ActiveTheme::DEVICE_ICON_HOVER);
    btn.setHoverBackgroundColor(ActiveTheme::DEVICE_ICON_HOVER_BG);
    btn.setActiveColor(kind == DeviceIcon::Power ? ActiveTheme::DEVICE_GREEN : activeRole);
    btn.setActiveBackgroundColor(juce::Colours::transparentBlack);
    if (kind == DeviceIcon::Toggle || kind == DeviceIcon::Power) {
        btn.setStateTint(kind == DeviceIcon::Power ? ActiveTheme::DEVICE_GREEN : activeRole);
        btn.setClickingTogglesState(true);
    }
}

/** @brief Chipless mute (track chain header, rack chain rows): the device icon style, with the
 *  speaker turning warning yellow and crossed (pair with syncMuteGlyph) when muted, or an M
 *  under the Letters preference. Matches the chip mute in MasterSpeakerButton.hpp. */
inline void applyDeviceMuteStyle(magda::SvgButton& btn, float buttonHeight = 26.0f) {
    applyDeviceIconStyle(btn, DeviceIcon::Toggle, juce::Colour(0xFFB3B3B3),
                         ActiveTheme::STATUS_WARNING, buttonHeight);
    btn.setStateColourReplacement(juce::Colour(0xFF1E1E1E), ActiveTheme::DEVICE_ICON,
                                  ActiveTheme::STATUS_WARNING);
    btn.setLetterGlyph("M");
}

/** @brief Chipless solo: the ring, amber when soloed, or an S under the Letters preference. */
inline void applyDeviceSoloStyle(magda::SvgButton& btn, float buttonHeight = 26.0f) {
    applyDeviceIconStyle(btn, DeviceIcon::Toggle, juce::Colour(0xFFB3B3B3),
                         ActiveTheme::DEVICE_AMBER, buttonHeight);
    btn.setLetterGlyph("S");
}

/** @brief A text toggle with no fill (chain M / S): the glyph colour carries the state and
 *  hover fills like a device icon button. Set textColourOnId for the on colour. */
class GlyphToggleLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool highlighted, bool down) override {
        if (!highlighted && !down)
            return;
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_ICON_HOVER_BG));
        g.fillRoundedRectangle(button.getLocalBounds().toFloat(), 5.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool highlighted,
                        bool /*down*/) override {
        const auto colour =
            button.getToggleState()
                ? button.findColour(juce::TextButton::textColourOnId)
                : ActiveTheme::getColour(highlighted ? ActiveTheme::DEVICE_ICON_HOVER
                                                     : ActiveTheme::DEVICE_ICON);
        g.setColour(colour);
        g.setFont(FontManager::getInstance().getMonoFont(12.0f).boldened());
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred,
                   false);
    }

    static GlyphToggleLookAndFeel& getInstance() {
        static GlyphToggleLookAndFeel instance;
        return instance;
    }
};

}  // namespace magda::daw::ui::node_header
