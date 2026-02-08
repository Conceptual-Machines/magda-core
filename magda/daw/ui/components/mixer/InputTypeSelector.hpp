#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * @brief Segmented control for selecting input type (Audio or MIDI)
 *
 * Displays two side-by-side segments. The active segment is highlighted
 * with a type-appropriate color (green for Audio, cyan for MIDI).
 */
class InputTypeSelector : public juce::Component {
  public:
    enum class InputType { Audio, MIDI };

    InputTypeSelector();
    ~InputTypeSelector() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    void setInputType(InputType type);
    void setInputTypeSilently(InputType type);
    InputType getInputType() const {
        return currentType_;
    }

    std::function<void(InputType)> onInputTypeChanged;

  private:
    InputType currentType_ = InputType::MIDI;
    bool isHovering_ = false;

    juce::Rectangle<int> getAudioSegmentArea() const;
    juce::Rectangle<int> getMidiSegmentArea() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputTypeSelector)
};

}  // namespace magda
