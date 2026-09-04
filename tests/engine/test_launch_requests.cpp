#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "launch/SessionLauncher.hpp"

/**
 * @file test_launch_requests.cpp
 * @brief What a launch asked for off the audio thread does when it gets there
 *        (#2305).
 *
 * The lane, not the state machine: that a request lands in the block it was
 * asked in, keeps its order, cannot be applied twice, and cannot reach a slot
 * that has gone. LaunchHandle's own behaviour is test_launch_handle.cpp's.
 */

using namespace magda;
using namespace magda::engine;

namespace {

constexpr TrackId kTrack = 1;
constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

/// A quarter beat a block, so a launch and its first block are easy to tell
/// apart from the one after it.
constexpr double kBeatsPerBlock = 0.25;

BlockInfo blockAt(int index) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.sampleRate = kSampleRate;
    block.monotonicSamples = {SamplePosition{index * kBlockSize},
                              SamplePosition{(index + 1) * kBlockSize}};
    block.playing = true;
    block.continuous = index != 0;
    block.beats = {index * kBeatsPerBlock, (index + 1) * kBeatsPerBlock};
    block.monotonicBeats = block.beats;
    block.seconds = {static_cast<double>(index * kBlockSize) / kSampleRate,
                     static_cast<double>((index + 1) * kBlockSize) / kSampleRate};
    block.monotonicSeconds = block.seconds;
    return block;
}

/// A published table over handles the case owns, which is what the store makes
/// for real. Handles are held by the caller so a case can read one directly.
struct Rig {
    /// @p scenes on kTrack, one handle each.
    explicit Rig(int scenes) : handles(static_cast<std::size_t>(scenes)) {
        auto table = std::make_shared<LaunchHandleTable>();
        for (auto scene = 0; scene < scenes; ++scene)
            table->entries.push_back(LaunchHandleTable::Entry{
                SlotKey{kTrack, scene}, &handles[static_cast<std::size_t>(scene)]});

        feed.publish(std::move(table));
    }

    /// Nothing published at all, which is a session whose slots have no handles.
    Rig() = default;

    void roll(int index) {
        advanceLaunchHandles(feed, requests, blockAt(index));
    }

    LaunchHandle& slot(int scene) {
        return handles[static_cast<std::size_t>(scene)];
    }

    static SlotKey key(int scene) {
        return SlotKey{kTrack, scene};
    }

    std::vector<LaunchHandle> handles;
    LaunchHandleFeed feed;
    LaunchRequestQueue requests;
};

bool playing(const LaunchHandle& handle) {
    return handle.playState() == LaunchHandle::PlayState::playing;
}

}  // namespace

TEST_CASE("A request reaches its handle on the next block and no earlier",
          "[engine][session][launch]") {
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));

        // The gesture is still open, so nothing has been committed and a block
        // rendering now would see nothing.
        CHECK_FALSE(playing(rig.slot(0)));
    }

    // Still nothing: a committed request is applied by the pass that advances,
    // not by the commit.
    CHECK_FALSE(playing(rig.slot(0)));

    rig.roll(0);
    CHECK(playing(rig.slot(0)));
}

TEST_CASE("A request is applied once and not again", "[engine][session][launch]") {
    // The whole reason the lane is a queue rather than a published table: a
    // request left standing would retrigger the clip every block.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(0);
    const auto first = rig.slot(0).playedSampleRange();
    REQUIRE(first.has_value());

    rig.roll(1);
    rig.roll(2);

    const auto after = rig.slot(0).playedSampleRange();
    REQUIRE(after.has_value());

    // One run, three blocks long. Re-applied, the origin would have moved to
    // each later block and the run would be one block long for ever.
    CHECK(after->start == first->start);
    CHECK(after->end > first->end);
}

TEST_CASE("Requests keep the order they were asked in", "[engine][session][launch]") {
    // Why the loop length travels on this lane rather than beside it: a length
    // set for a clip about to start has to arrive before the launch.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.setLooping(Rig::key(0), 0.5);
        gesture.play(Rig::key(0));
    }

    // Two blocks a quarter beat each, so the half-beat loop re-triggers at the
    // end of the second one and the run begins again.
    rig.roll(0);
    rig.roll(1);
    rig.roll(2);

    const auto played = rig.slot(0).playedSampleRange();
    REQUIRE(played.has_value());

    // Re-anchored by the wrap rather than still counting from the launch.
    CHECK(played->start > SamplePosition{0});
}

TEST_CASE("A scene launch arrives as one thing", "[engine][session][launch]") {
    Rig rig(3);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
        gesture.playSynced(Rig::key(1), Rig::key(0));
        gesture.playSynced(Rig::key(2), Rig::key(0));
    }

    rig.roll(0);

    for (auto scene = 0; scene < 3; ++scene)
        CHECK(playing(rig.slot(scene)));

    // In phase, not merely started together: a synced launch joins the leader's
    // run, so all three report the same origin.
    const auto leader = rig.slot(0).playedSampleRange();
    REQUIRE(leader.has_value());

    for (auto scene = 1; scene < 3; ++scene) {
        const auto joined = rig.slot(scene).playedSampleRange();
        REQUIRE(joined.has_value());
        CHECK(joined->start == leader->start);
        CHECK(joined->end == leader->end);
    }
}

TEST_CASE("A synced launch whose leader has gone starts on its own", "[engine][session][launch]") {
    // The leader's slot was emptied between the ask and the block. Playing
    // alone is the honest answer: the clip was asked to play and there is
    // nothing left to be in phase with.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.playSynced(Rig::key(0), SlotKey{kTrack, 7});
    }

    rig.roll(0);
    CHECK(playing(rig.slot(0)));
}

TEST_CASE("A request for a slot that is not there does nothing", "[engine][session][launch]") {
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(SlotKey{kTrack, 4});
        gesture.play(SlotKey{kTrack + 1, 0});
    }

    rig.roll(0);
    CHECK_FALSE(playing(rig.slot(0)));
}

TEST_CASE("A slot emptied and refilled does not inherit what was asked of the last one",
          "[engine][session][launch]") {
    // The property that made this a queue and not a table of intents. A request
    // is consumed by the block it reaches, so a handle made afterwards has
    // nothing standing against its slot.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(0);
    REQUIRE(playing(rig.slot(0)));

    // What publishHandles() does to a slot whose clip was deleted and replaced.
    LaunchHandle refilled;
    auto table = std::make_shared<LaunchHandleTable>();
    table->entries.push_back(LaunchHandleTable::Entry{Rig::key(0), &refilled});
    rig.feed.publish(std::move(table));

    rig.roll(1);
    CHECK_FALSE(playing(refilled));
}

TEST_CASE("Back to arrangement travels the same lane", "[engine][session][launch]") {
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(0);
    REQUIRE(rig.slot(0).holdsSection());

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.backToArrangement(Rig::key(0));
    }

    rig.roll(1);
    CHECK_FALSE(playing(rig.slot(0)));
    CHECK_FALSE(rig.slot(0).holdsSection());
    CHECK(rig.slot(0).blockStatus().releasedSection);
}

TEST_CASE("A stop leaves the track held until it is given back", "[engine][session][launch]") {
    // The difference #2302 settled, over the lane: stopping is not handing back.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }
    rig.roll(0);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.stop(Rig::key(0));
    }
    rig.roll(1);

    CHECK_FALSE(playing(rig.slot(0)));
    CHECK(rig.slot(0).holdsSection());
}

TEST_CASE("A request made while nothing is published is not kept", "[engine][session][launch]") {
    // A queue filling while no session exists would deliver a launch made
    // minutes ago at whatever moment one appeared.
    Rig rig;

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(0);

    LaunchHandle handle;
    auto table = std::make_shared<LaunchHandleTable>();
    table->entries.push_back(LaunchHandleTable::Entry{Rig::key(0), &handle});
    rig.feed.publish(std::move(table));

    rig.roll(1);
    CHECK_FALSE(playing(handle));
}

TEST_CASE("A gesture too large for the ring is dropped whole", "[engine][session][launch]") {
    // Half a scene launching is worse than none: the tracks that made it would
    // be a bar ahead of the ones that did not for as long as they played.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        for (auto i = 0; i <= LaunchRequestQueue::kCapacity; ++i)
            gesture.play(Rig::key(0));
    }

    CHECK(rig.requests.overflows() == 1);

    rig.roll(0);
    CHECK_FALSE(playing(rig.slot(0)));

    // And the lane still works afterwards.
    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(1);
    CHECK(playing(rig.slot(0)));
    CHECK(rig.requests.overflows() == 1);
}

TEST_CASE("A gesture that exactly fills the ring is kept", "[engine][session][launch]") {
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        for (auto i = 0; i < LaunchRequestQueue::kCapacity; ++i)
            gesture.play(Rig::key(0));
    }

    CHECK(rig.requests.overflows() == 0);

    rig.roll(0);
    CHECK(playing(rig.slot(0)));
}

TEST_CASE("The ring is reusable once its requests have been read", "[engine][session][launch]") {
    // The cursors are counts rather than indices, so the capacity is what is
    // outstanding and not what has ever been asked.
    Rig rig(1);

    for (auto round = 0; round < 4; ++round) {
        {
            LaunchRequestQueue::Gesture gesture(rig.requests);
            for (auto i = 0; i < LaunchRequestQueue::kCapacity / 2; ++i)
                gesture.play(Rig::key(0));
        }

        rig.roll(round);
    }

    CHECK(rig.requests.overflows() == 0);
    CHECK(playing(rig.slot(0)));
}
