#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

/** @brief Adjoining text segments in a field-style box, the chosen one tinted; segments can be
 * disabled. */
class PillSegments : public juce::Component, public juce::SettableTooltipClient {
  public:
    void setOptions(const juce::StringArray& options) {
        options_ = options;
        enabled_.clear();
        for (int i = 0; i < options_.size(); ++i)
            enabled_.add(true);
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
    void setSegmentEnabled(int index, bool enabled) {
        if (juce::isPositiveAndBelow(index, enabled_.size()) && enabled_[index] != enabled) {
            enabled_.set(index, enabled);
            repaint();
        }
    }

    int getIdealWidth() const {
        int w = 0;
        for (const auto& o : options_)
            w += juce::roundToInt(juce::GlyphArrangement::getStringWidth(font(), o)) + 24;
        return w;
    }

    std::function<void(int index)> onChange;

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);

        g.setFont(font());
        for (int i = 0; i < options_.size(); ++i) {
            const auto seg = segmentBounds(i);
            if (i > 0) {
                g.setColour(ActiveTheme::getBorderColour());
                g.drawVerticalLine(juce::roundToInt(seg.getX()), r.getY() + 1.0f,
                                   r.getBottom() - 1.0f);
            }
            if (i == selected_) {
                g.setColour(ActiveTheme::getAccentColour().withAlpha(0.15f));
                g.fillRect(seg.reduced(0.0f, 1.0f));
            }
            g.setColour(!enabled_[i]     ? ActiveTheme::getColour(ActiveTheme::TEXT_DIM)
                        : i == selected_ ? ActiveTheme::getAccentColour()
                                         : ActiveTheme::getSecondaryTextColour());
            g.drawText(options_[i], seg, juce::Justification::centred, false);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        for (int i = 0; i < options_.size(); ++i) {
            if (segmentBounds(i).contains(e.position) && enabled_[i] && i != selected_) {
                setSelectedIndex(i);
                if (onChange)
                    onChange(i);
                return;
            }
        }
    }

  private:
    static juce::Font font() {
        return FontManager::getInstance().getUIFont(12.0f);
    }

    juce::Rectangle<float> segmentBounds(int index) const {
        const int ideal = juce::jmax(1, getIdealWidth());
        const float scale = static_cast<float>(getWidth()) / static_cast<float>(ideal);
        float x = 0.0f;
        for (int i = 0; i < options_.size(); ++i) {
            const float w =
                (juce::GlyphArrangement::getStringWidth(font(), options_[i]) + 24.0f) * scale;
            if (i == index)
                return {x, 0.0f, w, static_cast<float>(getHeight())};
            x += w;
        }
        return {};
    }

    juce::StringArray options_;
    juce::Array<bool> enabled_;
    int selected_ = 0;
};

}  // namespace magda::daw::ui
