#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/views/ArrangementViewportPolicy.hpp"

namespace magda {
namespace {

class ArrangementVerticalScrollTest final : public juce::UnitTest {
  public:
    ArrangementVerticalScrollTest()
        : juce::UnitTest("Arrangement Vertical Scroll Tests", "magda") {}

    void runTest() override {
        beginTest("Short track content keeps the empty viewport area interactive");
        expectEquals(arrangement_viewport::panelHeight(80, 480), 480);

        beginTest("Short track content does not need a vertical scrollbar");
        expect(!arrangement_viewport::needsVerticalScrollBar(80, 480));
        expect(!arrangement_viewport::needsVerticalScrollBar(480, 480));

        beginTest("Overflowing track content keeps its full height and scrollbar");
        expectEquals(arrangement_viewport::panelHeight(720, 480), 720);
        expect(arrangement_viewport::needsVerticalScrollBar(720, 480));

        beginTest("Normalized scrollbar travel maps to the real scrollable extent");
        expectEquals(arrangement_viewport::scrollOffset(0.0, 0.2, 1000, 500), 0);
        expectEquals(arrangement_viewport::scrollOffset(0.4, 0.2, 1000, 500), 250);
        expectEquals(arrangement_viewport::scrollOffset(0.8, 0.2, 1000, 500), 500);
        expectEquals(arrangement_viewport::scrollOffset(0.8, 0.2, 80, 480), 0);
    }
};

ArrangementVerticalScrollTest arrangementVerticalScrollTest;

}  // namespace
}  // namespace magda
