#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "launch/SessionLauncher.hpp"

/**
 * @file test_launch_behaviour.cpp
 * @brief A scene across tracks, and the events that meet on one handle (#2306).
 *
 * What the launcher does when more than one track is involved. test_launch_-
 * handle.cpp is one handle's own state machine and test_launch_requests.cpp is
 * the lane a request travels down; both build their slots on a single track,
 * which is the one thing a scene is not.
 *
 * Assertions about which slot is playing on which beat rather than about audio.
 * A scene that started seven of its eight tracks on one beat and the eighth on
 * the next sounds like a scene, which is why it needs pinning here.
 */

using Catch::Approx;
using namespace magda;
using namespace magda::engine;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

/// A quarter beat a block, so a launch and its first block are easy to tell
/// apart from the one after it. Carried per sample too, since the sizes at the
/// bottom of this file cut the same beats differently.
constexpr double kBeatsPerBlock = 0.25;
constexpr double kBeatsPerSample = kBeatsPerBlock / kBlockSize;

BlockInfo blockOf(std::int64_t startSample, int numSamples) {
    const auto endSample = startSample + numSamples;

    BlockInfo block;
    block.numSamples = numSamples;
    block.sampleRate = kSampleRate;
    block.monotonicSamples = {SamplePosition{startSample}, SamplePosition{endSample}};
    block.playing = true;
    block.continuous = startSample != 0;
    block.beats = {static_cast<double>(startSample) * kBeatsPerSample,
                   static_cast<double>(endSample) * kBeatsPerSample};
    block.monotonicBeats = block.beats;
    block.seconds = {static_cast<double>(startSample) / kSampleRate,
                     static_cast<double>(endSample) / kSampleRate};
    block.monotonicSeconds = block.seconds;
    return block;
}

BlockInfo blockAt(int index) {
    return blockOf(static_cast<std::int64_t>(index) * kBlockSize, kBlockSize);
}

/// The block covering @p beat, so a case names the beat it is rolling to rather
/// than counting callbacks.
int blockHolding(double beat) {
    return static_cast<int>(beat / kBeatsPerBlock);
}

/**
 * @brief A published table over @p tracks tracks of @p scenes slots each.
 *
 * Sorted by slot key, which for more than one track means every scene of track
 * one before any of track two: what `rangeFor` binary-searches and what a scene
 * launch has to reach across.
 */
struct Rig {
    Rig(int tracks, int scenes) : trackCount(tracks), sceneCount(scenes) {
        handles.resize(static_cast<std::size_t>(tracks) * static_cast<std::size_t>(scenes));

        auto table = std::make_shared<LaunchHandleTable>();
        for (auto track = 0; track < tracks; ++track)
            for (auto scene = 0; scene < scenes; ++scene)
                table->entries.push_back(LaunchHandleTable::Entry{.key = key(track, scene),
                                                                  .handle = &at(track, scene)});

        feed.publish(std::move(table));
    }

    static SlotKey key(int track, int scene) {
        return SlotKey{static_cast<TrackId>(track + 1), scene};
    }

    LaunchHandle& at(int track, int scene) {
        return handles[static_cast<std::size_t>(track) * static_cast<std::size_t>(sceneCount) +
                       static_cast<std::size_t>(scene)];
    }

    bool playing(int track, int scene) {
        return at(track, scene).playState() == LaunchHandle::PlayState::playing;
    }

    void roll(int index) {
        advanceLaunchHandles(feed, requests, blockAt(index));
    }

    /// Every block from @p first up to and including the one holding @p beat.
    void rollTo(double beat, int first = 0) {
        for (auto index = first; index <= blockHolding(beat); ++index)
            roll(index);
    }

    int trackCount = 0;
    int sceneCount = 0;
    std::vector<LaunchHandle> handles;
    LaunchHandleFeed feed;
    LaunchRequestQueue requests;
};

/// The monotonic sample a run began on, which is what "the same beat" means
/// once a scene is more than one track.
std::int64_t startedAt(const LaunchHandle& handle) {
    REQUIRE(handle.playedSampleRange().has_value());
    return handle.playedSampleRange()->start.sample;
}

}  // namespace

TEST_CASE("A scene starts every track it names on one beat", "[engine][session][launch][scene]") {
    Rig rig(4, 2);

    // The leader and its followers in one gesture, which is what makes a scene
    // one event rather than four launches that happen to be adjacent.
    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        std::vector<SlotKey> followers;
        followers.reserve(4);
        for (auto track = 0; track < 4; ++track)
            followers.push_back(rig.key(track, 1));

        gesture.playScene(rig.key(0, 1), followers, 1.0);
    }

    rig.rollTo(1.0);

    const auto leader = startedAt(rig.at(0, 1));
    for (auto track = 0; track < 4; ++track) {
        INFO("track " << track);
        REQUIRE(rig.playing(track, 1));
        CHECK(startedAt(rig.at(track, 1)) == leader);
    }

    // The beat asked for, not the boundary of the block it landed in.
    CHECK(leader == static_cast<std::int64_t>(1.0 / kBeatsPerSample));
}

TEST_CASE("A scene joins a track that was already playing to the others",
          "[engine][session][launch][scene]") {
    Rig rig(3, 2);

    // Track 0 is already running scene 1, out of phase with the scene about to
    // be launched. A synced launch joins the leader's run rather than starting
    // its own, so the three report one origin afterwards.
    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(rig.key(0, 1), 0.5);
    }
    rig.rollTo(0.5);
    REQUIRE(rig.playing(0, 1));

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        std::vector<SlotKey> followers{rig.key(0, 1), rig.key(1, 1), rig.key(2, 1)};
        gesture.playScene(rig.key(1, 1), followers, 2.0);
    }
    rig.rollTo(2.0, blockHolding(0.5) + 1);

    const auto leader = startedAt(rig.at(1, 1));
    for (auto track = 0; track < 3; ++track) {
        INFO("track " << track);
        REQUIRE(rig.playing(track, 1));
        CHECK(startedAt(rig.at(track, 1)) == leader);
    }
}

TEST_CASE("A scene stops the track it has no clip for, on the scene's own beat",
          "[engine][session][launch][scene]") {
    Rig rig(3, 2);

    // Track 2 is playing something else. The scene about to be launched holds
    // no clip for it, which the surface above turns into a stop travelling in
    // the same gesture as the launches (SessionLaunchService).
    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(rig.key(2, 0), 0.5);
    }
    rig.rollTo(0.5);
    REQUIRE(rig.playing(2, 0));

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        std::vector<SlotKey> followers{rig.key(0, 1), rig.key(1, 1)};
        gesture.playScene(rig.key(0, 1), followers, 2.0);
        gesture.stop(rig.key(2, 0), 2.0);
    }
    rig.rollTo(2.0, blockHolding(0.5) + 1);

    CHECK(rig.playing(0, 1));
    CHECK(rig.playing(1, 1));

    // Stopped on the beat the others started on, rather than a block either
    // side of it: the whole gesture is drained before any handle advances.
    CHECK_FALSE(rig.playing(2, 0));
    REQUIRE(rig.at(2, 0).lastPlayedRange().has_value());
    CHECK(rig.at(2, 0).lastPlayedRange()->end == Approx(2.0));
    CHECK(startedAt(rig.at(0, 1)) == static_cast<std::int64_t>(2.0 / kBeatsPerSample));
}

TEST_CASE("A stop replaces a launch that has not fired", "[engine][session][launch]") {
    Rig rig(1, 1);

    // A handle holds one pending request, so a stop arriving over a queued
    // launch is what was asked for last and the slot never starts.
    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(rig.key(0, 0), 2.0);
    }
    rig.roll(0);
    REQUIRE(rig.at(0, 0).queuedState() == LaunchHandle::QueueState::playQueued);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.stop(rig.key(0, 0), 1.0);
    }
    rig.rollTo(3.0, 1);

    // Never started, rather than started and then stopped. A stop clears the
    // run it ended, so the run it did not have is the last run there has been:
    // both of the checks above pass on a slot that played beat one to beat two.
    CHECK_FALSE(rig.playing(0, 0));
    CHECK_FALSE(rig.at(0, 0).playedSampleRange().has_value());
    CHECK_FALSE(rig.at(0, 0).lastPlayedRange().has_value());
}

TEST_CASE("A stop later than the launch it replaces still cancels it",
          "[engine][session][launch]") {
    Rig rig(1, 1);

    // The stop is quantized past the launch's own beat. It is still the pending
    // request, so the beat the launch was waiting for passes with nothing on it
    // rather than starting a run the stop then ends.
    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.play(rig.key(0, 0), 1.0);
    }
    rig.roll(0);
    REQUIRE(rig.at(0, 0).queuedState() == LaunchHandle::QueueState::playQueued);

    {
        LaunchRequestQueue::Gesture gesture(rig.requests);
        gesture.stop(rig.key(0, 0), 2.0);
    }
    rig.rollTo(3.0, 1);

    CHECK_FALSE(rig.playing(0, 0));
    CHECK_FALSE(rig.at(0, 0).playedSampleRange().has_value());
    CHECK_FALSE(rig.at(0, 0).lastPlayedRange().has_value());
}

TEST_CASE("A scene lands on the same sample at every block size",
          "[engine][session][launch][scene]") {
    // The sizes the invariance gate renders at (#2078), and one that divides no
    // beat in this rig evenly.
    for (const auto blockSize : {64, 96, 512, 4096}) {
        INFO("block size " << blockSize);

        Rig rig(4, 1);
        {
            LaunchRequestQueue::Gesture gesture(rig.requests);
            std::vector<SlotKey> followers;
            followers.reserve(4);
            for (auto track = 0; track < 4; ++track)
                followers.push_back(rig.key(track, 0));

            gesture.playScene(rig.key(0, 0), followers, 1.0);
        }

        const auto endSample = static_cast<std::int64_t>(2.0 / kBeatsPerSample);
        for (std::int64_t at = 0; at < endSample; at += blockSize)
            advanceLaunchHandles(rig.feed, rig.requests, blockOf(at, blockSize));

        // The beat, not the callback boundary it fell inside: a scene quantized
        // to beat one starts at sample 2048 whatever the host asked for.
        for (auto track = 0; track < 4; ++track) {
            INFO("track " << track);
            REQUIRE(rig.playing(track, 0));
            CHECK(startedAt(rig.at(track, 0)) == static_cast<std::int64_t>(1.0 / kBeatsPerSample));
        }
    }
}
