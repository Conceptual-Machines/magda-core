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
