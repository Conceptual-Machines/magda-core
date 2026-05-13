#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Tracktion Engine Chorus
 *
 * 1 row x 4 columns layout:
 *   Depth, Speed, Width, Mix
 *
 * All parameters are non-automatable CachedValues exposed as virtual params.
 */
class ChorusUI : public juce::Component {
  public:
    ChorusUI();
    ~ChorusUI() override = default;

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

    SliderWithLabel depth_;
    SliderWithLabel speed_;
    SliderWithLabel width_;
    SliderWithLabel mix_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusUI)
};

}  // namespace magda::daw::ui
