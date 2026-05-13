#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Tracktion Engine Phaser
 *
 * 1 row x 3 columns layout:
 *   Depth, Rate, Feedback
 *
 * All parameters are non-automatable CachedValues exposed as virtual params.
 */
class PhaserUI : public juce::Component {
  public:
    PhaserUI();
    ~PhaserUI() override = default;

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
    SliderWithLabel rate_;
    SliderWithLabel feedback_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaserUI)
};

}  // namespace magda::daw::ui
