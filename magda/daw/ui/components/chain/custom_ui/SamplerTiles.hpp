// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

/**
 * @file SamplerTiles.hpp
 * @brief The Sampler faceplate's value tiles, for a faceplate built on it (#3100): a painted
 *        cell with a caption and a frameless value box inside.
 */

namespace magda::daw::ui::sampler_tiles {

constexpr int kWidth = 64;
constexpr int kGap = 4;
constexpr int kMaxHeight = 48;  ///< The grid's text tile, so these read as its cells.

/// A value box that sits inside a painted tile: no frame or fill of its own.
inline void styleValue(TextSlider& slider) {
    slider.setDrawFrame(false);
    slider.setShowFillIndicator(false);
    slider.setBackgroundColour(juce::Colours::transparentBlack);
    slider.setFont(FontManager::getInstance().getUIFontMedium(15.0f));
    slider.setTextColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    slider.setJustification(juce::Justification::centredLeft);
}

inline void styleValue(LinkableTextSlider& slider) {
    styleValue(slider.getSlider());
}

/**
 * @brief A caption and its value inside @p inner, as the height allows (#3144).
 *
 * Caption over value while the value fits under it, sized to the room left. At the height a
 * rack's chain leaves, a wide tile puts the two on one line; a narrow one stacks them in small
 * text, since one line would truncate both.
 */
inline void layoutCaptioned(juce::Label& caption, LinkableTextSlider& value,
                            juce::Rectangle<int> inner, const juce::Font& captionFont,
                            float valueSize) {
    constexpr int kCaptionH = 14;
    constexpr int kSmallCaptionH = 10;
    constexpr int kMinValueH = 13;
    constexpr int kMinOneLineW = 72;
    auto& fonts = FontManager::getInstance();
    const auto small = fonts.getUIFont(9.5f);

    if (inner.getHeight() - kCaptionH >= kMinValueH) {
        caption.setFont(captionFont);
        caption.setBounds(inner.removeFromTop(kCaptionH));
        value.setBounds(inner);
        value.setFont(fonts.getUIFontMedium(juce::jmin(valueSize, float(inner.getHeight()))));
        value.getSlider().setJustification(juce::Justification::centredLeft);
    } else if (inner.getWidth() < kMinOneLineW) {
        caption.setFont(small);
        caption.setBounds(inner.removeFromTop(kSmallCaptionH));
        value.setBounds(inner);
        value.setFont(fonts.getUIFontMedium(juce::jmin(11.0f, float(inner.getHeight()))));
        value.getSlider().setJustification(juce::Justification::centredLeft);
    } else {
        // The caption takes its own width, so the value keeps the rest.
        caption.setFont(small);
        const int captionW =
            juce::GlyphArrangement::getStringWidthInt(small, caption.getText()) + 6;
        caption.setBounds(inner.removeFromLeft(juce::jmin(captionW, inner.getWidth() / 2)));
        value.setBounds(inner);
        value.setFont(fonts.getUIFontMedium(11.0f));
        value.getSlider().setJustification(juce::Justification::centredRight);
    }
}

/// A tile's caption and value inside its painted cell at @p tile, with FM's insets and fonts.
inline void layoutTile(juce::Label& caption, LinkableTextSlider& value, juce::Rectangle<int> tile,
                       int insetX = 9) {
    layoutCaptioned(caption, value, tile.reduced(insetX, 4),
                    FontManager::getInstance().getUIFont(12.0f), 15.0f);
}

/// The cell behind a tile's caption and value: the grid cell's fill and border.
inline void paint(juce::Graphics& g, juce::Rectangle<int> tile, float corner = 5.0f) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(tile.toFloat(), corner);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(tile.toFloat().reduced(0.5f), corner, 1.0f);
}

}  // namespace magda::daw::ui::sampler_tiles
