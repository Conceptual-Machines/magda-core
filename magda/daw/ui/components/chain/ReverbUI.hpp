#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Tracktion Engine Reverb
 *
 * 2 rows x 3 columns layout:
 *   Row 1: Room Size, Damping, Width
 *   Row 2: Wet Level, Dry Level, Freeze
 */
class ReverbUI : public juce::Component {
  public:
    ReverbUI();
    ~ReverbUI() override = default;

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

    SliderWithLabel roomSize_;
    SliderWithLabel damping_;
    SliderWithLabel width_;
    SliderWithLabel wetLevel_;
    SliderWithLabel dryLevel_;
    SliderWithLabel freeze_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbUI)
};

}  // namespace magda::daw::ui
