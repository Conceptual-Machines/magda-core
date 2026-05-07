#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/transport_api_live.hpp"
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

TEST_CASE("TimelineController creates loop at playhead when activating without a region",
          "[timeline][loop][playhead][regression]") {
    magda::TimelineController controller;
    TestAudioEngineListener listener;
    controller.addAudioEngineListener(&listener);

    controller.dispatch(magda::SetEditPositionEvent{12.5});
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    const auto& loop = controller.getState().loop;
    REQUIRE(loop.enabled);
    REQUIRE(loop.isValid());
    REQUIRE(loop.startTime == Catch::Approx(12.5));
    REQUIRE(loop.endTime == Catch::Approx(14.5));
    REQUIRE(loop.startBeats == Catch::Approx(25.0));
    REQUIRE(loop.endBeats == Catch::Approx(29.0));

    REQUIRE(listener.loopRegionChangedCount == 1);
    REQUIRE(listener.loopEnabledChangedCount == 0);
    REQUIRE(listener.lastLoopStart == Catch::Approx(12.5));
    REQUIRE(listener.lastLoopEnd == Catch::Approx(14.5));
    REQUIRE(listener.lastLoopEnabled);

    controller.removeAudioEngineListener(&listener);
}

TEST_CASE("TimelineController uses playback cursor when activating loop during playback",
          "[timeline][loop][playhead][regression]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetEditPositionEvent{8.0});
    controller.dispatch(magda::StartPlaybackEvent{});
    controller.dispatch(magda::SetPlaybackPositionEvent{11.25});
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    const auto& loop = controller.getState().loop;
    REQUIRE(loop.enabled);
    REQUIRE(loop.startTime == Catch::Approx(11.25));
    REQUIRE(loop.endTime == Catch::Approx(13.25));
}

TEST_CASE("TimelineController does not create invalid loop on too-short timeline",
          "[timeline][loop][regression]") {
    magda::TimelineController controller;

    controller.dispatch(magda::SetTimelineLengthEvent{0.005});
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    REQUIRE_FALSE(controller.getState().loop.enabled);
    REQUIRE_FALSE(controller.getState().loop.isValid());
}

TEST_CASE("TimelineController moves existing disabled loop to playhead when activating",
          "[timeline][loop][playhead][regression]") {
    magda::TimelineController controller;
    TestAudioEngineListener listener;
    controller.addAudioEngineListener(&listener);

    controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
    controller.dispatch(magda::SetLoopEnabledEvent{false});
    listener.loopRegionChangedCount = 0;
    listener.loopEnabledChangedCount = 0;

    controller.dispatch(magda::SetEditPositionEvent{12.0});
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    REQUIRE(controller.getState().loop.enabled);
    REQUIRE(controller.getState().loop.startTime == Catch::Approx(12.0));
    REQUIRE(controller.getState().loop.endTime == Catch::Approx(16.0));
    REQUIRE(listener.loopRegionChangedCount == 1);
    REQUIRE(listener.loopEnabledChangedCount == 0);
    REQUIRE(listener.lastLoopEnabled);

    controller.removeAudioEngineListener(&listener);
}

TEST_CASE("TimelineController keeps active loop unchanged on duplicate enable",
          "[timeline][loop]") {
    magda::TimelineController controller;
    TestAudioEngineListener listener;
    controller.addAudioEngineListener(&listener);

    controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
    listener.loopRegionChangedCount = 0;
    listener.loopEnabledChangedCount = 0;

    controller.dispatch(magda::SetEditPositionEvent{12.0});
    controller.dispatch(magda::SetLoopEnabledEvent{true});

    REQUIRE(controller.getState().loop.enabled);
    REQUIRE(controller.getState().loop.startTime == Catch::Approx(4.0));
    REQUIRE(controller.getState().loop.endTime == Catch::Approx(8.0));
    REQUIRE(listener.loopRegionChangedCount == 0);
    REQUIRE(listener.loopEnabledChangedCount == 0);

    controller.removeAudioEngineListener(&listener);
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
