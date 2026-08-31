#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/transport_api_live.hpp"
#include "magda/daw/ui/layout/LayoutConfig.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"

namespace {

class TestAudioEngineListener : public magda::AudioEngineListener {
  public:
    void onTransportPlay(double) override {}
    void onTransportStop(double) override {}
    void onTransportPause() override {}
    void onTransportRecord(double) override {}
    void onTransportStopRecording() override {}
    void onEditPositionChanged(double) override {}
    void onTempoChanged(double) override {}
    void onTimeSignatureChanged(int, int) override {}

    void onLoopRegionChanged(double startTime, double endTime, bool enabled) override {
        ++loopRegionChangedCount;
        lastLoopStart = startTime;
        lastLoopEnd = endTime;
        lastLoopEnabled = enabled;
    }

    void onLoopEnabledChanged(bool enabled) override {
        ++loopEnabledChangedCount;
        lastLoopEnabled = enabled;
    }

    int loopRegionChangedCount = 0;
    int loopEnabledChangedCount = 0;
    double lastLoopStart = -1.0;
    double lastLoopEnd = -1.0;
    bool lastLoopEnabled = false;
};

}  // namespace

TEST_CASE("Enabling loop with no region seeds a 1-bar region at the playhead",
          "[timeline][loop][regression]") {
    magda::TimelineController controller;
    TestAudioEngineListener listener;
    controller.addAudioEngineListener(&listener);

    controller.dispatch(magda::SetEditPositionEvent{12.5});
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    const auto& loop = controller.getState().loop;
    REQUIRE(loop.enabled);
    REQUIRE(loop.isValid());
    // 1 bar at 120 BPM 4/4 = 2.0s
    REQUIRE(loop.startTime == Catch::Approx(12.5));
    REQUIRE(loop.endTime == Catch::Approx(14.5));

    REQUIRE(listener.loopRegionChangedCount == 1);
    REQUIRE(listener.lastLoopEnabled);

    controller.removeAudioEngineListener(&listener);
}

TEST_CASE("Timeline range state treats beats as authoritative", "[timeline][beats][regression]") {
    magda::LoopRegion loop;
    loop.setFromBeats(8.0, 16.0, 120.0);
    loop.startTime = 99.0;
    loop.endTime = 99.0;

    REQUIRE(loop.isValid());
    REQUIRE(loop.getDurationBeats() == Catch::Approx(8.0));

    magda::PunchRegion punch;
    punch.setFromBeats(4.0, 12.0, 120.0);
    punch.startTime = 50.0;
    punch.endTime = 1.0;

    REQUIRE(punch.isValid());
    REQUIRE(punch.getDurationBeats() == Catch::Approx(8.0));

    magda::TimeSelection selection;
    selection.setFromBeats(2.0, 6.0, 120.0);
    selection.startTime = 10.0;
    selection.endTime = 0.0;

    REQUIRE(selection.isActive());
    REQUIRE(selection.getDurationBeats() == Catch::Approx(4.0));
}

TEST_CASE("TimelineController playhead and ranges are beat-authoritative",
          "[timeline][beats][controller]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTempoEvent{120.0});
    controller.dispatch(magda::SetTimelineLengthBeatsEvent{128.0});
    controller.dispatch(magda::SetEditPositionBeatsEvent{8.0});
    controller.dispatch(magda::StartPlaybackEvent{});
    controller.dispatch(magda::SetPlaybackPositionBeatsEvent{10.0});
    controller.dispatch(magda::SetLoopRegionBeatsEvent{12.0, 20.0});
    controller.dispatch(magda::SetTimeSelectionBeatsEvent{24.0, 32.0, {}});

    const auto& state = controller.getState();
    REQUIRE(state.timelineLengthBeats == Catch::Approx(128.0));
    REQUIRE(state.timelineLength == Catch::Approx(64.0));
    REQUIRE(state.playhead.editPositionBeats == Catch::Approx(8.0));
    REQUIRE(state.playhead.editPosition == Catch::Approx(4.0));
    REQUIRE(state.playhead.playbackPositionBeats == Catch::Approx(10.0));
    REQUIRE(state.playhead.playbackPosition == Catch::Approx(5.0));
    REQUIRE(state.loop.startBeats == Catch::Approx(12.0));
    REQUIRE(state.loop.endBeats == Catch::Approx(20.0));
    REQUIRE(state.selection.startBeats == Catch::Approx(24.0));
    REQUIRE(state.selection.endBeats == Catch::Approx(32.0));

    controller.dispatch(magda::SetTempoEvent{60.0});

    REQUIRE(state.timelineLengthBeats == Catch::Approx(128.0));
    REQUIRE(state.timelineLength == Catch::Approx(128.0));
    REQUIRE(state.playhead.editPositionBeats == Catch::Approx(8.0));
    REQUIRE(state.playhead.editPosition == Catch::Approx(8.0));
    REQUIRE(state.playhead.playbackPositionBeats == Catch::Approx(10.0));
    REQUIRE(state.playhead.playbackPosition == Catch::Approx(10.0));
    REQUIRE(state.loop.startBeats == Catch::Approx(12.0));
    REQUIRE(state.loop.startTime == Catch::Approx(12.0));
    REQUIRE(state.selection.endBeats == Catch::Approx(32.0));
    REQUIRE(state.selection.endTime == Catch::Approx(32.0));
}

TEST_CASE("TimelineController fractional beat ranges survive tempo changes",
          "[timeline][beats][tempo][regression]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTempoEvent{127.5});
    controller.dispatch(magda::SetTimelineLengthBeatsEvent{512.0});
    controller.dispatch(magda::SetEditPositionBeatsEvent{33.375});
    controller.dispatch(magda::StartPlaybackEvent{});
    controller.dispatch(magda::SetPlaybackPositionBeatsEvent{41.625});
    controller.dispatch(magda::SetLoopRegionBeatsEvent{7.25, 63.75});
    controller.dispatch(magda::SetTimeSelectionBeatsEvent{91.125, 143.875, {2, 4}});

    const auto& state = controller.getState();
    REQUIRE(state.playhead.editPositionBeats == Catch::Approx(33.375));
    REQUIRE(state.playhead.playbackPositionBeats == Catch::Approx(41.625));
    REQUIRE(state.loop.startBeats == Catch::Approx(7.25));
    REQUIRE(state.loop.endBeats == Catch::Approx(63.75));
    REQUIRE(state.selection.startBeats == Catch::Approx(91.125));
    REQUIRE(state.selection.endBeats == Catch::Approx(143.875));

    controller.dispatch(magda::SetTempoEvent{73.0});

    REQUIRE(state.playhead.editPositionBeats == Catch::Approx(33.375));
    REQUIRE(state.playhead.playbackPositionBeats == Catch::Approx(41.625));
    REQUIRE(state.loop.startBeats == Catch::Approx(7.25));
    REQUIRE(state.loop.endBeats == Catch::Approx(63.75));
    REQUIRE(state.selection.startBeats == Catch::Approx(91.125));
    REQUIRE(state.selection.endBeats == Catch::Approx(143.875));
    REQUIRE(state.loop.startTime == Catch::Approx(7.25 * 60.0 / 73.0));
    REQUIRE(state.selection.endTime == Catch::Approx(143.875 * 60.0 / 73.0));
}

TEST_CASE("Anchored beat zoom keeps bar one pinned to the gutter",
          "[timeline][zoom][beats][regression]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTimelineLengthBeatsEvent{256.0});
    controller.dispatch(magda::ViewportResizedEvent{900, 600});
    controller.dispatch(magda::SetScrollPositionEvent{0, 0});
    controller.dispatch(
        magda::SetZoomAnchoredBeatsEvent{20.0, 0.0, magda::LayoutConfig::TIMELINE_LEFT_PADDING});

    const auto& state = controller.getState();
    REQUIRE(state.zoom.scrollX == 0);
    REQUIRE(state.zoom.horizontalZoom == Catch::Approx(20.0));
}

TEST_CASE("Anchored beat zoom preserves the selected beat's screen position",
          "[timeline][zoom][beats]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTimelineLengthBeatsEvent{512.0});
    controller.dispatch(magda::ViewportResizedEvent{1000, 600});

    constexpr double anchorBeats = 24.0;
    constexpr double pixelsPerBeat = 18.0;
    constexpr int anchorScreenX = 320;
    controller.dispatch(
        magda::SetZoomAnchoredBeatsEvent{pixelsPerBeat, anchorBeats, anchorScreenX});

    const auto& state = controller.getState();
    const int anchorContentX = static_cast<int>(anchorBeats * state.zoom.horizontalZoom) +
                               magda::LayoutConfig::TIMELINE_LEFT_PADDING;

    REQUIRE(state.zoom.horizontalZoom == Catch::Approx(pixelsPerBeat));
    REQUIRE(anchorContentX - state.zoom.scrollX == anchorScreenX);
}

TEST_CASE("Zoom-to-fit beat range is independent of tempo", "[timeline][zoom][beats][tempo]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTempoEvent{120.0});
    controller.dispatch(magda::SetTimelineLengthBeatsEvent{256.0});
    controller.dispatch(magda::ViewportResizedEvent{1280, 720});
    controller.dispatch(magda::ZoomToFitBeatsEvent{32.0, 96.0, 0.10});

    const auto& state = controller.getState();
    const double zoomBeforeTempoChange = state.zoom.horizontalZoom;
    const int scrollBeforeTempoChange = state.zoom.scrollX;

    controller.dispatch(magda::SetTempoEvent{83.0});

    REQUIRE(state.zoom.horizontalZoom == Catch::Approx(zoomBeforeTempoChange));
    REQUIRE(state.zoom.scrollX == scrollBeforeTempoChange);
    REQUIRE(state.timelineLengthBeats == Catch::Approx(256.0));
}

TEST_CASE("TimelineController arrangement sections are beat-authoritative",
          "[timeline][beats][sections]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTempoEvent{120.0});
    controller.dispatch(magda::SetTimelineLengthBeatsEvent{64.0});
    controller.dispatch(magda::AddSectionBeatsEvent{"Verse", 8.0, 16.0, juce::Colours::green});

    const auto& state = controller.getState();
    REQUIRE(state.sections.size() == 1);
    REQUIRE(state.sections[0].startBeats == Catch::Approx(8.0));
    REQUIRE(state.sections[0].endBeats == Catch::Approx(16.0));
    REQUIRE(state.sections[0].startTime == Catch::Approx(4.0));

    controller.dispatch(magda::MoveSectionBeatsEvent{0, 12.0});
    REQUIRE(state.sections[0].startBeats == Catch::Approx(12.0));
    REQUIRE(state.sections[0].endBeats == Catch::Approx(20.0));

    controller.dispatch(magda::ResizeSectionBeatsEvent{0, 10.0, 18.0});
    REQUIRE(state.sections[0].startBeats == Catch::Approx(10.0));
    REQUIRE(state.sections[0].endBeats == Catch::Approx(18.0));
    REQUIRE(state.sections[0].startTime == Catch::Approx(5.0));

    controller.dispatch(magda::SetTempoEvent{60.0});
    REQUIRE(state.sections[0].startBeats == Catch::Approx(10.0));
    REQUIRE(state.sections[0].endBeats == Catch::Approx(18.0));
    REQUIRE(state.sections[0].startTime == Catch::Approx(10.0));
    REQUIRE(state.sections[0].endTime == Catch::Approx(18.0));
}

TEST_CASE("Enabling loop with an existing valid region keeps it in place",
          "[timeline][loop][regression]") {
    magda::TimelineController controller;
    TestAudioEngineListener listener;

    // Pre-existing loop region, disabled.
    controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
    controller.dispatch(magda::SetLoopEnabledEvent{false});

    controller.addAudioEngineListener(&listener);

    // Move the playhead far away from the loop region.
    controller.dispatch(magda::SetEditPositionEvent{12.0});

    // Re-enable the loop. The region must NOT relocate to the playhead.
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    const auto& loop = controller.getState().loop;
    REQUIRE(loop.enabled);
    REQUIRE(loop.startTime == Catch::Approx(4.0));
    REQUIRE(loop.endTime == Catch::Approx(8.0));

    // Toggling enabled-only should fire onLoopEnabledChanged, not onLoopRegionChanged.
    REQUIRE(listener.loopRegionChangedCount == 0);
    REQUIRE(listener.loopEnabledChangedCount == 1);
    REQUIRE(listener.lastLoopEnabled);

    controller.removeAudioEngineListener(&listener);
}

TEST_CASE("Disabling loop preserves the region for later re-enable",
          "[timeline][loop][regression]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
    controller.dispatch(magda::SetLoopEnabledEvent{false});

    const auto& loop = controller.getState().loop;
    REQUIRE_FALSE(loop.enabled);
    REQUIRE(loop.isValid());
    REQUIRE(loop.startTime == Catch::Approx(4.0));
    REQUIRE(loop.endTime == Catch::Approx(8.0));

    controller.dispatch(magda::SetLoopEnabledEvent{true});
    REQUIRE(loop.enabled);
    REQUIRE(loop.startTime == Catch::Approx(4.0));
    REQUIRE(loop.endTime == Catch::Approx(8.0));
}

TEST_CASE("Enabling loop is a no-op when already enabled with same region", "[timeline][loop]") {
    magda::TimelineController controller;
    TestAudioEngineListener listener;

    controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
    controller.addAudioEngineListener(&listener);

    controller.dispatch(magda::SetLoopEnabledEvent{true});

    REQUIRE(listener.loopRegionChangedCount == 0);
    REQUIRE(listener.loopEnabledChangedCount == 0);

    controller.removeAudioEngineListener(&listener);
}

TEST_CASE("Enabling loop is skipped when timeline is too short to host even a tiny region",
          "[timeline][loop][regression]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTimelineLengthEvent{0.005});
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    REQUIRE_FALSE(controller.getState().loop.enabled);
    REQUIRE_FALSE(controller.getState().loop.isValid());
}

TEST_CASE("TransportApiLive dispatches loop changes through controller path",
          "[timeline][loop][api][regression]") {
    magda::TransportApiLive transport;

    int dispatchCount = 0;
    bool lastEnabled = false;
    transport.setLoopDispatcher([&](bool enabled) {
        ++dispatchCount;
        lastEnabled = enabled;
    });

    transport.setLoopEnabled(true);

    REQUIRE(dispatchCount == 1);
    REQUIRE(lastEnabled);
}

TEST_CASE("SetLoopEnabledEvent ignores active time selection",
          "[timeline][loop][api][regression]") {
    // A scripted/programmatic loop toggle must not promote a UI time selection into
    // a loop region. Selection-promotion is a UI button affordance, not a controller
    // semantic. Routing TransportApi -> SetLoopEnabledEvent (instead of through
    // MainView::setLoopEnabled) is what enforces this; this test locks the controller
    // half of the contract.
    magda::TimelineController controller;

    controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
    controller.dispatch(magda::SetLoopEnabledEvent{false});

    // User has a time selection active in the UI somewhere unrelated to the loop.
    controller.dispatch(magda::SetTimeSelectionEvent{20.0, 24.0, {}});

    controller.dispatch(magda::SetLoopEnabledEvent{true});

    const auto& loop = controller.getState().loop;
    REQUIRE(loop.enabled);
    // Region must be the original [4, 8], NOT promoted to the selection [20, 24].
    REQUIRE(loop.startTime == Catch::Approx(4.0));
    REQUIRE(loop.endTime == Catch::Approx(8.0));
}
