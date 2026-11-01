// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TrackWidthPresetButtons.hpp"

#include <BinaryData.h>

#include <utility>

#include "../themes/ActiveTheme.hpp"
#include "TrackHeightPresetsPanel.hpp"

namespace magda {

TrackWidthPresetButtons::TrackWidthPresetButtons() {
    // Clicks reach mouseDown only through the buttons, so a click on a gap can't set the flag.
    setInterceptsMouseClicks(false, true);

    setupButton(0, "TrackWidthNarrow", BinaryData::track_width_narrow_svg,
                BinaryData::track_width_narrow_svgSize, "Narrow track width",
                &Config::getNarrowTrackWidth);
    setupButton(1, "TrackWidthMedium", BinaryData::track_width_medium_svg,
                BinaryData::track_width_medium_svgSize, "Medium track width",
                &Config::getMediumTrackWidth);
    setupButton(2, "TrackWidthWide", BinaryData::track_height_large_svg,
                BinaryData::track_height_large_svgSize, "Wide track width",
                &Config::getWideTrackWidth);
}

void TrackWidthPresetButtons::setupButton(size_t index, const juce::String& name,
                                          const char* svgData, size_t svgSize,
                                          const juce::String& tooltip,
                                          int (Config::*preset)() const) {
    auto& button = buttons_[index];
    button = std::make_unique<SvgButton>(name, svgData, svgSize);
    button->setOriginalColor(juce::Colour(0xFFB3B3B3));
    button->setNormalColor(ActiveTheme::TEXT_DIM);
    button->setHoverColor(ActiveTheme::TEXT_SECONDARY);
    button->setPressedColor(ActiveTheme::ACCENT_PRIMARY);
    button->setBorderColor(ActiveTheme::BORDER);
    button->setBorderThickness(1.0f);
    button->setIconPadding(5.0f);
    button->setWantsKeyboardFocus(false);
    button->setTooltip(tooltip);
    button->onClick = [this, preset]() {
        if (!std::exchange(rightClicked_, false) && onWidthChosen)
            onWidthChosen((Config::getInstance().*preset)());
    };
    button->addMouseListener(this, false);
    addAndMakeVisible(*button);
}

juce::Rectangle<int> TrackWidthPresetButtons::boundsInRail(juce::Rectangle<int> rail) {
    // The rails' bottom group: two 28px buttons, 6px apart, 8px off the edge.
    constexpr int kBottomGroup = 8 + 28 + 6 + 28;
    constexpr int kGap = 6;
    constexpr int kWidth = 28;
    const int height = BUTTON_HEIGHT * NUM_PRESETS;
    return {rail.getCentreX() - kWidth / 2, rail.getBottom() - kBottomGroup - kGap - height, kWidth,
            height};
}

void TrackWidthPresetButtons::resized() {
    auto stack = getLocalBounds();
    for (auto& button : buttons_)
        button->setBounds(stack.removeFromTop(BUTTON_HEIGHT));
}

void TrackWidthPresetButtons::mouseDown(const juce::MouseEvent& event) {
    rightClicked_ = event.mods.isPopupMenu();
    if (rightClicked_)
        showTrackWidthPresets(event.eventComponent->getScreenBounds());
}

}  // namespace magda
