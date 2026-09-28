#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <vector>

#include "launch/SessionLauncher.hpp"

/**
 * @file test_launch_tap.cpp
 * @brief What a slot publishes about itself, and when (#2303).
 */

using namespace magda;
using namespace magda::engine;

namespace {

constexpr TrackId kTrack = 1;
constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

/// A quarter beat a block, so elapsed beats are countable by eye.
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

/// Handles and their taps, published the way the store publishes them.
struct Rig {
    explicit Rig(int scenes)
        : handles(static_cast<std::size_t>(scenes)), taps(static_cast<std::size_t>(scenes)) {
        auto table = std::make_shared<LaunchHandleTable>();
        std::map<SlotKey, std::uint64_t> incarnations;

        for (auto scene = 0; scene < scenes; ++scene) {
            const auto key = SlotKey{kTrack, scene};
            const auto at = static_cast<std::size_t>(scene);
            table->entries.push_back(LaunchHandleTable::Entry{
                .key = key, .handle = &handles[at], .incarnation = 1, .tap = &taps[at]});
            incarnations[key] = 1;
        }

        feed.publish(std::move(table));
        requests.setIncarnations(std::move(incarnations));
    }

    void roll(int index) {
        advanceLaunchHandles(feed, requests, blockAt(index));
    }

    LaunchTap::Reading reading(int scene = 0) const {
        return taps[static_cast<std::size_t>(scene)].read();
    }

    static SlotKey key(int scene) {
        return SlotKey{kTrack, scene};
    }

    std::vector<LaunchHandle> handles;
    std::vector<LaunchTap> taps;
    LaunchHandleFeed feed;
    LaunchRequestQueue requests;
};

}  // namespace

TEST_CASE("A slot nobody has rendered reads as stopped", "[engine][session][launch][tap]") {
    // Why the tap needs no write count.
    const LaunchTap tap;
    const auto reading = tap.read();

    CHECK_FALSE(reading.playing);
    CHECK(reading.queued == LaunchTap::Queued::nothing);
    CHECK_FALSE(reading.holdsSection);
    CHECK(reading.elapsedBeats == 0.0f);
}

TEST_CASE("A launch is published by the block that made it", "[engine][session][launch][tap]") {
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    CHECK_FALSE(rig.reading().playing);

    rig.roll(0);
    CHECK(rig.reading().playing);
}

TEST_CASE("A queued launch is visible before it sounds", "[engine][session][launch][tap]") {
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0), 0.75);
    }

    rig.roll(0);

    const auto queued = rig.reading();
    CHECK_FALSE(queued.playing);
    CHECK(queued.queued == LaunchTap::Queued::play);

    rig.roll(1);
    rig.roll(2);
    CHECK_FALSE(rig.reading().playing);

    // Beat 0.75 is the first sample of block 3, so that is the block it sounds
    // in and not one earlier.
    rig.roll(3);

    const auto started = rig.reading();
    CHECK(started.playing);
    CHECK(started.queued == LaunchTap::Queued::nothing);
}

TEST_CASE("A pending stop is visible while the slot still sounds",
          "[engine][session][launch][tap]") {
    // What isSessionTrackStopPending answers by polling, asked of the slot.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }
    rig.roll(0);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.stop(Rig::key(0), 1.75);
    }
    rig.roll(1);

    const auto pending = rig.reading();
    CHECK(pending.playing);
    CHECK(pending.queued == LaunchTap::Queued::stop);
}

TEST_CASE("The published position is what the run has covered", "[engine][session][launch][tap]") {
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(0);
    CHECK(rig.reading().elapsedBeats == Catch::Approx(kBeatsPerBlock).margin(1e-5));

    rig.roll(1);
    rig.roll(2);

    // Three blocks of a quarter beat, unlooped.
    CHECK(rig.reading().elapsedBeats == Catch::Approx(3.0 * kBeatsPerBlock).margin(1e-5));
}

TEST_CASE("A run days long still moves the playhead", "[engine][session][launch][tap]") {
    // A run accumulates for as long as the clip plays, and a float's step
    // reaches 16 ms after three days at 120 bpm (#2303 review). The published
    // position has to resolve a block at any age the run reaches.
    LaunchTap tap;

    // Beats covered by thirty-three days at 120 bpm.
    constexpr double kDays = 33.0 * 24.0 * 60.0 * 120.0;
    constexpr double kStep = 1.0 / 4096.0;

    LaunchTap::Reading written;
    written.playing = true;
    written.elapsedBeats = kDays;
    tap.write(written);
    const auto first = tap.read();

    written.elapsedBeats = kDays + kBeatsPerBlock;
    tap.write(written);
    const auto second = tap.read();

    CHECK(first.elapsedBeats == Catch::Approx(kDays).margin(kStep));
    CHECK(second.elapsedBeats - first.elapsedBeats == Catch::Approx(kBeatsPerBlock).margin(kStep));
}

TEST_CASE("A stop leaves the track held, and says so", "[engine][session][launch][tap]") {
    // #2302: silence after a stop is the session still holding the track.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }
    rig.roll(0);
    REQUIRE(rig.reading().holdsSection);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.stop(Rig::key(0));
    }
    rig.roll(1);

    CHECK_FALSE(rig.reading().playing);
    CHECK(rig.reading().holdsSection);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.backToArrangement(Rig::key(0));
    }
    rig.roll(2);

    CHECK_FALSE(rig.reading().holdsSection);
}

TEST_CASE("A reading is one block's own", "[engine][session][launch][tap]") {
    // Why position and state are one word.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(0);

    const auto reading = rig.reading();
    REQUIRE(reading.playing);
    CHECK(reading.elapsedBeats > 0.0f);
}

TEST_CASE("Every slot of a scene publishes the same position", "[engine][session][launch][tap]") {
    Rig rig(3);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
        gesture.playSynced(Rig::key(1), Rig::key(0));
        gesture.playSynced(Rig::key(2), Rig::key(0));
    }

    rig.roll(0);
    rig.roll(1);

    const auto leader = rig.reading(0);
    REQUIRE(leader.playing);

    for (auto scene = 1; scene < 3; ++scene) {
        const auto joined = rig.reading(scene);
        CHECK(joined.playing);
        CHECK(joined.elapsedBeats == Catch::Approx(leader.elapsedBeats).margin(1e-6));
    }
}

TEST_CASE("A slot goes on publishing while the transport is stopped",
          "[engine][session][launch][tap]") {
    // A stopped engine renders no blocks, so the tap holds where it was left.
    Rig rig(1);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(Rig::key(0));
    }

    rig.roll(0);
    const auto held = rig.reading();

    CHECK(rig.reading().playing == held.playing);
    CHECK(rig.reading().elapsedBeats == held.elapsedBeats);
}
