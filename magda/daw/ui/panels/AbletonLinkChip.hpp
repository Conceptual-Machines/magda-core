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
class AbletonLinkChip : public juce::Button, private juce::Timer {
  public:
    AbletonLinkChip();

    ~AbletonLinkChip() override;

    void setOn(bool on);
    /** @brief Blink the LED while a start waits for the session's downbeat. */
    void setWaiting(bool waiting);
    bool isOn() const {
        return on_;
    }

  private:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void timerCallback() override;

    juce::Path lettering_;
    bool on_ = false;
    bool ledDim_ = false;
};

}  // namespace magda::daw::ui
