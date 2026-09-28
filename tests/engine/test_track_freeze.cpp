#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/host/EngineProject.hpp"
#include "magda/daw/engine/host/TrackFreeze.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_track_freeze.cpp
 * @brief What a freeze renders, and what a frozen track plays (#2555).
 */

using magda::ClipInfo;
using magda::DeviceInfo;
using magda::TrackId;
using magda::TrackInfo;
using magda::daw::engine_host::FrozenTrack;

namespace host = magda::daw::engine_host;
namespace engine = magda::engine;

namespace {

TrackInfo track(TrackId id, const juce::String& output = "master") {
    TrackInfo info;
    info.id = id;
    info.audioOutputDevice = output;
    return info;
}

DeviceInfo device(magda::DeviceId id) {
    DeviceInfo info;
    info.id = id;
    info.audioInputChannels = 2;
    info.audioOutputChannels = 2;
    return info;
}

ClipInfo midiClip(magda::ClipId id, TrackId trackId, double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = trackId;
    clip.setMidiContent();
    clip.setPlacementBeats(startBeat, lengthBeats);
    return clip;
}

engine::ClipLane lane(TrackId trackId, std::vector<ClipInfo> clips) {
    engine::ClipLane result;
    result.trackId = trackId;
    result.clips = std::move(clips);
    return result;
}

/// Track 1 feeds 2, which feeds 3, the one frozen; track 4 goes to the master on its own.
std::vector<TrackInfo> project() {
    auto frozen = track(3);
    frozen.frozen = true;
    frozen.chain.fxChainElements.emplace_back(device(30));
    frozen.chain.postFxPostFader = true;
    frozen.chain.postFxChainElements.push_back({.device = device(31)});
    frozen.audioInputDevice = "track:4";

    auto feeding = track(1, "track:2");
    feeding.chain.fxChainElements.emplace_back(device(10));

    return {feeding, track(2, "track:3"), frozen, track(4)};
}

const FrozenTrack kFrozen{.trackId = 3, .source = 7, .durationSeconds = 3.0};

bool hasTrack(const std::vector<TrackInfo>& tracks, TrackId id) {
    return std::ranges::find(tracks, id, &TrackInfo::id) != tracks.end();
}

bool plansDevice(const engine::RenderPlan& plan, magda::DeviceId id) {
    return std::ranges::any_of(plan.ops, [id](const auto& op) {
        return op.kind == engine::OpKind::Device && op.key.deviceId == id;
    });
}

}  // namespace

TEST_CASE("The tracks feeding a freeze are every route that reaches it", "[engine][freeze][2555]") {
    auto feeding = host::tracksFeeding(project(), 3);
    std::ranges::sort(feeding);

    CHECK(feeding == std::vector<TrackId>{1, 2});
    CHECK(host::tracksFeeding(project(), 4).empty());
}

TEST_CASE("A track routed into another track is refused a freeze", "[engine][freeze][2555]") {
    CHECK(host::freezeRefusal(project(), 3).isEmpty());
    CHECK(host::freezeRefusal(project(), 2).isNotEmpty());
}

TEST_CASE("A freeze renders from zero to the last clip on anything it holds",
          "[engine][freeze][2555]") {
    const std::vector<engine::ClipLane> lanes{lane(1, {midiClip(1, 1, 4.0, 12.0)}),
                                              lane(3, {midiClip(2, 3, 0.0, 8.0)}),
                                              lane(4, {midiClip(3, 4, 0.0, 64.0)})};

    const auto request = host::freezeRequest(project(), lanes, 3, juce::File("/tmp/freeze.wav"),
                                             {.sampleRate = 48000.0, .maxBlockSize = 256});
    REQUIRE(request.has_value());

    CHECK(request->range.start.value == 0.0);
    CHECK(request->range.end.value == 16.0);
    CHECK_FALSE(request->tailSeconds.has_value());
    CHECK_FALSE(request->useMasterPlugins);
    CHECK(request->freezeTrackId == 3);
    CHECK(request->sampleRate == 48000.0);
    CHECK(request->blockSize == 256);
    CHECK(request->bitDepth == 32);
    CHECK(std::ranges::find(request->trackIds, 4) == request->trackIds.end());

    CHECK_FALSE(host::freezeRequest(project(), {lane(4, {midiClip(3, 4, 0.0, 4.0)})}, 3,
                                    juce::File("/tmp/freeze.wav"), {})
                    .has_value());
}

TEST_CASE("A frozen track plays with its fader and nothing before it", "[engine][freeze][2555]") {
    const auto played = host::tracksAsPlayed(project(), {kFrozen});

    CHECK_FALSE(hasTrack(played, 1));
    CHECK_FALSE(hasTrack(played, 2));
    REQUIRE(hasTrack(played, 3));
    CHECK(hasTrack(played, 4));

    const auto frozen = *std::ranges::find(played, 3, &TrackInfo::id);
    CHECK(frozen.chain.fxChainElements.empty());
    CHECK(frozen.chain.postFxChainElements.size() == 1);
    CHECK(frozen.audioInputDevice.isEmpty());

    const auto plan = engine::compileRenderPlan(played, track(magda::MASTER_TRACK_ID));
    CHECK_FALSE(plansDevice(plan, 10));
    CHECK_FALSE(plansDevice(plan, 30));
    CHECK(plansDevice(plan, 31));
}

TEST_CASE("A frozen track's lane is its file from beat zero", "[engine][freeze][2555]") {
    const std::vector<engine::ClipLane> lanes{lane(1, {midiClip(1, 1, 0.0, 4.0)}),
                                              lane(3, {midiClip(2, 3, 0.0, 4.0)}),
                                              lane(4, {midiClip(3, 4, 0.0, 4.0)})};
    const auto tempo = host::tempoMapAt(120.0, 4, 4);

    const auto played = host::lanesAsPlayed(lanes, project(), {kFrozen}, tempo);
    const auto snapshot = engine::compileClipSnapshot(
        played,
        {{.id = 7, .path = "/tmp/freeze.wav", .sampleRate = 48000.0, .durationSeconds = 3.0}},
        tempo);

    CHECK(snapshot.find(1) == nullptr);
    CHECK(snapshot.find(2) == nullptr);
    REQUIRE(snapshot.find(4) != nullptr);

    const auto* frozen = snapshot.find(3);
    REQUIRE(frozen != nullptr);
    CHECK(frozen->midi.empty());
    REQUIRE(frozen->audio.size() == 1);
    REQUIRE(frozen->audio[0].events.size() == 1);

    const auto& event = frozen->audio[0].events[0];
    CHECK(event.filePath == "/tmp/freeze.wav");
    CHECK(event.anchorSamples == 0);
    CHECK(event.span.seconds.start == Catch::Approx(0.0));
    CHECK(event.span.seconds.end == Catch::Approx(3.0));
}
