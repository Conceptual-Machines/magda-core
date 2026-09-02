#include <catch2/catch_test_macros.hpp>

#include "clip/ClipSnapshot.hpp"
#include "core/TrackInfo.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * @file test_launch_handle_lifetime.cpp
 * @brief Which slot handles survive a publish, and which do not (#2301).
 *
 * A handle is per slot and a slot is a clip, so what says a slot exists is the
 * snapshot rather than a walk over tracks, and a clip edit deliberately does
 * not compile a plan. Their whole lifetime is therefore publishHandles(): made,
 * kept and retired there and nowhere else, and these are the cases that say a
 * slot emptied and refilled comes back new rather than carrying the run it had.
 */

using namespace magda;
using namespace magda::engine;

namespace {

constexpr TrackId kTrack = 1;

/// A factory that builds nothing: these cases are about lifetime rather than
/// about what a handle is attached to.
class NoFactory final : public RuntimeStateFactory {};

/// A snapshot naming @p scenes on kTrack, with no material in them: what is
/// asserted here is which slots exist, not what they play.
ClipSnapshot snapshotWithScenes(const std::vector<int>& scenes, TrackId trackId = kTrack) {
    ClipSnapshot snapshot;
    TrackClipPlayback track;
    track.trackId = trackId;
    for (const auto scene : scenes)
        track.session.push_back(SessionSlotPlayback{scene, {}, {}, 4.0});
    snapshot.tracks.push_back(std::move(track));
    return snapshot;
}

/// A handle three bars into a run, so a handle that was reused rather than
/// remade is visible rather than merely equal.
void launch(LaunchHandle& handle) {
    handle.play({});
    handle.advance(SyncRange{BeatRange{0.0, 2.0}, BeatRange{0.0, 2.0}, SecondsRange{0.0, 1.0},
                             SecondsRange{0.0, 1.0}});
    REQUIRE(handle.playState() == LaunchHandle::PlayState::playing);
}

}  // namespace

TEST_CASE("A slot's handle does not outlive the slot", "[engine][session][launch]") {
    NoFactory factory;
    RuntimeStateStore store(factory);
    LaunchHandleFeed feed;

    store.publishHandles(snapshotWithScenes({0, 2}), feed);

    auto* first = store.findHandle(SlotKey{kTrack, 0});
    REQUIRE(first != nullptr);
    REQUIRE(store.findHandle(SlotKey{kTrack, 2}) != nullptr);
    launch(*first);

    SECTION("a snapshot that still names it keeps the handle it had") {
        // The whole point of keeping one: an edit somewhere else in the project
        // republishes the snapshot, and a clip that is playing goes on playing.
        store.publishHandles(snapshotWithScenes({0, 2}), feed);

        auto* kept = store.findHandle(SlotKey{kTrack, 0});
        REQUIRE(kept == first);
        CHECK(kept->playState() == LaunchHandle::PlayState::playing);
    }

    SECTION("emptying one slot does not disturb the others") {
        store.publishHandles(snapshotWithScenes({0}), feed);

        CHECK(store.findHandle(SlotKey{kTrack, 0}) == first);
        CHECK(store.findHandle(SlotKey{kTrack, 2}) == nullptr);
    }

    SECTION("a slot emptied and refilled comes back new") {
        // The bug this rule exists for, and the reason retirement is here
        // rather than at a plan publish: emptying a slot and filling it again
        // is two clip edits and no structural one, so nothing else would have
        // run in between and the new clip would come up already playing, three
        // bars into a run it never started.
        store.publishHandles(snapshotWithScenes({2}), feed);
        CHECK(store.findHandle(SlotKey{kTrack, 0}) == nullptr);

        store.publishHandles(snapshotWithScenes({0, 2}), feed);

        auto* refilled = store.findHandle(SlotKey{kTrack, 0});
        REQUIRE(refilled != nullptr);
        CHECK(refilled->playState() == LaunchHandle::PlayState::stopped);
        CHECK_FALSE(refilled->playedRange().has_value());
        CHECK_FALSE(refilled->lastPlayedRange().has_value());
    }

    SECTION("a slot whose track is gone goes with it") {
        store.publishHandles(snapshotWithScenes({0, 2}, kTrack + 1), feed);

        CHECK(store.findHandle(SlotKey{kTrack, 0}) == nullptr);
        CHECK(store.findHandle(SlotKey{kTrack, 2}) == nullptr);
        CHECK(store.findHandle(SlotKey{kTrack + 1, 0}) != nullptr);
    }

    SECTION("a project with nothing in it keeps nothing") {
        store.publishHandles(ClipSnapshot{}, feed);

        CHECK(store.findHandle(SlotKey{kTrack, 0}) == nullptr);
        CHECK(store.findHandle(SlotKey{kTrack, 2}) == nullptr);
    }
}

TEST_CASE("A plan publish is not what retires a handle", "[engine][session][launch]") {
    // Runtime state is retired against what the model holds, and a slot is not
    // in that walk: it is a clip. A structural edit that has lost the track
    // must not take a handle the live table still points a session source at,
    // because the source would be left with a pointer to nothing.
    NoFactory factory;
    RuntimeStateStore store(factory);
    LaunchHandleFeed feed;

    store.publishHandles(snapshotWithScenes({0, 1}), feed);
    const auto before = store.size();

    const RenderPlan empty;
    const RuntimeStateIds nothing;

    CHECK(store.releaseDeleted(empty, nothing, nullptr) == 0);
    CHECK(store.size() == before);
    CHECK(store.findHandle(SlotKey{kTrack, 0}) != nullptr);
}
