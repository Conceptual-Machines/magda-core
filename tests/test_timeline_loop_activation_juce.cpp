#include <juce_core/juce_core.h>

#include <cmath>

#include "SharedTestEngine.hpp"
#include "magda/daw/api/transport_api_live.hpp"
#include "magda/daw/project/ProjectManager.hpp"
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

class ScopedProjectLoopSettings {
  public:
    ScopedProjectLoopSettings() {
        const auto& info = magda::ProjectManager::getInstance().getCurrentProjectInfo();
        enabled = info.loopEnabled;
        startBeats = info.loopStartBeats;
        endBeats = info.loopEndBeats;
    }

    ~ScopedProjectLoopSettings() {
        magda::ProjectManager::getInstance().setLoopSettings(enabled, startBeats, endBeats);
    }

  private:
    bool enabled = false;
    double startBeats = 0.0;
    double endBeats = 0.0;
};

void expectNear(juce::UnitTest& test, double actual, double expected, const juce::String& label) {
    constexpr double tolerance = 1.0e-9;
    test.expect(std::abs(actual - expected) <= tolerance, label + " expected " +
                                                              juce::String(expected, 6) + ", got " +
                                                              juce::String(actual, 6));
}

}  // namespace

class TimelineLoopActivationJuceTest final : public juce::UnitTest {
  public:
    TimelineLoopActivationJuceTest()
        : juce::UnitTest("Timeline Loop Activation Integration Tests", "magda") {}

    void runTest() override {
        testApiLoopTogglePreservesExistingRegionWithActiveSelection();
        testApiLoopToggleSeedsDefaultRegionWhenNoneExists();
        testApiLoopToggleUpdatesTracktionTransportWithoutMovingLoopRange();
        testLoopPlaybackStartsBeforeLoopWithoutSnapping();
        testLoopPlaybackStartsAfterLoopWithoutSnappingBack();
    }

  private:
    void testApiLoopTogglePreservesExistingRegionWithActiveSelection() {
        beginTest("TransportApi loop dispatcher preserves an existing region");

        ScopedProjectLoopSettings projectLoopGuard;
        magda::TimelineController controller;

        controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
        controller.dispatch(magda::SetLoopEnabledEvent{false});
        controller.dispatch(magda::SetTimeSelectionEvent{20.0, 24.0, {}});

        TestAudioEngineListener listener;
        controller.addAudioEngineListener(&listener);

        magda::TransportApiLive transport;
        transport.setLoopDispatcher([&controller](bool enabled) {
            controller.dispatch(magda::SetLoopEnabledEvent{enabled});
        });

        transport.setLoopEnabled(true);

        const auto& loop = controller.getState().loop;
        expect(loop.enabled, "Loop should be enabled through the API dispatcher");
        expect(loop.isValid(), "Existing loop region should remain valid");
        expectNear(*this, loop.startTime, 4.0, "Loop start");
        expectNear(*this, loop.endTime, 8.0, "Loop end");
        expect(listener.loopRegionChangedCount == 0,
               "Pure enable should not notify a region relocation");
        expect(listener.loopEnabledChangedCount == 1,
               "Pure enable should notify the engine of the enabled-state change");
        expect(listener.lastLoopEnabled, "Listener should see looping enabled");

        controller.removeAudioEngineListener(&listener);
    }

    void testApiLoopToggleSeedsDefaultRegionWhenNoneExists() {
        beginTest("TransportApi loop dispatcher seeds a default region at the playhead");

        ScopedProjectLoopSettings projectLoopGuard;
        magda::TimelineController controller;
        TestAudioEngineListener listener;
        controller.addAudioEngineListener(&listener);

        controller.dispatch(magda::SetEditPositionEvent{12.5});

        magda::TransportApiLive transport;
        transport.setLoopDispatcher([&controller](bool enabled) {
            controller.dispatch(magda::SetLoopEnabledEvent{enabled});
        });

        transport.setLoopEnabled(true);

        const auto& loop = controller.getState().loop;
        expect(loop.enabled, "Loop should be enabled through the API dispatcher");
        expect(loop.isValid(), "Loop toggle should create a valid default region");
        expectNear(*this, loop.startTime, 12.5, "Default loop start");
        expectNear(*this, loop.endTime, 14.5, "Default loop end");
        expect(listener.loopRegionChangedCount == 1,
               "Creating a default region should notify the audio engine once");
        expectNear(*this, listener.lastLoopStart, 12.5, "Listener loop start");
        expectNear(*this, listener.lastLoopEnd, 14.5, "Listener loop end");
        expect(listener.lastLoopEnabled, "Listener should see looping enabled");

        controller.removeAudioEngineListener(&listener);
    }

    void testApiLoopToggleUpdatesTracktionTransportWithoutMovingLoopRange() {
        beginTest("TransportApi loop dispatcher updates Tracktion without moving the loop range");

        ScopedProjectLoopSettings projectLoopGuard;
        auto& engine = magda::test::getSharedEngine();
        magda::test::resetTransport(engine);
        engine.setLooping(false);
        engine.setLoopRegion(0.0, 0.01);

        magda::TimelineController controller;
        controller.addAudioEngineListener(&engine);

        controller.dispatch(magda::SetLoopRegionEvent{4.0, 8.0});
        controller.dispatch(magda::SetLoopEnabledEvent{false});
        controller.dispatch(magda::SetTimeSelectionEvent{20.0, 24.0, {}});

        magda::TransportApiLive transport;
        transport.setLoopDispatcher([&controller](bool enabled) {
            controller.dispatch(magda::SetLoopEnabledEvent{enabled});
        });

        transport.setLoopEnabled(true);

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Shared Tracktion edit should be available");
        if (edit != nullptr) {
            const auto loopRange = edit->getTransport().getLoopRange();
            expectNear(*this, loopRange.getStart().inSeconds(), 4.0, "Tracktion loop start");
            expectNear(*this, loopRange.getEnd().inSeconds(), 8.0, "Tracktion loop end");
            expect(engine.isLooping(), "Tracktion transport should be looping");
        }

        controller.removeAudioEngineListener(&engine);
        engine.setLooping(false);
        engine.setLoopRegion(0.0, 0.01);
    }

    void testLoopPlaybackStartsBeforeLoopWithoutSnapping() {
        beginTest("Loop playback starts before the loop without snapping to loop start");

        auto& engine = magda::test::getSharedEngine();
        magda::test::resetTransport(engine);
        engine.setLoopRegion(4.0, 8.0);
        engine.setLooping(true);

        magda::TimelineController controller;
        controller.addAudioEngineListener(&engine);

        controller.dispatch(magda::SetEditPositionEvent{2.0});
        controller.dispatch(magda::StartPlaybackEvent{});

        expectNear(*this, engine.getCurrentPosition(), 2.0, "Tracktion play position before loop");
        expect(engine.isLooping(), "Looping should remain armed for roll-in playback");

        controller.dispatch(magda::StopPlaybackEvent{});
        controller.removeAudioEngineListener(&engine);
        engine.setLooping(false);
        engine.setLoopRegion(0.0, 0.01);
    }

    void testLoopPlaybackStartsAfterLoopWithoutSnappingBack() {
        beginTest("Loop playback starts after the loop without snapping back");

        auto& engine = magda::test::getSharedEngine();
        magda::test::resetTransport(engine);
        engine.setLoopRegion(4.0, 8.0);
        engine.setLooping(true);

        magda::TimelineController controller;
        controller.addAudioEngineListener(&engine);

        controller.dispatch(magda::SetEditPositionEvent{10.0});
        controller.dispatch(magda::StartPlaybackEvent{});

        expectNear(*this, engine.getCurrentPosition(), 10.0, "Tracktion play position after loop");
        expect(engine.isLooping(),
               "Transport loop toggle should remain enabled after past-loop start");

        controller.dispatch(magda::StopPlaybackEvent{});
        controller.removeAudioEngineListener(&engine);
        engine.setLooping(false);
        engine.setLoopRegion(0.0, 0.01);
    }
};

static TimelineLoopActivationJuceTest timelineLoopActivationJuceTestInstance;
