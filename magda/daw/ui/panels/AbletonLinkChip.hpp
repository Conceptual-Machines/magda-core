#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * @file AbletonLinkChip.hpp
 * @brief The transport's Ableton Link switch (#929).
 */

namespace magda::daw::ui {

/**
 * @brief A chip with a state LED and the lettering of Ableton's Link button, lit while Link is on.
 *
 * The lettering is Ableton's artwork, not typed text: their guidelines allow "Link" alone only
 * in that form.
 */
class AbletonLinkChip : public juce::Button {
  public:
    AbletonLinkChip();

    void setOn(bool on);
    bool isOn() const {
        return on_;
    }

  private:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    juce::Path lettering_;
    bool on_ = false;
};

}  // namespace magda::daw::ui
