#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/transport_api_live.hpp"

namespace {

struct FakeEngine {
    bool playing = false;
    bool recording = false;
    bool looping = false;
    double positionBeats = 0.0;
    double beatsPerBar = 4.0;

    magda::TransportApiLive::EngineState state() {
        return {.playing = [this] { return playing; },
                .recording = [this] { return recording; },
                .looping = [this] { return looping; },
                .positionBeats = [this] { return positionBeats; },
                .beatsPerBar = [this] { return beatsPerBar; }};
    }
};

}  // namespace

TEST_CASE("Transport reads come from an engine with no Edit", "[remote][transport][2784]") {
    FakeEngine engine;
    magda::TransportApiLive transport;
    transport.setEngineState(engine.state());

    engine.playing = true;
    engine.recording = true;
    engine.looping = true;
    engine.positionBeats = 6.5;
    CHECK(transport.isPlaying());
    CHECK(transport.isRecording());
    CHECK(transport.isLoopEnabled());
    CHECK(transport.getPositionBeats() == 6.5);

    engine.beatsPerBar = 3.5;
    CHECK(transport.beatsAtBarOffset(6.5, 2) == 13.5);
    CHECK(transport.beatsAtBarOffset(6.5, -1) == 3.0);
}

TEST_CASE("Transport seek and record go through their dispatchers", "[remote][transport][2784]") {
    magda::TransportApiLive transport;
    double seekedTo = -1.0;
    std::vector<bool> recordRequests;
    transport.setSeekDispatcher([&](double beats) { seekedTo = beats; });
    transport.setRecordDispatcher([&](bool recording) { recordRequests.push_back(recording); });

    transport.setPositionBeats(12.0);
    transport.setRecording(true);
    transport.setRecording(false);
    CHECK(seekedTo == 12.0);
    CHECK(recordRequests == std::vector<bool>{true, false});
}

TEST_CASE("An engine with no Edit reports transport changes to listeners",
          "[remote][transport][2784]") {
    magda::TransportApiLive transport;
    int notified = 0;
    const auto token = transport.addStateListener([&] { ++notified; });
    transport.notifyStateChanged();
    CHECK(notified == 1);
    transport.removeStateListener(token);
    transport.notifyStateChanged();
    CHECK(notified == 1);
}

TEST_CASE("Bar seeks on an engine with no Edit follow its meter and clamp at the start",
          "[remote][transport][2556]") {
    FakeEngine engine;
    engine.positionBeats = 7.5;
    engine.beatsPerBar = 3.0;
    magda::TransportApiLive transport;
    transport.setEngineState(engine.state());
    double seekedTo = -1.0;
    transport.setSeekDispatcher([&](double beats) { seekedTo = beats; });

    transport.seekBars(2);
    CHECK(seekedTo == 13.5);
    transport.seekBars(-5);
    CHECK(seekedTo == 0.0);
}

TEST_CASE("An unwired transport is safe without an engine", "[remote][transport][2918]") {
    magda::TransportApiLive transport;
    CHECK_FALSE(transport.isPlaying());
    CHECK_FALSE(transport.isRecording());
    CHECK_FALSE(transport.isLoopEnabled());
    CHECK(transport.getPositionBeats() == 0.0);
    CHECK(transport.beatsAtBarOffset(7.5, 2) == 7.5);
    transport.play();
    transport.stop();
    transport.setRecording(true);
    transport.setLoopEnabled(true);
    transport.setPositionBeats(12.0);
    transport.seekBars(2);
    CHECK(transport.getPositionBeats() == 0.0);
}

TEST_CASE("Transport dispatchers preserve state and application overrides",
          "[remote][transport][2918]") {
    FakeEngine engine;
    magda::TransportApiLive transport;
    transport.setEngineState(engine.state());
    transport.setPlayDispatcher([&] { engine.playing = true; });
    transport.setStopDispatcher([&] {
        engine.playing = false;
        engine.recording = false;
    });
    transport.setLoopDispatcher([&](bool enabled) { engine.looping = enabled; });
    transport.setRecordDispatcher([&](bool enabled) { engine.recording = enabled; });
    transport.setSeekDispatcher([&](double beats) { engine.positionBeats = beats; });

    transport.play();
    CHECK(transport.isPlaying());
    transport.setRecording(true);
    CHECK(transport.isRecording());
    transport.setLoopEnabled(true);
    CHECK(transport.isLoopEnabled());
    transport.setLoopEnabled(false);
    CHECK_FALSE(transport.isLoopEnabled());
    transport.setPositionBeats(10.0);
    CHECK(transport.getPositionBeats() == 10.0);
    transport.stop();
    CHECK_FALSE(transport.isPlaying());
    CHECK_FALSE(transport.isRecording());

    // MainWindow replaces the engine dispatcher with TimelineController's path.
    bool controllerCalled = false;
    transport.setPlayDispatcher([&] { controllerCalled = true; });
    transport.play();
    CHECK(controllerCalled);
    CHECK_FALSE(engine.playing);
}

TEST_CASE("Bar seeking delegates meter changes to the engine", "[remote][transport][2918]") {
    FakeEngine engine;
    engine.positionBeats = 11.5;
    auto state = engine.state();
    double queriedBeats = -1.0;
    int queriedBars = 0;
    state.beatsAtBarOffset = [&](double beats, int bars) {
        queriedBeats = beats;
        queriedBars = bars;
        return 8.0;
    };
    magda::TransportApiLive transport;
    transport.setEngineState(std::move(state));
    transport.setSeekDispatcher([&](double beats) { engine.positionBeats = beats; });
    transport.seekBars(-1);
    CHECK(queriedBeats == 11.5);
    CHECK(queriedBars == -1);
    CHECK(engine.positionBeats == 8.0);
}

TEST_CASE("Transport observation follows listeners, source replacement and teardown",
          "[remote][transport][2918]") {
    bool firstObserving = false;
    bool secondObserving = false;
    {
        magda::TransportApiLive transport;
        magda::TransportApiLive::EngineState first;
        first.refreshStateSource = [&](bool observing) { firstObserving = observing; };
        transport.setEngineState(std::move(first));
        CHECK_FALSE(firstObserving);
        CHECK(transport.addStateListener({}) == 0);
        CHECK_FALSE(firstObserving);

        const auto firstToken = transport.addStateListener([] {});
        const auto secondToken = transport.addStateListener([] {});
        CHECK(firstObserving);
        transport.removeStateListener(firstToken);
        CHECK(firstObserving);

        magda::TransportApiLive::EngineState second;
        second.refreshStateSource = [&](bool observing) { secondObserving = observing; };
        transport.setEngineState(std::move(second));
        CHECK_FALSE(firstObserving);
        CHECK(secondObserving);
        transport.removeStateListener(secondToken);
        CHECK_FALSE(secondObserving);

        transport.addStateListener([] {});
        CHECK(secondObserving);
    }
    CHECK_FALSE(secondObserving);
}

TEST_CASE("Transport listeners may unsubscribe during notification", "[remote][transport][2918]") {
    magda::TransportApiLive transport;
    int firstCalls = 0;
    int secondCalls = 0;
    int firstToken = 0;
    firstToken = transport.addStateListener([&] {
        ++firstCalls;
        transport.removeStateListener(firstToken);
    });
    transport.addStateListener([&] { ++secondCalls; });
    transport.notifyStateChanged();
    transport.notifyStateChanged();
    CHECK(firstCalls == 1);
    CHECK(secondCalls == 2);
}
