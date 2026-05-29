#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "magda/daw/ui/components/timeline/LoopMarkerInteraction.hpp"
#include "magda/daw/ui/components/timeline/TimelineComponent.hpp"

class ArrangeBeatNativeTest final : public juce::UnitTest {
  public:
    ArrangeBeatNativeTest() : juce::UnitTest("Arrange Beat Native Tests", "magda") {}

    void runTest() override {
        beginTest("Loop marker start drag uses host positions");

        magda::LoopMarkerInteraction interaction;
        double changedStart = -1.0;
        double changedEnd = -1.0;
        int repaintCount = 0;

        magda::LoopMarkerInteraction::Host host;
        host.pixelToPosition = [](int pixel) { return static_cast<double>(pixel) / 10.0; };
        host.positionToPixel = [](double position) {
            return static_cast<int>(std::round(position * 10.0));
        };
        host.snapPosition = [](double position) { return std::round(position); };
        host.onLoopChanged = [&](double start, double end) {
            changedStart = start;
            changedEnd = end;
        };
        host.onRepaint = [&] { ++repaintCount; };
        host.maxPosition = 128.0;
        host.topBorderY = 0;
        host.topBorderThreshold = 8;

        interaction.setHost(host);
        interaction.setLoopRange(4.0, 12.0, true);

        expect(interaction.mouseDown(40, 0));
        expect(interaction.mouseDrag(63, 0));
        expect(interaction.mouseUp(63, 0));
        expect(std::abs(changedStart - 6.0) < 1.0e-9);
        expect(std::abs(changedEnd - 12.0) < 1.0e-9);
        expectEquals(repaintCount, 1);

        beginTest("Loop marker region drag clamps in host positions");

        interaction.setLoopRange(100.0, 120.0, true);
        changedStart = -1.0;
        changedEnd = -1.0;

        expect(interaction.mouseDown(1100, 0));
        expect(interaction.mouseDrag(1300, 0));
        expect(interaction.mouseUp(1300, 0));
        expect(std::abs(changedStart - 108.0) < 1.0e-9);
        expect(std::abs(changedEnd - 128.0) < 1.0e-9);

        beginTest("Timeline loop API reports beats");

        magda::TimelineComponent timeline;
        double callbackStart = -1.0;
        double callbackEnd = -1.0;
        timeline.onLoopRegionBeatsChanged = [&](double startBeats, double endBeats) {
            callbackStart = startBeats;
            callbackEnd = endBeats;
        };

        timeline.setTempo(120.0);
        timeline.setTimelineLength(120.0);
        timeline.setZoom(12.0);
        timeline.setSize(1600, 160);
        timeline.setLoopRegionBeats(8.0, 16.0);

        expect(std::abs(callbackStart - 8.0) < 1.0e-9);
        expect(std::abs(callbackEnd - 16.0) < 1.0e-9);

        juce::Image image{juce::Image::ARGB, 1600, 160, true};
        juce::Graphics graphics{image};
        timeline.paint(graphics);
        expect(true);

        beginTest("Timeline time selection API accepts beats");

        timeline.setTimeSelectionBeats(4.0, 24.0);
        timeline.paint(graphics);
        expect(true);
    }
};

static ArrangeBeatNativeTest arrangeBeatNativeTest;
