#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magica {

/**
 * Custom LookAndFeel for the mixer channel strips
 * Uses custom SVG icons for faders
 */
class MixerLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    MixerLookAndFeel();
    ~MixerLookAndFeel() override = default;

    // Override linear slider drawing for custom fader appearance
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    // Get the thumb size for layout calculations
    int getSliderThumbRadius(juce::Slider& slider) override;

    // Override rotary slider drawing for custom knob appearance
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

    // Override ComboBox drawing for compact dropdown appearance
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX,
                      int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    // Smaller arrow button for ComboBox
    void drawComboBoxArrow(juce::Graphics& g, juce::Rectangle<int> arrowZone);

  private:
    std::unique_ptr<juce::Drawable> faderThumb_;
    std::unique_ptr<juce::Drawable> faderTrack_;
    std::unique_ptr<juce::Drawable> knobBody_;
    std::unique_ptr<juce::Drawable> knobPointer_;

    void loadIcons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerLookAndFeel)
};

}  // namespace magica
