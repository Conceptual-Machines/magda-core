#include <juce_gui_basics/juce_gui_basics.h>

#include <utility>
#include <vector>

#include "magda/daw/ui/components/timeline/TimeRuler.hpp"

/**
 * The piano roll's ruler: the lens area above the time ticks zooms, and a double-click there
 * fits the view to the loop or the clip. A click on the ticks moves the playhead and a
 * double-click on them places the edit cursor.
 */

namespace {

juce::MouseEvent eventAt(juce::Component& c, int x, int y, juce::ModifierKeys mods, int clicks) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            juce::Point<float>(static_cast<float>(x), static_cast<float>(y)), mods,
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                            juce::Point<float>(static_cast<float>(x), static_cast<float>(y)),
                            juce::Time::getCurrentTime(), clicks, false);
}

class TimeRulerClicksTest final : public juce::UnitTest {
  public:
    TimeRulerClicksTest() : juce::UnitTest("Time ruler clicks", "magda") {}

    void runTest() override {
        magda::TimeRuler ruler;
        ruler.setSize(400, 40);
        ruler.setTempo(120.0);
        ruler.setTimelineLength(60.0);
        ruler.setZoom(50.0);

        int playheadClicks = 0;
        int editCursorRequests = 0;
        ruler.onPlayheadPositionClicked = [&](double, bool) { ++playheadClicks; };
        ruler.onEditCursorRequested = [&](double, bool) { ++editCursorRequests; };
        std::vector<std::pair<double, double>> fits;
        ruler.onZoomToLoopRequested = [&](double start, double end) {
            fits.emplace_back(start, end);
        };

        const auto pressed = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
        const int lens = 4;
        const int ticks = ruler.getHeight() - 2;
        const auto click = [&](int y, int clicks) {
            ruler.mouseDown(eventAt(ruler, 120, y, pressed, clicks));
            ruler.mouseUp(eventAt(ruler, 120, y, pressed, clicks));
        };

        beginTest("A click in the lens area places nothing");
        click(lens, 1);
        expectEquals(playheadClicks, 0);
        expectEquals(editCursorRequests, 0);

        beginTest("A double-click in the lens area fits nothing without a clip");
        click(lens, 2);
        ruler.mouseDoubleClick(eventAt(ruler, 120, lens, pressed, 2));
        expectEquals(editCursorRequests, 0);
        expect(fits.empty());

        beginTest("A double-click in the lens area fits the clip, or its loop");
        ruler.setRelativeMode(true);
        ruler.setClipLength(8.0);
        ruler.mouseDoubleClick(eventAt(ruler, 300, lens, pressed, 2));
        ruler.setLoopRegion(1.0, 2.0, true, true);
        ruler.mouseDoubleClick(eventAt(ruler, 300, lens, pressed, 2));
        expectEquals(static_cast<int>(fits.size()), 2);
        if (fits.size() == 2) {
            expectWithinAbsoluteError(fits[0].second, 8.0, 1e-9);
            expectWithinAbsoluteError(fits[1].first, 1.0, 1e-9);
            expectWithinAbsoluteError(fits[1].second, 3.0, 1e-9);
        }
        expectEquals(editCursorRequests, 0);

        beginTest("A click on the time ticks moves the playhead");
        click(ticks, 1);
        expectEquals(playheadClicks, 1);
        expectEquals(editCursorRequests, 0);

        beginTest("A double-click on the time ticks places the edit cursor");
        click(ticks, 2);
        ruler.mouseDoubleClick(eventAt(ruler, 120, ticks, pressed, 2));
        expectEquals(editCursorRequests, 1);
    }
};

TimeRulerClicksTest timeRulerClicksTest;

}  // namespace
