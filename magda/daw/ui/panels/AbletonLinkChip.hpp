#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <optional>

/**
 * @file AbletonLinkChip.hpp
 * @brief The transport's Ableton Link switch (#929).
 */

namespace magda::daw::ui {

/**
 * @brief A chip with a state LED and the lettering of Ableton's Link button, lit while Link is on.
 *
 * While on, the LED flashes on the session's beats, brighter on the downbeat, and blinks fast
 * while a start waits for one.
 *
 * The lettering is Ableton's artwork, not typed text: their guidelines allow "Link" alone only
 * in that form.
 */
class AbletonLinkChip : public juce::Button, private juce::Timer {
  public:
    /// The waiting blink's half period, which Play blinks at too.
    static constexpr int kBlinkIntervalMs = 150;

    AbletonLinkChip();

    ~AbletonLinkChip() override;

    void setOn(bool on);
    /** @brief Blink the LED while a start waits for the session's downbeat. */
    void setWaiting(bool waiting);

    /// Where the session is in its bar, in beats; nothing leaves the LED steady.
    std::function<std::optional<double>()> barPhase;
    bool isOn() const {
        return on_;
    }

  private:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void timerCallback() override;

    enum class Led : std::uint8_t { Steady, Dim, Beat, Downbeat };

    Led ledNow() const;

    juce::Path lettering_;
    bool on_ = false;
    bool waiting_ = false;
    Led led_ = Led::Steady;
};

}  // namespace magda::daw::ui
