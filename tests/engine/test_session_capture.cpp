#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <ranges>
#include <vector>

#include "launch/SessionCapture.hpp"
#include "launch/SessionLauncher.hpp"
#include "transport/TransportClock.hpp"
#include "transport/TransportState.hpp"

/**
 * @file test_session_capture.cpp
 * @brief What the session played, placed from the launcher's own edges (#2464).
 *
 * A driven transport and the real launcher: a case launches a slot and asks
 * where the capture put it, so what is pinned here is that a span begins on the
 * sample the run began on rather than on whatever boundary a poll would have
 * noticed it at.
 */

using Catch::Approx;
using namespace magda;
using namespace magda::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

/// 120 bpm, so a beat is 24,000 samples and no launch position is a whole
/// number of blocks.
constexpr double kBeatSamples = 24000.0;

SlotKey key(int track, int scene = 0) {
    return SlotKey{static_cast<TrackId>(track), scene};
}

/**
 * @brief A transport, a table of handles and a capture, driven a block at a time.
 */
class Rig {
  public:
    explicit Rig(int tracks = 1, SlotFollow follow = {}) {
        transport_.tempo = TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
        handles_.resize(static_cast<std::size_t>(tracks));

        auto table = std::make_shared<LaunchHandleTable>();
        std::map<SlotKey, std::uint64_t> incarnations;

        for (auto track = 0; track < tracks; ++track) {
            table->entries.push_back(
                LaunchHandleTable::Entry{.key = key(track + 1),
                                         .handle = &handles_[static_cast<std::size_t>(track)],
                                         .incarnation = 1,
                                         .follow = follow});
            incarnations[key(track + 1)] = 1;
        }

        feed_.publish(std::move(table));
        requests_.setIncarnations(std::move(incarnations));
    }

    void play(double fromBeat = 0.0) {
        ++transport_.request.generation;
        transport_.request.playing = true;
        transport_.request.locate = true;
        transport_.request.positionBeat = fromBeat;
    }

    void loop(double startBeat, double endBeat) {
        transport_.loop = LoopRange{true, startBeat, endBeat};
    }

    void launch(int track, double monotonicBeat) {
        LaunchRequestQueue::Gesture gesture(requests_);
        gesture.play(key(track), monotonicBeat);
    }

    /// Every slot of @p tracks in one gesture, which is what a scene is.
    void launchScene(const std::vector<int>& tracks, double monotonicBeat) {
        std::vector<SlotKey> followers;
        for (auto track : tracks)
            followers.push_back(key(track));

        LaunchRequestQueue::Gesture gesture(requests_);
        gesture.playScene(followers.front(), followers, monotonicBeat);
    }

    void stopSlot(int track, double monotonicBeat) {
        LaunchRequestQueue::Gesture gesture(requests_);
        gesture.stop(key(track), monotonicBeat);
    }

    /// Roll @p beats of transport, collecting what the launcher publishes.
    /// @p collecting false leaves the edges in the lane, which is a frame that
    /// has not run yet.
    void roll(double beats, bool collecting = true) {
        const auto samples = static_cast<int>(beats * kBeatSamples);

        for (auto left = samples; left > 0;) {
            const auto callback = std::min(kBlockSize, left);

            for (const auto& segment : clock_.advance(transport_, kSampleRate, callback))
                advanceLaunchHandles(feed_, requests_, segment.block, &runs_);

            if (collecting)
                capture_.update();

            left -= callback;
        }
    }

    SessionCapture& capture() {
        return capture_;
    }

    SlotRunQueue& runs() {
        return runs_;
    }

  private:
    TransportSnapshot transport_;
    TransportClock clock_;

    std::vector<LaunchHandle> handles_;
    LaunchHandleFeed feed_;
    LaunchRequestQueue requests_;
    SlotRunQueue runs_;
    SessionCapture capture_{runs_};
};

}  // namespace

TEST_CASE("a captured run begins on the beat its launch fired", "[engine][capture]") {
    // A launch quantized to each of these lands inside a block rather than on a
    // boundary, which is the whole point of taking the launcher's own stamp.
    for (const auto quantized : {1.0, 1.5, 2.0, 3.25, 4.0}) {
        Rig rig;
        rig.capture().arm();
        rig.play();
        rig.launch(1, quantized);
        rig.roll(quantized + 2.0);
        rig.stopSlot(1, quantized + 2.0);
        rig.roll(1.0);

        const auto captured = rig.capture().collect();
        REQUIRE(captured.size() == 1);
        CHECK(captured.front().startBeat == Approx(quantized).margin(1.0 / kBeatSamples));
        CHECK(captured.front().lengthBeats == Approx(2.0).margin(2.0 / kBeatSamples));

        // The stamp is a sample, and it is the one the run began on.
        CHECK(captured.front().origin.sample ==
              static_cast<std::int64_t>(quantized * kBeatSamples));
    }
}

TEST_CASE("a scene captures as one event across tracks", "[engine][capture]") {
    Rig rig(4);
    rig.capture().arm();
    rig.play();
    rig.launchScene({1, 2, 3, 4}, 2.0);
    rig.roll(4.0);

    for (auto track = 1; track <= 4; ++track)
        rig.stopSlot(track, 6.0);

    rig.roll(3.0);

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 4);
    CHECK(rig.runs().overflows() == 0);

    for (const auto& run : captured) {
        CHECK(run.origin == captured.front().origin);
        CHECK(run.startBeat == Approx(captured.front().startBeat));
    }
}

TEST_CASE("a capture over a transport loop places each run once", "[engine][capture]") {
    Rig rig;
    rig.loop(0.0, 4.0);
    rig.capture().arm();
    rig.play();
    rig.launch(1, 2.0);

    // Three times round the loop, which the run plays straight through.
    rig.roll(12.0);
    rig.stopSlot(1, 14.0);
    rig.roll(3.0);

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 1);

    // Placed where it was launched, and as long as it actually sounded: the
    // timeline went back to zero three times in between, and neither answer
    // followed it.
    CHECK(captured.front().startBeat == Approx(2.0).margin(1.0 / kBeatSamples));
    CHECK(captured.front().lengthBeats == Approx(12.0).margin(2.0 / kBeatSamples));
}

TEST_CASE("a re-launch is a second span", "[engine][capture]") {
    Rig rig;
    rig.capture().arm();
    rig.play();
    rig.launch(1, 1.0);
    rig.roll(3.0);
    rig.launch(1, 3.0);
    rig.roll(3.0);
    rig.stopSlot(1, 6.0);
    rig.roll(1.0);

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 2);
    CHECK(captured[0].startBeat == Approx(1.0).margin(1.0 / kBeatSamples));
    CHECK(captured[0].lengthBeats == Approx(2.0).margin(2.0 / kBeatSamples));
    CHECK(captured[1].startBeat == Approx(3.0).margin(1.0 / kBeatSamples));
    CHECK(captured[1].lengthBeats == Approx(3.0).margin(2.0 / kBeatSamples));
}

TEST_CASE("a run a follow action ended is captured to there", "[engine][capture]") {
    // The third thing that ends a run, beside a stop and a re-launch (#2464).
    Rig rig(1, SlotFollow{.action = SlotAction::stop, .lengthBeats = 4.0});
    rig.capture().arm();
    rig.play();
    rig.launch(1, 2.0);
    rig.roll(10.0);

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 1);
    CHECK(captured.front().startBeat == Approx(2.0).margin(1.0 / kBeatSamples));
    CHECK(captured.front().lengthBeats == Approx(4.0).margin(2.0 / kBeatSamples));
}

TEST_CASE("arming after a launch keeps the beat the run began on", "[engine][capture]") {
    Rig rig;
    rig.play();
    rig.launch(1, 1.0);
    rig.roll(3.0);

    // The run has been sounding for two beats when Record is pressed.
    REQUIRE(rig.capture().sounding() == 1);
    rig.capture().arm();

    rig.stopSlot(1, 5.0);
    rig.roll(3.0);

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 1);
    CHECK(captured.front().startBeat == Approx(1.0).margin(1.0 / kBeatSamples));
    CHECK(captured.front().lengthBeats == Approx(4.0).margin(2.0 / kBeatSamples));
}

TEST_CASE("disarming takes the edges before it reads the boundary", "[engine][capture]") {
    // The run stopped on its own two beats before the button was pressed, and
    // its end was still in the lane. A boundary read first would have stretched
    // the span to here, which is the drift a poll has (#2464 review).
    Rig rig;
    rig.capture().arm();
    rig.play();
    rig.launch(1, 1.0);
    rig.roll(4.0);

    rig.stopSlot(1, 5.0);
    rig.roll(2.0, false);

    rig.capture().disarm();

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 1);
    CHECK(captured.front().lengthBeats == Approx(4.0).margin(1.0 / kBeatSamples));
    CHECK(rig.capture().sounding() == 0);
}

TEST_CASE("nothing is captured while disarmed", "[engine][capture]") {
    Rig rig;
    rig.play();
    rig.launch(1, 1.0);
    rig.roll(3.0);
    rig.stopSlot(1, 3.0);
    rig.roll(1.0);

    CHECK(rig.capture().collect().empty());
}

TEST_CASE("an end no block stamped closes the run it belongs to", "[engine][capture]") {
    // A slot deleted while it sounded: its handle goes before any block could
    // report the end, so the store says where the run had got to instead
    // (RuntimeStateStore::publishHandles).
    Rig rig;
    rig.capture().arm();
    rig.play();
    rig.launch(1, 1.0);
    rig.roll(4.0);

    REQUIRE(rig.capture().sounding() == 1);

    rig.capture().apply(SlotRunEvent{.key = SlotKey{1, 0},
                                     .kind = SlotRunEvent::Kind::ended,
                                     .incarnation = 1,
                                     .monotonicBeat = 4.0});

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 1);
    CHECK(captured.front().startBeat == Approx(1.0).margin(1.0 / kBeatSamples));
    CHECK(captured.front().lengthBeats == Approx(3.0).margin(1.0 / kBeatSamples));
    CHECK(rig.capture().sounding() == 0);
}

TEST_CASE("a run is held under the handle that played it", "[engine][capture]") {
    // The refill's launch reaches the capture before the retired run's end, and
    // the two are different runs of the same slot: keying by slot alone would
    // drop the first, which is what the fork's play-state poll cannot tell
    // apart either.
    Rig rig;
    rig.capture().arm();
    rig.play();
    rig.launch(1, 1.0);
    rig.roll(4.0);

    // Incarnation two, launched where the first was still in flight.
    rig.capture().apply(SlotRunEvent{.key = SlotKey{1, 0},
                                     .kind = SlotRunEvent::Kind::began,
                                     .incarnation = 2,
                                     .timelineBeat = 4.0,
                                     .monotonicBeat = 4.0});

    // And only now the end of the first, out of order.
    rig.capture().apply(SlotRunEvent{.key = SlotKey{1, 0},
                                     .kind = SlotRunEvent::Kind::ended,
                                     .incarnation = 1,
                                     .monotonicBeat = 4.0});

    rig.capture().apply(SlotRunEvent{.key = SlotKey{1, 0},
                                     .kind = SlotRunEvent::Kind::ended,
                                     .incarnation = 2,
                                     .monotonicBeat = 6.0});

    auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 2);
    std::ranges::sort(captured, {}, &CapturedRun::incarnation);

    // Both spans survive, and each says which material played it.
    CHECK(captured[0].incarnation == 1);
    CHECK(captured[0].lengthBeats == Approx(3.0).margin(1.0 / kBeatSamples));
    CHECK(captured[1].incarnation == 2);
    CHECK(captured[1].lengthBeats == Approx(2.0));
}

TEST_CASE("disarming ends what is still sounding", "[engine][capture]") {
    Rig rig;
    rig.capture().arm();
    rig.play();
    rig.launch(1, 1.0);
    rig.roll(4.0);

    rig.capture().disarm();

    const auto captured = rig.capture().collect();
    REQUIRE(captured.size() == 1);
    CHECK(captured.front().startBeat == Approx(1.0).margin(1.0 / kBeatSamples));
    CHECK(captured.front().lengthBeats == Approx(3.0).margin(1.0 / kBeatSamples));

    // The slot is still playing; what stopped is the capture.
    CHECK(rig.capture().sounding() == 1);

    rig.stopSlot(1, 6.0);
    rig.roll(3.0);
    CHECK(rig.capture().collect().empty());
}
