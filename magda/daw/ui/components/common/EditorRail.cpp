// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorRail.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "BinaryData.h"
#include "SvgButton.hpp"

namespace magda {

namespace {
constexpr int kButtonGap = 4;
constexpr int kGroupGap = 12;
}  // namespace

void EditorRail::styleButton(SvgButton& button) {
    button.setOriginalColor(juce::Colour(0xFFB3B3B3));
    button.setNormalColor(ActiveTheme::TEXT_SECONDARY);
    button.setHoverColor(ActiveTheme::TEXT_PRIMARY);
    button.setTintedChip(ActiveTheme::ACCENT_PRIMARY);
    button.setBorderThickness(1.0f);
    button.setCornerRadius(6.0f);
    button.setIconPadding(7.0f);
}

void EditorRail::syncMonitorToggle(SvgButton& button, bool on) {
    button.updateSvgData(BinaryData::memonitor_svg, BinaryData::memonitor_svgSize);
    button.setOriginalColor(juce::Colour(0xFFB3B3B3));
    button.setTintedChip(ActiveTheme::MIDI_MONITOR);
    button.setActive(on);
}

void EditorRail::setButtons(std::vector<std::vector<SvgButton*>> topGroups,
                            std::vector<SvgButton*> bottom) {
    topGroups_ = std::move(topGroups);
    bottom_ = std::move(bottom);
    for (auto& group : topGroups_)
        for (auto* button : group)
            addChildComponent(button);
    for (auto* button : bottom_)
        addChildComponent(button);
    resized();
}

void EditorRail::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::MIDI_RAIL));
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

void EditorRail::resized() {
    const int x = (kWidth - kButton) / 2;
    int y = x;
    bool placedGroup = false;
    for (auto& group : topGroups_) {
        bool placedInGroup = false;
        for (auto* button : group) {
            if (!button->isVisible())
                continue;
            if (placedGroup && !placedInGroup)
                y += kGroupGap - kButtonGap;
            button->setBounds(x, y, kButton, kButton);
            y += kButton + kButtonGap;
            placedInGroup = true;
        }
        placedGroup = placedGroup || placedInGroup;
    }

    y = getHeight() - x;
    for (auto* button : bottom_) {
        if (!button->isVisible())
            continue;
        y -= kButton;
        button->setBounds(x, y, kButton, kButton);
        y -= kButtonGap;
    }
}

}  // namespace magda
