#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/host/OfflineRenderModel.hpp"

/**
 * @file test_offline_render_model.cpp
 * @brief What a render request cuts out of the project before it compiles (#2555).
 */

using magda::ChainElement;
using magda::ClipId;
using magda::ClipInfo;
using magda::DeviceInfo;
using magda::OfflineRenderRequest;
using magda::TrackId;
using magda::TrackInfo;
using magda::daw::engine_host::narrowForRender;
using magda::daw::engine_host::OfflineRenderModel;

namespace {

TrackInfo track(TrackId id) {
    TrackInfo info;
    info.id = id;
    info.volume = 0.5f;
    info.pan = -0.25f;
    return info;
}

DeviceInfo device(magda::DeviceId id, bool instrument) {
    DeviceInfo info;
    info.id = id;
    info.isInstrument = instrument;
    return info;
}

ClipInfo clip(ClipId id, TrackId trackId) {
    ClipInfo info;
    info.id = id;
    info.trackId = trackId;
    return info;
}

magda::engine::ClipLane lane(TrackId trackId, std::vector<ClipInfo> clips) {
    magda::engine::ClipLane result;
    result.trackId = trackId;
    result.clips = std::move(clips);
    result.session = {clip(99, trackId)};
    result.playbackMode = magda::TrackPlaybackMode::Session;
    return result;
}

/// Three tracks: 1 routes into bus 2 and sends to aux 3.
OfflineRenderModel project() {
    OfflineRenderModel model;
    model.master = track(magda::MASTER_TRACK_ID);

    auto source = track(1);
    source.audioOutputDevice = "track:2";
    source.sends.push_back({.busIndex = 0, .level = 1.0f, .preFader = false, .destTrackId = 3});
    source.chain.fxChainElements.emplace_back(device(10, false));
    source.chain.fxChainElements.emplace_back(device(11, true));
    source.chain.fxChainElements.emplace_back(device(12, false));
    source.chain.postFxChainElements.push_back({.device = device(13, false)});

    auto aux = track(3);
    aux.auxBusIndex = 0;

    model.tracks = {source, track(2), aux};
    model.lanes = {lane(1, {clip(1, 1), clip(2, 1)}), lane(2, {}), lane(3, {})};
    return model;
}

const TrackInfo* find(const OfflineRenderModel& model, TrackId id) {
    for (const auto& info : model.tracks)
        if (info.id == id)
            return &info;
    return nullptr;
}

}  // namespace

TEST_CASE("A render of one track routes what fed a dropped track to the master",
          "[engine][offline][2555]") {
    OfflineRenderRequest request;
    request.trackIds = {1};

    const auto narrowed = narrowForRender(project(), request);

    REQUIRE(narrowed.tracks.size() == 1);
    CHECK(narrowed.tracks[0].audioOutputDevice == "master");
    CHECK(narrowed.tracks[0].sends.empty());
    REQUIRE(narrowed.lanes.size() == 1);
    CHECK(narrowed.lanes[0].trackId == 1);
}

TEST_CASE("An excluded track leaves every other route alone", "[engine][offline][2555]") {
    OfflineRenderRequest request;
    request.excludedTrackIds = {3};

    const auto narrowed = narrowForRender(project(), request);

    const auto* source = find(narrowed, 1);
    REQUIRE(source != nullptr);
    CHECK(find(narrowed, 3) == nullptr);
    CHECK(source->audioOutputDevice == "track:2");
    CHECK(source->sends.empty());
}

TEST_CASE("A render plays the arrangement and none of the session", "[engine][offline][2555]") {
    const auto narrowed = narrowForRender(project(), {});

    for (const auto& rendered : narrowed.lanes) {
        CHECK(rendered.session.empty());
        CHECK(rendered.playbackMode == magda::TrackPlaybackMode::Arrangement);
    }
}

TEST_CASE("A clip filter keeps only the clips it names", "[engine][offline][2555]") {
    OfflineRenderRequest request;
    request.clipIds = {2};

    const auto narrowed = narrowForRender(project(), request);

    REQUIRE(!narrowed.lanes.empty());
    REQUIRE(narrowed.lanes[0].clips.size() == 1);
    CHECK(narrowed.lanes[0].clips[0].id == 2);
}

TEST_CASE("Rendering without track effects keeps the chain up to its instrument at unity",
          "[engine][offline][2555]") {
    OfflineRenderRequest request;
    request.useTrackEffects = false;

    const auto narrowed = narrowForRender(project(), request);
    const auto* source = find(narrowed, 1);
    REQUIRE(source != nullptr);

    REQUIRE(source->chain.fxChainElements.size() == 2);
    CHECK(magda::getDevice(source->chain.fxChainElements.back()).id == 11);
    CHECK(source->chain.postFxChainElements.empty());
    CHECK(source->volume == 1.0f);
    CHECK(source->pan == 0.0f);
}

TEST_CASE("Rendering without plugins empties the chains and the faders with them",
          "[engine][offline][2555]") {
    OfflineRenderRequest request;
    request.usePlugins = false;
    request.useMasterPlugins = false;

    auto model = project();
    model.master.chain.fxChainElements.emplace_back(device(20, false));

    magda::AutomationLaneInfo fader;
    fader.target.kind = magda::ControlTarget::Kind::TrackVolume;
    fader.target.devicePath = magda::ChainNodePath::trackLevel(1);
    model.automation.push_back(fader);

    const auto narrowed = narrowForRender(std::move(model), request);
    const auto* source = find(narrowed, 1);
    REQUIRE(source != nullptr);

    CHECK(source->chain.fxChainElements.empty());
    CHECK(source->volume == 1.0f);
    CHECK(narrowed.master.chain.fxChainElements.empty());
    CHECK(narrowed.automation.empty());
}

TEST_CASE("A render hears no live input", "[engine][offline][2555]") {
    auto model = project();
    model.tracks[0].recordArmed = true;
    model.tracks[0].inputMonitor = magda::InputMonitorMode::In;

    const auto narrowed = narrowForRender(std::move(model), {});
    const auto* source = find(narrowed, 1);
    REQUIRE(source != nullptr);

    CHECK_FALSE(source->recordArmed);
    CHECK(source->inputMonitor == magda::InputMonitorMode::Off);
}

TEST_CASE("A freeze renders its track up to the fader, unmuted, with nothing soloed",
          "[engine][offline][2555]") {
    auto model = project();
    model.tracks[1].muted = true;
    model.tracks[1].soloed = true;
    model.tracks[1].chain.postFxPostFader = true;
    model.tracks[1].chain.postFxChainElements.push_back({.device = device(21, false)});
    model.tracks[1].chain.fxChainElements.emplace_back(device(20, false));
    model.tracks[0].soloed = true;

    magda::AutomationLaneInfo fader;
    fader.target.kind = magda::ControlTarget::Kind::TrackVolume;
    fader.target.devicePath = magda::ChainNodePath::trackLevel(2);
    model.automation.push_back(fader);

    OfflineRenderRequest request;
    request.trackIds = {2, 1};
    request.freezeTrackId = 2;
    request.useMasterPlugins = false;

    const auto narrowed = narrowForRender(std::move(model), request);
    const auto* frozen = find(narrowed, 2);
    const auto* feeding = find(narrowed, 1);
    REQUIRE(frozen != nullptr);
    REQUIRE(feeding != nullptr);

    CHECK(frozen->volume == 1.0f);
    CHECK(frozen->pan == 0.0f);
    CHECK_FALSE(frozen->muted);
    CHECK_FALSE(frozen->soloed);
    CHECK(frozen->chain.fxChainElements.size() == 1);
    CHECK(frozen->chain.postFxChainElements.empty());
    CHECK(narrowed.automation.empty());

    CHECK_FALSE(feeding->soloed);
    CHECK(feeding->volume == 0.5f);
    CHECK(feeding->audioOutputDevice == "track:2");
}
