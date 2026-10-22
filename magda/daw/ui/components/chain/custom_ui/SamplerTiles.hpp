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

/// The cell behind a tile's caption and value: the grid cell's fill and border.
inline void paint(juce::Graphics& g, juce::Rectangle<int> tile, float corner = 5.0f) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(tile.toFloat(), corner);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(tile.toFloat().reduced(0.5f), corner, 1.0f);
}

}  // namespace magda::daw::ui::sampler_tiles
