#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

/** @brief An inspector section title in spaced capitals, then a hairline to the right edge. */
class SectionHeader : public juce::Component {
  public:
    explicit SectionHeader(const juce::String& text = {}) {
        setText(text);
        setInterceptsMouseClicks(false, false);
    }

    void setText(const juce::String& text) {
        text_ = text.toUpperCase();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto font =
            FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.08f);
        g.setFont(font);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        auto bounds = getLocalBounds();
        const int textW = juce::GlyphArrangement::getStringWidthInt(font, text_);
        g.drawText(text_, bounds.removeFromLeft(textW), juce::Justification::centredLeft, false);

        bounds.removeFromLeft(8);
        if (bounds.getWidth() > 0) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::HAIRLINE));
            g.fillRect(bounds.getX(), bounds.getCentreY(), bounds.getWidth(), 1);
        }
    }

  private:
    juce::String text_;
};

}  // namespace magda::daw::ui
