#include <catch2/catch_test_macros.hpp>

#include "clip/ClipSnapshot.hpp"
#include "core/TrackInfo.hpp"
#include "exec/RuntimeStateStore.hpp"

/**
 * @file test_launch_handle_lifetime.cpp
 * @brief Which slot handles survive a publish, and which do not (#2301).
 *
 * A handle is per slot and the plan is per track, so the plan cannot say which
 * scenes are filled. What can is the snapshot, and these are the cases that say
 * a slot emptied and refilled comes back new rather than carrying the run it
 * had before.
 */

using namespace magda;
using namespace magda::engine;

namespace {

constexpr TrackId kTrack = 1;

/// A factory that builds nothing: these cases are about lifetime rather than
/// about what a handle is attached to.
class NoFactory final : public RuntimeStateFactory {};

TrackInfo trackWithId(TrackId id) {
    TrackInfo track;
    track.id = id;
    return track;
}

/// A snapshot naming @p scenes on kTrack, with no material in them: what is
/// asserted here is which slots exist, not what they play.
ClipSnapshot snapshotWithScenes(std::vector<int> scenes) {
    ClipSnapshot snapshot;
    TrackClipPlayback track;
    track.trackId = kTrack;
    for (const auto scene : scenes)
        track.session.push_back(SessionSlotPlayback{scene, {}, {}, 4.0});
    snapshot.tracks.push_back(std::move(track));
    return snapshot;
}

}  // namespace

TEST_CASE("A slot's handle does not outlive the slot", "[engine][session][launch]") {
    NoFactory factory;
    RuntimeStateStore store(factory);

    auto& first = store.handle(SlotKey{kTrack, 0});
    store.handle(SlotKey{kTrack, 2});

    first.play({});
    first.advance(SyncRange{BeatRange{0.0, 2.0}, BeatRange{0.0, 2.0}});
    REQUIRE(first.playState() == LaunchHandle::PlayState::playing);

    const auto tracks = std::vector<TrackInfo>{trackWithId(kTrack)};
    const auto master = trackWithId(MASTER_TRACK_ID);
    const RenderPlan empty;

    SECTION("a slot the snapshot still names keeps it") {
        const auto ids = collectRuntimeStateIds(tracks, master, snapshotWithScenes({0, 2}));
        store.releaseDeleted(empty, ids, nullptr);

        auto* kept = store.findHandle(SlotKey{kTrack, 0});
        REQUIRE(kept != nullptr);
        CHECK(kept->playState() == LaunchHandle::PlayState::playing);
    }

    SECTION("emptying one slot does not disturb the others") {
        const auto ids = collectRuntimeStateIds(tracks, master, snapshotWithScenes({0}));
        store.releaseDeleted(empty, ids, nullptr);

        CHECK(store.findHandle(SlotKey{kTrack, 0}) != nullptr);
        CHECK(store.findHandle(SlotKey{kTrack, 2}) == nullptr);
    }

    SECTION("a slot emptied and refilled comes back new") {
        // The bug this rule exists for. The track survives, so a keep set that
        // only knew tracks would hand the next clip in scene 0 a handle that is
        // already playing, three bars into a run it never started.
        const auto emptied = collectRuntimeStateIds(tracks, master, snapshotWithScenes({}));
        store.releaseDeleted(empty, emptied, nullptr);

        CHECK(store.findHandle(SlotKey{kTrack, 0}) == nullptr);

        auto& refilled = store.handle(SlotKey{kTrack, 0});
        CHECK(refilled.playState() == LaunchHandle::PlayState::stopped);
        CHECK_FALSE(refilled.playedRange().has_value());
        CHECK_FALSE(refilled.lastPlayedRange().has_value());
    }

    SECTION("a caller that cannot name slots keeps them by track") {
        // Absent is not empty: a walk with no snapshot to hand does not get to
        // retire a handle it knows nothing about.
        const auto unknown = collectRuntimeStateIds(tracks, master);
        REQUIRE_FALSE(unknown.slots.has_value());

        store.releaseDeleted(empty, unknown, nullptr);

        CHECK(store.findHandle(SlotKey{kTrack, 0}) != nullptr);
        CHECK(store.findHandle(SlotKey{kTrack, 2}) != nullptr);
    }

    SECTION("a slot whose track is gone goes with it, stale snapshot or not") {
        // The snapshot still names both slots and the model no longer has the
        // track at all. Snapshots publish on their own schedule, so this is an
        // ordinary state rather than a corrupt one, and the track is what
        // settles it: the slots only ever narrow what the track already keeps.
        const auto gone = collectRuntimeStateIds({}, master, snapshotWithScenes({0, 2}));
        store.releaseDeleted(empty, gone, nullptr);

        CHECK(store.findHandle(SlotKey{kTrack, 0}) == nullptr);
        CHECK(store.findHandle(SlotKey{kTrack, 2}) == nullptr);
    }
}
