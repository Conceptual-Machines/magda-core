#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/components/timeline/TimelineComponent.hpp"
#include "magda/daw/ui/layout/LayoutConfig.hpp"

namespace {

int getTimelinePaintHeight() {
    const auto& layout = magda::LayoutConfig::getInstance();
    return layout.chordRowHeight + layout.arrangementBarHeight + layout.timeRulerHeight + 48;
}

void paintVisibleSlice(magda::TimelineComponent& timeline, int scrollX) {
    constexpr int imageWidth = 1280;
    const int imageHeight = timeline.getHeight();

    juce::Image image{juce::Image::ARGB, imageWidth, imageHeight, true};
    juce::Graphics graphics{image};
    graphics.addTransform(juce::AffineTransform::translation(static_cast<float>(-scrollX), 0.0f));

    timeline.paint(graphics);
}

}  // namespace

class TimelineExtremeZoomPaintTest final : public juce::UnitTest {
  public:
    TimelineExtremeZoomPaintTest() : juce::UnitTest("Timeline Extreme Zoom Paint Tests", "magda") {}

    void runTest() override {
        beginTest("Paint visible slices of a very wide zoomed timeline");

        magda::TimelineComponent timeline;
        timeline.setTempo(120.0);
        timeline.setTimelineLength(600.0);
        timeline.setZoom(10000.0);
        timeline.setSize(10000000, getTimelinePaintHeight());
        timeline.addSectionBeats("Long section", 0.0, 1200.0, juce::Colours::blue);
        timeline.setLoopRegionBeats(0.0, 1200.0);
        timeline.setTimeSelectionBeats(120.0, 1080.0);

        paintVisibleSlice(timeline, 0);
        paintVisibleSlice(timeline, 5000000);

        expectGreaterThan(timeline.getWidth(), 1000000);
    }
};

static TimelineExtremeZoomPaintTest timelineExtremeZoomPaintTest;
