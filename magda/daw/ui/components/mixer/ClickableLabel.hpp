#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * @brief A non-editable juce::Label that fires a callback when clicked.
 *
 * Used for the mixer peak-value readouts: clicking the text resets the peak
 * hold. The base Label is never put into edit mode, so overriding mouseDown
 * does not interfere with text editing.
 */
class ClickableLabel : public juce::Label {
  public:
    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent& event) override {
        if (onClick && event.mods.isLeftButtonDown())
            onClick();
        else
            juce::Label::mouseDown(event);
    }
};

}  // namespace magda
