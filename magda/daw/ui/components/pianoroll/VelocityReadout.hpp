#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

/**
 * @brief Transient velocity value badge shown while editing note velocity with
 *        the modifier + wheel gesture (#1706). It positions itself just above an
 *        anchor point (the cursor), shows the value, then hides after a short
 *        delay. Both MIDI editors add one as a child of their note grid.
 */
class VelocityReadout : public juce::Component, private juce::Timer {
  public:
    VelocityReadout() {
        setInterceptsMouseClicks(false, false);
        setVisible(false);
    }

    /** Show `velocity` centred above `anchor` (in the parent's coordinates) and
     *  (re)start the auto-hide timer. */
    void flash(int velocity, juce::Point<int> anchor) {
        velocity_ = velocity;

        constexpr int w = 46;
        constexpr int h = 20;
        int x = anchor.x - w / 2;
        int y = anchor.y - h - 10;  // sit above the cursor
        if (auto* parent = getParentComponent()) {
            x = juce::jlimit(2, juce::jmax(2, parent->getWidth() - w - 2), x);
            y = juce::jlimit(2, juce::jmax(2, parent->getHeight() - h - 2), y);
        }
        setBounds(x, y, w, h);
        setVisible(true);
        toFront(false);
        repaint();
        startTimer(kHideDelayMs);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).withAlpha(0.92f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText("v " + juce::String(velocity_), getLocalBounds(), juce::Justification::centred,
                   false);
    }

  private:
    static constexpr int kHideDelayMs = 700;

    void timerCallback() override {
        stopTimer();
        setVisible(false);
    }

    int velocity_ = 100;
};

}  // namespace magda
