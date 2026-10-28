// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/ActiveTheme.hpp"

namespace magda::daw::ui {

/** @brief A square header chip that draws a small vector glyph. */
class GlyphChip : public juce::Button {
  public:
    enum class Glyph { Bookmark, Letter };

    /// What Glyph::Letter draws: one or two characters.
    void setLetter(const juce::String& letter) {
        letter_ = letter;
        repaint();
    }

    GlyphChip(const juce::String& name, Glyph glyph) : juce::Button(name), glyph_(glyph) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        g.setColour(ActiveTheme::getColour(highlighted || down ? ActiveTheme::SURFACE_HOVER
                                                               : ActiveTheme::SURFACE)
                        .withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        g.setColour(ActiveTheme::getColour(highlighted ? ActiveTheme::TEXT_PRIMARY
                                                       : ActiveTheme::TEXT_SECONDARY)
                        .withMultipliedAlpha(alpha));
        const auto c = bounds.getCentre();
        if (glyph_ == Glyph::Bookmark) {
            juce::Path p;
            p.startNewSubPath(c.x - 4.0f, c.y - 6.0f);
            p.lineTo(c.x + 4.0f, c.y - 6.0f);
            p.lineTo(c.x + 4.0f, c.y + 6.0f);
            p.lineTo(c.x, c.y + 3.0f);
            p.lineTo(c.x - 4.0f, c.y + 6.0f);
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved));
        } else {
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(letter_, bounds, juce::Justification::centred, false);
        }
    }

  private:
    Glyph glyph_;
    juce::String letter_;
};

}  // namespace magda::daw::ui
