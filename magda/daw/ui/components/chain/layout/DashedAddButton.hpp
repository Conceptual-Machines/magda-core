#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/** @brief The v1 add target: a dashed outline with "+", and a label when one is set. */
class DashedAddButton : public juce::Button {
  public:
    explicit DashedAddButton(const juce::String& label = {}) : juce::Button(label) {}

    /** Draws the outline in the active accent, for a drop hovering over it. */
    void setHighlighted(bool highlighted) {
        if (highlighted_ != highlighted) {
            highlighted_ = highlighted;
            repaint();
        }
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override {
        const bool hot = isMouseOver || isButtonDown;
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        juce::Path outline;
        outline.addRoundedRectangle(bounds, 4.0f);
        const float dashes[] = {4.0f, 3.0f};
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(ActiveTheme::getColour(highlighted_ ? ActiveTheme::DEVICE_BLUE
                                           : hot        ? ActiveTheme::DEVICE_LINE2
                                                        : ActiveTheme::DEVICE_LINE));
        g.fillPath(dashed);

        const auto text =
            ActiveTheme::getColour(hot ? ActiveTheme::DEVICE_DIM : ActiveTheme::DEVICE_DIM2);
        g.setColour(text);
        const auto label = getButtonText();
        if (label.isEmpty()) {
            drawPlus(g, getLocalBounds().toFloat().getCentre());
            return;
        }
        auto area = getLocalBounds().reduced(14, 0);
        drawPlus(g, area.removeFromLeft(10).toFloat().getCentre());
        area.removeFromLeft(10);
        g.setFont(FontManager::getInstance().getUIFont(11.5f));
        g.drawText(label, area, juce::Justification::centredLeft, false);
    }

  private:
    static void drawPlus(juce::Graphics& g, juce::Point<float> centre) {
        constexpr float half = 5.0f;
        g.fillRect(juce::Rectangle<float>(half * 2.0f, 1.5f).withCentre(centre));
        g.fillRect(juce::Rectangle<float>(1.5f, half * 2.0f).withCentre(centre));
    }

    bool highlighted_ = false;
};

}  // namespace magda::daw::ui
