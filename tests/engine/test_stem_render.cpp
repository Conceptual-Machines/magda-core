// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/engine/host/StemRender.hpp"

/**
 * @file test_stem_render.cpp
 * @brief Which tracks a stem export writes, and what each stem renders (#54).
 */

using magda::TrackId;
using magda::TrackInfo;
using magda::TrackType;
using magda::daw::engine_host::stemSources;
using magda::daw::engine_host::stemTracks;

namespace {

TrackInfo track(TrackId id, TrackType type = TrackType::Media) {
    TrackInfo info;
    info.id = id;
    info.type = type;
    info.audioOutputDevice = "master";
    return info;
}

TrackInfo child(TrackId id, TrackId parent, TrackType type = TrackType::Media) {
    auto info = track(id, type);
    info.parentId = parent;
    info.audioOutputDevice = "track:" + juce::String(parent);
    return info;
}

std::set<TrackId> asSet(const std::vector<TrackId>& ids) {
    return {ids.begin(), ids.end()};
}

}  // namespace

TEST_CASE("Every track that plays gets a stem, in list order with the returns last",
          "[engine][stems][54]") {
    auto aux = track(5, TrackType::Aux);
    auto group = track(2, TrackType::Group);
    group.childIds = {4, 3};
    auto muted = track(6);
    muted.muted = true;
    auto mutedGroup = track(10, TrackType::Group);
    mutedGroup.muted = true;
    mutedGroup.childIds = {9};
    auto intoMuted = track(12);
    intoMuted.audioOutputDevice = "track:6";
    auto off = track(11);
    off.enabled = false;

    const std::vector<TrackInfo> tracks{aux,
                                        track(1),
                                        group,
                                        child(3, 2),
                                        child(4, 2),
                                        muted,
                                        track(7, TrackType::Chord),
                                        track(8, TrackType::Separator),
                                        mutedGroup,
                                        child(9, 10),
                                        off,
                                        intoMuted};

    CHECK(stemTracks(tracks) == std::vector<TrackId>{1, 2, 4, 3, 5});
}

TEST_CASE("A stem renders its track and every track whose audio reaches it",
          "[engine][stems][54]") {
    auto source = track(1);
    source.sends.push_back({.busIndex = 0, .destTrackId = 5});
    auto group = track(2, TrackType::Group);
    group.childIds = {3, 4};
    group.sends.push_back({.busIndex = 0, .destTrackId = 5});
    auto disabledSend = track(6);
    disabledSend.sends.push_back({.busIndex = 0, .destTrackId = 5, .enabled = false});

    auto grid = track(20);
    auto pad = child(21, 20, TrackType::Pad);
    auto multiOut = track(30, TrackType::MultiOut);
    multiOut.multiOutLink = magda::MultiOutTrackLink{.sourceTrackId = 1};

    const std::vector<TrackInfo> tracks{source,
                                        group,
                                        child(3, 2),
                                        child(4, 2),
                                        track(5, TrackType::Aux),
                                        disabledSend,
                                        grid,
                                        pad,
                                        child(22, 20, TrackType::Pad),
                                        multiOut};

    CHECK(stemSources(tracks, 1) == std::vector<TrackId>{1});
    CHECK(asSet(stemSources(tracks, 2)) == std::set<TrackId>{2, 3, 4});

    SECTION("a return is fed by its senders and what feeds them") {
        const auto sources = stemSources(tracks, 5);
        CHECK(sources.front() == 5);
        CHECK(asSet(sources) == std::set<TrackId>{1, 2, 3, 4, 5});
    }

    SECTION("a pad or multi-out channel takes the track whose instrument plays it") {
        CHECK(asSet(stemSources(tracks, 21)).contains(20));
        CHECK(asSet(stemSources(tracks, 30)) == std::set<TrackId>{1, 30});
    }
}
