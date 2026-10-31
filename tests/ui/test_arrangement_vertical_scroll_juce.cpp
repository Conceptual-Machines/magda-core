// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/views/ArrangementViewportPolicy.hpp"

namespace magda {
namespace {

class ArrangementVerticalScrollTest final : public juce::UnitTest {
  public:
    ArrangementVerticalScrollTest()
        : juce::UnitTest("Arrangement Vertical Scroll Tests", "magda") {}

    void runTest() override {
        constexpr int dropSpace = arrangement_viewport::kDropSpaceBelowTracks;

        beginTest("Short track content keeps the empty viewport area interactive");
        expectEquals(arrangement_viewport::panelHeight(80, 480), 480);

        beginTest("Short track content does not need a vertical scrollbar");
        expect(!arrangement_viewport::needsVerticalScrollBar(80, 480));
        expect(!arrangement_viewport::needsVerticalScrollBar(480 - dropSpace, 480));

        beginTest("Rows that fill the viewport still scroll down to the drop space");
        expectEquals(arrangement_viewport::panelHeight(480, 480), 480 + dropSpace);
        expect(arrangement_viewport::needsVerticalScrollBar(480, 480));

        beginTest("Overflowing track content keeps its full height, drop space and scrollbar");
        expectEquals(arrangement_viewport::panelHeight(720, 480), 720 + dropSpace);
        expect(arrangement_viewport::needsVerticalScrollBar(720, 480));

        beginTest("Normalized scrollbar travel maps to the real scrollable extent");
        const int maxScroll = 1000 + dropSpace - 500;
        expectEquals(arrangement_viewport::scrollOffset(0.0, 0.2, 1000, 500), 0);
        expectEquals(arrangement_viewport::scrollOffset(0.4, 0.2, 1000, 500), maxScroll / 2);
        expectEquals(arrangement_viewport::scrollOffset(0.8, 0.2, 1000, 500), maxScroll);
        expectEquals(arrangement_viewport::scrollOffset(0.8, 0.2, 80, 480), 0);
    }
};

ArrangementVerticalScrollTest arrangementVerticalScrollTest;

}  // namespace
}  // namespace magda
