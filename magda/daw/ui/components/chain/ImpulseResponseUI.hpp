#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Tracktion Engine Impulse Response plugin
 *
 * Layout:
 *   Top: [IR name label] [LOAD button]
 *   Row 1: Low Cut, High Cut, Q
 *   Row 2: Gain, Mix, (empty)
 */
class ImpulseResponseUI : public juce::Component, public juce::FileDragAndDropTarget {
  public:
    ImpulseResponseUI();
    ~ImpulseResponseUI() override = default;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    void setIRName(const juce::String& name);

    std::function<void(int paramIndex, float value)> onParameterChanged;
    std::function<void()> onLoadIRRequested;
    std::function<void(const juce::File&)> onFileDropped;

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

  private:
    struct SliderWithLabel {
        juce::Label label;
        LinkableTextSlider slider;

        explicit SliderWithLabel(TextSlider::Format fmt = TextSlider::Format::Decimal)
            : slider(fmt) {}
    };

    // IR file controls
    juce::Label irNameLabel_;
    juce::TextButton loadButton_;

    SliderWithLabel gain_{TextSlider::Format::Decibels};
    SliderWithLabel lowCut_;
    SliderWithLabel highCut_;
    SliderWithLabel mix_;
    SliderWithLabel filterQ_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImpulseResponseUI)
};

}  // namespace magda::daw::ui
