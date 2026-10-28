// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

/** @brief A tab's actions, right-aligned in the Settings window's tab row (#3162). */
class SettingsToolbar : public juce::Component {
  public:
    void addButton(juce::Button& button) {
        buttons_.push_back(&button);
        addAndMakeVisible(button);
    }

    void resized() override {
        auto row = getLocalBounds();
        for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
            if (!(*it)->isVisible())
                continue;
            const int w = juce::GlyphArrangement::getStringWidthInt(
                              FontManager::getInstance().getUIFont(13.5f), (*it)->getButtonText()) +
                          32;
            (*it)->setBounds(row.removeFromRight(w));
            row.removeFromRight(6);
        }
    }

  private:
    std::vector<juce::Button*> buttons_;
};

/** @brief A dot and a short state, shown at the right of the tab row. */
class SettingsStatusBadge : public juce::Component {
  public:
    void setStatus(const juce::String& text, bool live) {
        if (text == text_ && live == live_)
            return;
        text_ = text;
        live_ = live;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto font = FontManager::getInstance().getMonoFont(11.5f);
        const int textW = juce::GlyphArrangement::getStringWidthInt(font, text_);
        auto area = getLocalBounds().removeFromRight(textW + 14);
        g.setColour(live_ ? ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE)
                          : ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.fillEllipse(area.removeFromLeft(14).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
        g.setColour(live_ ? ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE)
                          : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.setFont(font);
        g.drawText(text_, area, juce::Justification::centredRight, false);
    }

  private:
    juce::String text_;
    bool live_ = false;
};

}  // namespace magda
