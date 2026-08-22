#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Custom LookAndFeel for the mixer channel strips
 * Uses custom SVG icons for faders
 */
class MixerLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    MixerLookAndFeel();
    ~MixerLookAndFeel() override;

    // A LookAndFeel is not a Component, so it never hears the look-and-feel
    // broadcast that a live theme switch sends. The colours below are pushed
    // into its own colour table and would keep the palette this instance was
    // constructed under; owners re-apply them from their lookAndFeelChanged().
    void refreshThemeColours();

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
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerLookAndFeel)
};

}  // namespace magda
