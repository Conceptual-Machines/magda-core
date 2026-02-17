#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Tracktion Engine Low/High-Pass Filter
 *
 * 1 row layout: Frequency slider + LP/HP mode toggle
 */
class FilterUI : public juce::Component {
  public:
    FilterUI();
    ~FilterUI() override = default;

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

    SliderWithLabel frequency_;
    // Mode toggle: 0 = lowpass, 1 = highpass (virtual param index 1)
    juce::Label modeLabel_;
    juce::TextButton modeButton_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilterUI)
};

}  // namespace magda::daw::ui
