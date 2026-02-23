#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Utility plugin (Gain, Pan, Phase Invert)
 *
 * Single-row layout: Gain | Pan | Phase Invert
 */
class UtilityUI : public juce::Component {
  public:
    UtilityUI();
    ~UtilityUI() override = default;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    struct SliderWithLabel {
        juce::Label label;
        LinkableTextSlider slider;

        explicit SliderWithLabel(TextSlider::Format fmt = TextSlider::Format::Decimal)
            : slider(fmt) {}
    };

    SliderWithLabel gain_{TextSlider::Format::Decibels};
    SliderWithLabel pan_;

    juce::Label phaseLabel_;
    juce::TextButton phaseButton_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UtilityUI)
};

}  // namespace magda::daw::ui
