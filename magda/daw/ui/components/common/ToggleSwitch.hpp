#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "../../themes/ActiveTheme.hpp"

namespace magda::daw::ui {

/** @brief A pill switch: the knob sits right on an accent track while on. */
class ToggleSwitch : public juce::Component, public juce::SettableTooltipClient {
  public:
    static constexpr int kWidth = 26;
    static constexpr int kHeight = 14;

    void setOn(bool on) {
        if (on_ != on) {
            on_ = on;
            repaint();
        }
    }
    bool isOn() const {
        return on_;
    }

    /// Fires on click; the owner flips the model and calls setOn().
    std::function<void()> onClick;

    void paint(juce::Graphics& g) override {
        const auto track = getLocalBounds().toFloat().withSizeKeepingCentre(
            static_cast<float>(kWidth), static_cast<float>(kHeight));
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        const float radius = track.getHeight() * 0.5f;
        if (on_) {
            g.setColour(
                ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(track, radius);
        } else {
            g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE).withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(track, radius);
            g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withMultipliedAlpha(alpha));
            g.drawRoundedRectangle(track.reduced(0.5f), radius, 1.0f);
        }
        const float knob = track.getHeight() - 4.0f;
        const float x = on_ ? track.getRight() - 2.0f - knob : track.getX() + 2.0f;
        g.setColour(ActiveTheme::getColour(on_ ? ActiveTheme::TEXT_BRIGHT : ActiveTheme::TEXT_DIM)
                        .withMultipliedAlpha(alpha));
        g.fillEllipse(x, track.getY() + 2.0f, knob, knob);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (isEnabled() && getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

  private:
    bool on_ = false;
};

}  // namespace magda::daw::ui
