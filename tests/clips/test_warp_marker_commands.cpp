#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/WarpMarkerCommands.hpp"

using namespace magda;
using Catch::Approx;

namespace {
struct WarpFixture : ClipManagerListener {
    ClipId id;
    std::vector<WarpMarker> displayed;
    int notifications = 0;

    WarpFixture() {
        auto& clips = ClipManager::getInstance();
        UndoManager::getInstance().clearHistory();
        clips.clearAllClips();
        TrackManager::getInstance().clearAllTracks();
        auto track = TrackManager::getInstance().createTrack("Warp");
        id = clips.createAudioClip(track, 12.0, 2.0,
                                   juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile("warp-edit-test.wav")
                                       .getFullPathName());
        test::setSourceDuration(*clips.getClip(id), 8.0);
        auto& event = *clips.getClip(id)->primaryEvent();
        event.warpEnabled = true;
        event.setAnchorSeconds(2.0);
        clips.addListener(this);
    }
    ~WarpFixture() override {
        ClipManager::getInstance().removeListener(this);
        UndoManager::getInstance().clearHistory();
        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
    }
    void clipsChanged() override {}
    void clipPropertyChanged(ClipId clip) override {
        if (clip == id) {
            ++notifications;
            displayed = getClipWarpMarkers(id);
        }
    }
    AudioEvent& event() {
        return *ClipManager::getInstance().getClip(id)->primaryEvent();
    }
};
}  // namespace

TEST_CASE("Warp edits without a bridge notify readers and undo the authored map", "[warp][2672]") {
    WarpFixture f;
    auto& undo = UndoManager::getInstance();
    CHECK(getClipWarpMarkers(f.id) == std::vector<WarpMarker>{{0, 0}, {8, 8}});
    undo.executeCommand(std::make_unique<AddWarpMarkerCommand>(f.id, 3.0, 4.0));
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {3, 4}, {8, 8}});
    CHECK(f.event().warpedSourceSeconds(3) == Approx(4));
    CHECK(f.event().anchorSeconds() == Approx(2));
    CHECK(f.notifications == 1);
    REQUIRE(undo.undo());
    CHECK(f.event().warpMarkers.empty());
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {8, 8}});
    REQUIRE(undo.redo());
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {3, 4}, {8, 8}});

    undo.executeCommand(std::make_unique<MoveWarpMarkerCommand>(f.id, 1, 5.0));
    undo.executeCommand(std::make_unique<MoveWarpMarkerCommand>(f.id, 1, 6.0));
    CHECK(f.displayed[1].warpTime == Approx(6));
    REQUIRE(undo.undo());
    CHECK(f.displayed[1].warpTime == Approx(4));
    REQUIRE(undo.redo());
    CHECK(f.displayed[1].warpTime == Approx(6));

    undo.executeCommand(std::make_unique<RemoveWarpMarkerCommand>(f.id, 1));
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {8, 8}});
    REQUIRE(undo.undo());
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {3, 6}, {8, 8}});
    REQUIRE(undo.redo());
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {8, 8}});
}

TEST_CASE("Native warp edits preserve boundaries and reject degenerate segments", "[warp][2672]") {
    WarpFixture f;
    f.event().warpMarkers = {{0, 0}, {3, 4}, {8, 8}};
    MoveWarpMarkerCommand crossing(f.id, 1, 50.0);
    crossing.execute();
    CHECK(f.event().warpMarkers[1].warpTime == Approx(8 - 5 * 0.10001));
    crossing.undo();
    CHECK(f.event().warpMarkers[1].warpTime == Approx(4));
    const auto before = f.event().warpMarkers;
    for (double source : {0.0, 3.0, 8.0, -1.0, 9.0}) {
        AddWarpMarkerCommand invalid(f.id, source, 4.0);
        invalid.execute();
        invalid.undo();
        CHECK(f.event().warpMarkers == before);
    }
    MoveWarpMarkerCommand invalid(f.id, 1, std::numeric_limits<double>::quiet_NaN());
    invalid.execute();
    CHECK(f.event().warpMarkers == before);
    RemoveWarpMarkerCommand boundary(f.id, 0);
    boundary.execute();
    CHECK(f.event().warpMarkers.size() == 3);
}

TEST_CASE("Warp reposition is one undoable operation without an audio bridge", "[warp][2672]") {
    WarpFixture f;
    f.event().warpMarkers = {{0, 0}, {3, 4}, {8, 8}};
    auto& undo = UndoManager::getInstance();
    {
        CompoundOperationScope scope("Reposition Warp Marker");
        undo.executeCommand(std::make_unique<RemoveWarpMarkerCommand>(f.id, 1));
        undo.executeCommand(std::make_unique<AddWarpMarkerCommand>(f.id, 5.0, 6.0));
    }
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {5, 6}, {8, 8}});
    REQUIRE(undo.undo());
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {3, 4}, {8, 8}});
    REQUIRE(undo.redo());
    CHECK(f.displayed == std::vector<WarpMarker>{{0, 0}, {5, 6}, {8, 8}});
}
