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
    button->setNormalColor(ActiveTheme::TEXT_SECONDARY);
    button->setHoverColor(ActiveTheme::TEXT_PRIMARY);
    button->setPressedColor(ActiveTheme::ACCENT_PRIMARY);
    button->setBorderColor(ActiveTheme::BORDER);
    button->setBorderThickness(1.0f);
    button->setIconPadding(3.0f);
    button->setWantsKeyboardFocus(false);
    button->setTooltip(tooltip);
    button->onClick = [this, preset]() {
        if (!std::exchange(rightClicked_, false) && onWidthChosen)
            onWidthChosen((Config::getInstance().*preset)());
    };
    button->addMouseListener(this, false);
    addAndMakeVisible(*button);
}

juce::Rectangle<int> TrackWidthPresetButtons::getBoxBounds() const {
    return getLocalBounds().withTrimmedTop(2).withTrimmedBottom(1);
}

void TrackWidthPresetButtons::paint(juce::Graphics& g) {
    const auto box = getBoxBounds().toFloat().reduced(0.5f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_STOP_ROW));
    g.fillRoundedRectangle(box, 6.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_CONTROL_BORDER));
    g.drawRoundedRectangle(box, 6.0f, 1.0f);
}

void TrackWidthPresetButtons::resized() {
    constexpr int kButtonWidth = 22;
    auto row = getBoxBounds().reduced(2).removeFromRight(kButtonWidth * 3);
    for (auto& button : buttons_)
        button->setBounds(row.removeFromLeft(kButtonWidth));
}

void TrackWidthPresetButtons::mouseDown(const juce::MouseEvent& event) {
    rightClicked_ = event.mods.isPopupMenu();
    if (rightClicked_)
        showTrackWidthPresets(event.eventComponent->getScreenBounds());
}

}  // namespace magda
