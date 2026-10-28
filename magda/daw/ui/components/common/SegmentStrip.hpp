// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

/** @brief Joined text segments in an inspector field; the selected one takes the accent. */
class SegmentStrip : public juce::Component, public juce::SettableTooltipClient {
  public:
    void setOptions(const juce::StringArray& options) {
        options_ = options;
        repaint();
    }

    void setSelectedIndex(int index) {
        if (selected_ != index) {
            selected_ = index;
            repaint();
        }
    }
    int getSelectedIndex() const {
        return selected_;
    }

    /// Fires when a segment other than the selected one is clicked.
    std::function<void(int index)> onChange;

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(bounds, 2.0f);

        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        for (int i = 0; i < options_.size(); ++i) {
            const auto segment = segmentBounds(i);
            if (i == selected_) {
                g.setColour(accent.withAlpha(0.22f * alpha));
                g.fillRect(segment);
            }
            if (i > 0) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withMultipliedAlpha(alpha));
                g.fillRect(segment.getX(), segment.getY(), 1.0f, segment.getHeight());
            }
            g.setColour(
                (i == selected_ ? accent : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY))
                    .withMultipliedAlpha(alpha));
            g.drawText(options_[i], segment, juce::Justification::centred, false);
        }

        g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (int i = 0; i < options_.size(); ++i) {
            if (segmentBounds(i).contains(e.position) && i != selected_) {
                if (onChange)
                    onChange(i);
                return;
            }
        }
    }

  private:
    juce::Rectangle<float> segmentBounds(int index) const {
        const auto bounds = getLocalBounds().toFloat();
        const float w = bounds.getWidth() / static_cast<float>(juce::jmax(1, options_.size()));
        return bounds.withX(bounds.getX() + w * static_cast<float>(index)).withWidth(w);
    }

    juce::StringArray options_;
    int selected_ = -1;
};

}  // namespace magda::daw::ui
