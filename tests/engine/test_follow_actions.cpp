#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <vector>

#include "launch/SessionLauncher.hpp"

/**
 * @file test_follow_actions.cpp
 * @brief What a slot does when its run ends, and who hears about it (#2304).
 */

using namespace magda;
using namespace magda::engine;
using Catch::Approx;

namespace {

constexpr TrackId kTrack = 1;
constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

/// A quarter beat a block, so a one-beat slot is four blocks.
constexpr double kBeatsPerBlock = 0.25;

/// The slot length every case here uses, in blocks and in beats.
constexpr int kBlocksPerPass = 4;
constexpr double kPassBeats = kBlocksPerPass * kBeatsPerBlock;

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

SlotFollow follows(SlotAction action, int loopCount = 1, double delayBeats = 0.0,
                   double lengthBeats = kPassBeats) {
    return SlotFollow{action, loopCount, delayBeats, lengthBeats};
}

/// A track's slots, published the way the store publishes them.
struct Rig {
    explicit Rig(std::vector<SlotFollow> slots)
        : follow(std::move(slots)), handles(follow.size()), taps(follow.size()) {
        std::vector<int> all;
        for (auto scene = 0; scene < scenes(); ++scene)
            all.push_back(scene);

        publish(all);
    }

    /// Name only @p scenes, which is what the store does after a slot is
    /// emptied: the handles of the rest stay where they are.
    void publish(const std::vector<int>& scenes) {
        auto table = std::make_shared<LaunchHandleTable>();
        std::map<SlotKey, std::uint64_t> incarnations;

        for (const auto scene : scenes) {
            const auto key = SlotKey{kTrack, scene};
            const auto at = static_cast<std::size_t>(scene);
            table->entries.push_back(LaunchHandleTable::Entry{.key = key,
                                                              .handle = &handles[at],
                                                              .incarnation = 1,
                                                              .tap = &taps[at],
                                                              .follow = follow[at]});
            incarnations[key] = 1;
        }

        feed.publish(std::move(table));
        requests.setIncarnations(std::move(incarnations));
    }

    void roll(int index) {
        advanceLaunchHandles(feed, requests, blockAt(index));
    }

    /// Roll blocks @p from up to but not including @p to.
    void rollThrough(int from, int to) {
        for (auto index = from; index < to; ++index)
            roll(index);
    }

    LaunchTap::Reading reading(int scene) const {
        return taps[static_cast<std::size_t>(scene)].read();
    }

    /// The one slot of the track that is sounding, or -1. What a track's active
    /// slot is, read off the taps rather than tracked alongside them.
    int sounding() const {
        for (auto scene = 0; scene < scenes(); ++scene)
            if (reading(scene).playing)
                return scene;

        return -1;
    }

    int scenes() const {
        return static_cast<int>(follow.size());
    }

    static SlotKey key(int scene) {
        return SlotKey{kTrack, scene};
    }

    std::vector<SlotFollow> follow;
    std::vector<LaunchHandle> handles;
    std::vector<LaunchTap> taps;
    LaunchHandleFeed feed;
    LaunchRequestQueue requests;
};

/// Launch @p scene at the first sample of the next block rolled.
void launch(Rig& rig, int scene) {
    LaunchRequestQueue::Gesture gesture(rig.requests);
    gesture.play(Rig::key(scene));
}

/// Re-trigger @p scene every pass, which is what a looping session clip does.
void loopIt(Rig& rig, int scene) {
    LaunchRequestQueue::Gesture gesture(rig.requests);
    gesture.setLooping(Rig::key(scene), kPassBeats);
}

}  // namespace

TEST_CASE("A slot that does not loop stops at the end of its material",
          "[engine][session][launch][follow]") {
    // With nothing to follow it. A run ends where its material does, which is
    // the same question a follow action is asked at.
    Rig rig({follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass);

    CHECK(rig.reading(0).playing);
    CHECK(rig.reading(0).elapsedBeats == Approx(kPassBeats));

    rig.roll(kBlocksPerPass);

    CHECK_FALSE(rig.reading(0).playing);

    // The session keeps the track: silence after a stop is not the arrangement
    // coming back (#2302).
    CHECK(rig.reading(0).holdsSection);
}

TEST_CASE("A looping slot with nothing to follow it plays on",
          "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::none)});

    loopIt(rig, 0);
    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass * 4);

    CHECK(rig.reading(0).playing);
}

TEST_CASE("A stop action ends the run on its own beat", "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::stop)});

    loopIt(rig, 0);
    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass);

    CHECK(rig.reading(0).playing);

    rig.roll(kBlocksPerPass);

    CHECK_FALSE(rig.reading(0).playing);
    CHECK_FALSE(rig.handles[0].playedRange().has_value());

    // The run it ended covered exactly one pass, so the stop landed on the beat
    // rather than on the callback boundary before or after it.
    REQUIRE(rig.handles[0].lastPlayedRange().has_value());
    CHECK(rig.handles[0].lastPlayedRange()->length() == Approx(kPassBeats));
}

TEST_CASE("Play next hands the track to the following slot on one beat",
          "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::next), follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass);

    CHECK(rig.sounding() == 0);

    rig.roll(kBlocksPerPass);

    CHECK(rig.sounding() == 1);

    // One instant, not two: the run that started begins where the one that
    // ended left off, so nothing of the track is played twice or missed.
    REQUIRE(rig.handles[0].lastPlayedRange().has_value());
    REQUIRE(rig.handles[1].playedMonotonicRange().has_value());
    CHECK(rig.handles[0].lastPlayedRange()->end == Approx(kPassBeats));
    CHECK(rig.handles[1].playedMonotonicRange()->start == Approx(kPassBeats));
}

TEST_CASE("Play next at the last slot stops and starts nothing",
          "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::none), follows(SlotAction::next)});

    launch(rig, 1);
    rig.rollThrough(0, kBlocksPerPass + 1);

    CHECK(rig.sounding() == -1);
}

TEST_CASE("Play previous hands the track back up the scenes", "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::none), follows(SlotAction::previous)});

    launch(rig, 1);
    rig.rollThrough(0, kBlocksPerPass + 1);

    CHECK(rig.sounding() == 0);
}

TEST_CASE("Play previous at the first slot stops and starts nothing",
          "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::previous), follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass + 1);

    CHECK(rig.sounding() == -1);
}

TEST_CASE("Play again restarts the run rather than ending it",
          "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::again)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass + 1);

    CHECK(rig.reading(0).playing);

    // Restarted, so the played range counts from the new run and not from the
    // launch a pass ago.
    REQUIRE(rig.handles[0].playedMonotonicRange().has_value());
    CHECK(rig.handles[0].playedMonotonicRange()->start == Approx(kPassBeats));
}

TEST_CASE("A loop count holds the action off until the pass it names",
          "[engine][session][launch][follow]") {
    // The case a played range cannot answer: every wrap restarts the run, so
    // what counts the passes is the beat the launch was asked for.
    Rig rig({follows(SlotAction::next, 3), follows(SlotAction::none)});

    loopIt(rig, 0);
    launch(rig, 0);

    rig.rollThrough(0, kBlocksPerPass * 3);

    CHECK(rig.sounding() == 0);

    rig.roll(kBlocksPerPass * 3);

    CHECK(rig.sounding() == 1);
}

TEST_CASE("A loop count is one pass for a slot that does not loop",
          "[engine][session][launch][follow]") {
    // A slot with one pass in it has one to count, whatever the clip asks for.
    Rig rig({follows(SlotAction::next, 3), follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass + 1);

    CHECK(rig.sounding() == 1);
}

TEST_CASE("A follow delay is beats on top of the passes", "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::next, 1, kPassBeats), follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass * 2);

    CHECK(rig.sounding() == 0);

    rig.roll(kBlocksPerPass * 2);

    CHECK(rig.sounding() == 1);
}

TEST_CASE("A request the user made outranks the follow action",
          "[engine][session][launch][follow]") {
    // A handle holds one pending request, so a follow action written over a
    // queued launch would drop what was asked for.
    Rig rig({follows(SlotAction::next), follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass - 1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0), kPassBeats * 2);
    }

    rig.rollThrough(kBlocksPerPass - 1, (kBlocksPerPass * 2) + 1);

    // Slot 1 was never started, and slot 0 relaunches when the user said.
    CHECK(rig.sounding() == 0);
    REQUIRE(rig.handles[0].playedMonotonicRange().has_value());
    CHECK(rig.handles[0].playedMonotonicRange()->start == Approx(kPassBeats * 2));
}

TEST_CASE("A follow action names the slots there are now", "[engine][session][launch][follow]") {
    // Nothing stores a target, so a slot emptied since the project loaded is
    // simply not in the list the action searches.
    Rig rig({follows(SlotAction::next), follows(SlotAction::none), follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass - 1);

    rig.publish({0, 2});
    rig.roll(kBlocksPerPass - 1);
    rig.roll(kBlocksPerPass);

    CHECK(rig.sounding() == 2);
}

TEST_CASE("A random action picks the same slot for the same beat",
          "[engine][session][launch][follow]") {
    // Deterministic on purpose: two renders of one project have to produce one
    // file (#1896).
    const auto pick = [](int launchBlock) {
        Rig rig({follows(SlotAction::random), follows(SlotAction::none), follows(SlotAction::none),
                 follows(SlotAction::none)});

        rig.rollThrough(0, launchBlock);
        launch(rig, 0);
        rig.rollThrough(launchBlock, launchBlock + kBlocksPerPass + 1);
        return rig.sounding();
    };

    CHECK(pick(0) == pick(0));
    CHECK(pick(0) >= 0);

    // A different beat is a different draw, or a random action would pick one
    // slot for the life of the set.
    auto differs = false;
    for (auto block = 1; block <= 8 && !differs; ++block)
        differs = pick(block) != pick(0);

    CHECK(differs);
}

TEST_CASE("An engine-initiated launch reads out exactly as a clicked one does",
          "[engine][session][launch][follow][tap]") {
    // The whole of the read-back: what a follow action started is published by
    // the slot that is playing it, so there is nothing for the model to adopt.
    Rig followed({follows(SlotAction::next), follows(SlotAction::none)});
    Rig clicked({follows(SlotAction::none), follows(SlotAction::none)});

    launch(followed, 0);
    launch(clicked, 0);

    followed.rollThrough(0, kBlocksPerPass);
    clicked.rollThrough(0, kBlocksPerPass);

    // The user clicks slot 1 on the beat the follow action would have.
    {
        LaunchRequestQueue::Gesture gesture(clicked.requests);
        gesture.stop(Rig::key(0), kPassBeats);
    }
    {
        LaunchRequestQueue::Gesture gesture(clicked.requests);
        gesture.play(Rig::key(1), kPassBeats);
    }

    followed.rollThrough(kBlocksPerPass, kBlocksPerPass * 2);
    clicked.rollThrough(kBlocksPerPass, kBlocksPerPass * 2);

    for (auto scene = 0; scene < 2; ++scene) {
        const auto a = followed.reading(scene);
        const auto b = clicked.reading(scene);

        CHECK(a.playing == b.playing);
        CHECK(a.queued == b.queued);
        CHECK(a.holdsSection == b.holdsSection);
        CHECK(a.elapsedBeats == Approx(b.elapsedBeats));
    }
}

TEST_CASE("A chain of follow actions plays the scenes in order",
          "[engine][session][launch][follow]") {
    // The issue's own verification: a known sequence of played ranges, with the
    // taps agreeing at every step rather than only at the end.
    Rig rig({follows(SlotAction::next), follows(SlotAction::next), follows(SlotAction::next)});

    launch(rig, 0);

    for (auto pass = 0; pass < 3; ++pass) {
        rig.rollThrough(pass * kBlocksPerPass, (pass + 1) * kBlocksPerPass);

        CHECK(rig.sounding() == pass);
        CHECK(rig.reading(pass).elapsedBeats == Approx(kPassBeats));
    }

    // The last slot has nothing to hand on to, so the chain ends there.
    rig.roll(kBlocksPerPass * 3);

    CHECK(rig.sounding() == -1);

    for (auto scene = 0; scene < 3; ++scene) {
        REQUIRE(rig.handles[scene].lastPlayedRange().has_value());
        CHECK(rig.handles[scene].lastPlayedRange()->start == Approx(scene * kPassBeats));
        CHECK(rig.handles[scene].lastPlayedRange()->length() == Approx(kPassBeats));
    }
}

TEST_CASE("A follow action leaves a target the user has already asked for",
          "[engine][session][launch][follow]") {
    // The rule the source already obeys, applied to the slot it hands over to:
    // a handle holds one pending request, so launching over a queued one would
    // drop what the user asked for (#2304 review).
    Rig rig({follows(SlotAction::next), follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass - 1);

    // Slot 1 is spoken for, two passes out, before the hand-over is due.
    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(1), kPassBeats * 2);
    }

    rig.rollThrough(kBlocksPerPass - 1, kBlocksPerPass * 2);

    // The source still stopped on its own beat; slot 1 waited for the beat the
    // user named rather than starting a pass early.
    CHECK(rig.sounding() == -1);

    rig.roll(kBlocksPerPass * 2);

    CHECK(rig.sounding() == 1);
    REQUIRE(rig.handles[1].playedMonotonicRange().has_value());
    CHECK(rig.handles[1].playedMonotonicRange()->start == Approx(kPassBeats * 2));
}

TEST_CASE("Play again is not turned away by its own stop", "[engine][session][launch][follow]") {
    // The source is its own target, and the stop this pass writes must not read
    // as somebody else having asked for the slot.
    Rig rig({follows(SlotAction::again)});

    launch(rig, 0);
    rig.rollThrough(0, (kBlocksPerPass * 2) + 1);

    CHECK(rig.reading(0).playing);
    REQUIRE(rig.handles[0].playedMonotonicRange().has_value());
    CHECK(rig.handles[0].playedMonotonicRange()->start == Approx(kPassBeats * 2));
}

TEST_CASE("A run shorter than a callback counts the block it slipped",
          "[engine][session][launch][follow]") {
    // Its launch is the block's one event, so its end has nowhere to go. The
    // handle clamps it to the next block and the slip is counted rather than
    // left to look like an audio device fault (#2304 review).
    const auto shorterThanABlock = kBeatsPerBlock / 2.0;
    Rig rig({follows(SlotAction::none, 1, 0.0, shorterThanABlock)});

    launch(rig, 0);
    rig.roll(0);

    CHECK(rig.reading(0).playing);
    CHECK(rig.handles[0].lateRunEnds() == 0);

    rig.roll(1);

    CHECK_FALSE(rig.reading(0).playing);
    CHECK(rig.handles[0].lateRunEnds() == 1);
}

TEST_CASE("A run that ends on its own beat is not counted late",
          "[engine][session][launch][follow]") {
    Rig rig({follows(SlotAction::none)});

    launch(rig, 0);
    rig.rollThrough(0, kBlocksPerPass + 1);

    CHECK_FALSE(rig.reading(0).playing);
    CHECK(rig.handles[0].lateRunEnds() == 0);
}
