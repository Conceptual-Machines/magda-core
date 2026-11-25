// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include "compiled/DrumVoiceLayout.hpp"

using magda::daw::ui::DrumVoiceGeometry;
using magda::daw::ui::drumVoiceSections;
using magda::daw::ui::kDrumVoicePluginIds;
using magda::daw::ui::ParamControlStyle;

TEST_CASE("Every drum voice tab keeps whole text cells in two rows", "[ui][drum][3435]") {
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr int kCellH = 48;
    // Padding, the tab row, and two rows of text cells.
    const int height = 2 * DrumVoiceGeometry::kSpace + DrumVoiceGeometry::kTitleHeight +
                       DrumVoiceGeometry::kCellGap + 2 * kCellH + DrumVoiceGeometry::kCellGap;
    REQUIRE(DrumVoiceGeometry::cellHeight(ParamControlStyle::Text) == kCellH);

    for (const char* pluginId : kDrumVoicePluginIds) {
        const auto sections = drumVoiceSections(pluginId);
        const auto geometry =
            DrumVoiceGeometry::of({0, 0, 600, height}, ParamControlStyle::Text, sections, false);
        const auto shown =
            DrumVoiceGeometry::of({0, 0, 600, height}, ParamControlStyle::Text, sections);
        CHECK(geometry.cells.empty());
        for (const auto& cell : shown.cells) {
            INFO(pluginId);
            CHECK(cell.getHeight() == kCellH);
            CHECK(cell.getBottom() <= height - DrumVoiceGeometry::kSpace);
        }
    }
}
