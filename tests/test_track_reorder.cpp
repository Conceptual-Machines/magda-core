#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {
struct ReorderFixture {
    ReorderFixture() {
        TrackManager::getInstance().clearAllTracks();
    }
    ~ReorderFixture() {
        TrackManager::getInstance().clearAllTracks();
    }
    TrackManager& tm() {
        return TrackManager::getInstance();
    }
};
}  // namespace

TEST_CASE("moveTrackToPosition reorders top-level tracks", "[tracks][reorder]") {
    ReorderFixture fx;
    auto a = fx.tm().createTrack("A", TrackType::Audio);
    auto b = fx.tm().createTrack("B", TrackType::Audio);
    auto c = fx.tm().createTrack("C", TrackType::Audio);

    SECTION("move last to the front") {
        fx.tm().moveTrackToPosition(c, 1);
        REQUIRE(fx.tm().getTopLevelTracks() == std::vector<TrackId>{c, a, b});
    }

    SECTION("move first to the middle") {
        fx.tm().moveTrackToPosition(a, 2);
        REQUIRE(fx.tm().getTopLevelTracks() == std::vector<TrackId>{b, a, c});
    }

    SECTION("move first to the end") {
        fx.tm().moveTrackToPosition(a, 3);
        REQUIRE(fx.tm().getTopLevelTracks() == std::vector<TrackId>{b, c, a});
    }

    SECTION("position is clamped to range") {
        fx.tm().moveTrackToPosition(a, 99);
        REQUIRE(fx.tm().getTopLevelTracks() == std::vector<TrackId>{b, c, a});
    }
}

TEST_CASE("moveTrackToPosition reorders within a group, not across", "[tracks][reorder][group]") {
    ReorderFixture fx;
    auto k = fx.tm().createTrack("Kick", TrackType::Audio);
    auto s = fx.tm().createTrack("Snare", TrackType::Audio);
    auto h = fx.tm().createTrack("Hat", TrackType::Audio);

    auto group = fx.tm().groupTracks({k, s, h}, "Drums");
    REQUIRE(group != INVALID_TRACK_ID);
    const auto* g = fx.tm().getTrack(group);
    REQUIRE(g != nullptr);
    REQUIRE(g->childIds == std::vector<TrackId>{k, s, h});

    // Move the hat to the front of the group.
    fx.tm().moveTrackToPosition(h, 1);
    REQUIRE(fx.tm().getTrack(group)->childIds == std::vector<TrackId>{h, k, s});

    // The grouped child stays inside the group (parent unchanged).
    REQUIRE(fx.tm().getTrack(h)->parentId == group);
}

TEST_CASE("getTrackSiblingPosition is 1-based among siblings", "[tracks][reorder]") {
    ReorderFixture fx;
    auto a = fx.tm().createTrack("A", TrackType::Audio);
    auto b = fx.tm().createTrack("B", TrackType::Audio);
    auto c = fx.tm().createTrack("C", TrackType::Audio);

    REQUIRE(fx.tm().getTrackSiblingPosition(a) == 1);
    REQUIRE(fx.tm().getTrackSiblingPosition(c) == 3);

    fx.tm().moveTrackToPosition(c, 1);
    REQUIRE(fx.tm().getTrackSiblingPosition(c) == 1);
    REQUIRE(fx.tm().getTrackSiblingPosition(a) == 2);
    (void)b;
}
