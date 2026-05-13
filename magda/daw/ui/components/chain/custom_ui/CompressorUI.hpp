#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Tracktion Engine Compressor
 *
 * 2 rows x 3 columns layout:
 *   Row 1: Threshold, Ratio, Attack
 *   Row 2: Release, Output, Sidechain
 */
class CompressorUI : public juce::Component {
  public:
    CompressorUI();
    ~CompressorUI() override = default;

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

    SliderWithLabel threshold_;
    SliderWithLabel ratio_;
    SliderWithLabel attack_;
    SliderWithLabel release_;
    SliderWithLabel output_{TextSlider::Format::Decibels};
    SliderWithLabel sidechain_{TextSlider::Format::Decibels};
    // SC Trigger toggle button (virtual param index 6)
    juce::Label scTriggerLabel_;
    juce::TextButton scTriggerButton_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorUI)
};

}  // namespace magda::daw::ui
