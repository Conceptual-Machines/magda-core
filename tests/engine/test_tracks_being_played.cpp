// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

#include "../../magda/daw/core/ClipManager.hpp"
#include "../../magda/daw/core/SelectionManager.hpp"
#include "../../magda/daw/core/TrackManager.hpp"
#include "../../magda/daw/engine/host/TracksBeingPlayed.hpp"

// Which tracks render at the callback while the rest render ahead (#1898).

using namespace magda;
using magda::daw::engine_host::tracksBeingPlayed;

namespace {

struct Project {
    Project() {
        clips.clearAllClips();
        tracks.clearAllTracks();
        for (int i = 0; i < 4; ++i)
            ids.push_back(tracks.createTrack("Track " + std::to_string(i), TrackType::Media));
        selection.clearSelection();
        selection.setClipInEditor(INVALID_CLIP_ID);
    }
    ~Project() {
        selection.clearSelection();
        selection.setClipInEditor(INVALID_CLIP_ID);
        clips.clearAllClips();
        tracks.clearAllTracks();
    }

    std::set<TrackId> played() const {
        return tracksBeingPlayed(tracks.getTracks(), selection, clips);
    }

    TrackInfo& track(int index) {
        return *tracks.getTrack(ids[static_cast<std::size_t>(index)]);
    }

    ClipManager& clips = ClipManager::getInstance();
    TrackManager& tracks = TrackManager::getInstance();
    SelectionManager& selection = SelectionManager::getInstance();
    std::vector<TrackId> ids;
};

}  // namespace

TEST_CASE("An idle, unselected track is not being played", "[engine][1898]") {
    Project project;
    CHECK(project.played().empty());
}

TEST_CASE("An armed or monitoring track is being played", "[engine][1898]") {
    Project project;
    project.track(0).recordArmed = true;
    project.track(0).inputMonitor = InputMonitorMode::Off;
    project.track(1).inputMonitor = InputMonitorMode::In;
    project.track(2).inputMonitor = InputMonitorMode::Auto;

    CHECK(project.played() == std::set<TrackId>{project.ids[0], project.ids[1]});
}

TEST_CASE("A selected track, clip or notes alone are not being played", "[engine][1898]") {
    Project project;
    const auto clip = project.clips.createMidiClip(project.ids[2], 0.0, 2.0, ClipView::Arrangement);

    SECTION("a track") {
        project.selection.selectTrack(project.ids[1]);
    }
    SECTION("a clip") {
        project.selection.selectClip(clip);
    }
    SECTION("notes in a clip") {
        project.selection.selectNotes(clip, {0});
    }
    CHECK(project.played().empty());
}

TEST_CASE("The clip open in an editor and the selected device are being played", "[engine][1898]") {
    Project project;

    SECTION("a clip in an editor") {
        const auto clip =
            project.clips.createMidiClip(project.ids[2], 0.0, 2.0, ClipView::Arrangement);
        project.selection.setClipInEditor(clip);
        CHECK(project.played() == std::set<TrackId>{project.ids[2]});
    }
    SECTION("a device") {
        project.selection.selectDevice(project.ids[0], 7);
        CHECK(project.played().contains(project.ids[0]));
    }
}
