#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "clip/ClipSnapshot.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/EventPlacement.hpp"
#include "core/ClipSourceMap.hpp"
#include "transport/TempoMap.hpp"

using magda::ClipInfo;
using magda::ClipSourceMap;

namespace {

constexpr magda::TrackId kTrack = 1;
constexpr magda::SourceId kSource = 7;
constexpr double kRate = 48000.0;
constexpr double kBpm = 120.0;

ClipInfo audioClip(double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = 1;
    clip.trackId = kTrack;
    clip.view = magda::ClipView::Arrangement;
    clip.setAudioContent();

    magda::AudioEvent event;
    event.sourceId = kSource;
    event.interpBpm = kBpm;
    clip.audio().addEvent(event);
    clip.setPlacementBeats(startBeat, lengthBeats);
    return clip;
}

magda::AudioEvent& eventOf(ClipInfo& clip) {
    return clip.audio().events.front();
}

/// The engine's answer and the model's agree at every beat across the clip.
void requireMatchesEngine(const ClipInfo& clip) {
    const magda::engine::TempoMap tempo({{0.0, kBpm, 0.0f}}, {{0.0, 4, 4}});
    const auto snapshot = magda::engine::compileClipSnapshot(
        {magda::engine::ClipLane{kTrack, {clip}}},
        {magda::engine::ClipSourceInfo{kSource, "/tmp/magda-fixtures/map.wav", kRate, 10.0}},
        tempo);
    const auto* track = snapshot.find(kTrack);
    REQUIRE(track != nullptr);
    REQUIRE(track->audio.size() == 1);
    const auto& played = track->audio.front();
    const auto& event = played.events.front();
    const auto how = magda::engine::sourceReadFor(event, kRate);

    const auto map = ClipSourceMap::of(clip, kBpm, kRate);
    REQUIRE(map.has_value());

    const auto start = clip.placement.startBeat;
    for (auto beat = start; beat < start + clip.placement.lengthBeats; beat += 0.37) {
        const auto seconds = beat * 60.0 / kBpm;
        const auto reading = magda::engine::readingPositionAt(played, event, seconds, beat, kRate);
        const auto engine = magda::engine::sourceSecondsOfReading(how, reading);
        INFO("beat " << beat);
        REQUIRE(map->sourceSecondsAt(beat - start) == Catch::Approx(engine).margin(1e-4));
    }
}

}  // namespace

TEST_CASE("The arrangement's source map is the one the engine plays",
          "[engine][clip][gain-envelope]") {
    auto clip = audioClip(4.0, 8.0);

    SECTION("plain") {}
    SECTION("slipped") {
        eventOf(clip).sourceAnchorSamples = static_cast<std::int64_t>(1.25 * kRate);
    }
    SECTION("sped up") {
        eventOf(clip).speedRatio = 1.5;
    }
    SECTION("in beat mode at its own tempo") {
        eventOf(clip).autoTempo = true;
        eventOf(clip).interpBpm = 100.0;
    }
    SECTION("reversed") {
        eventOf(clip).sourceAnchorSamples = static_cast<std::int64_t>(0.5 * kRate);
        eventOf(clip).reversed = true;
    }
    SECTION("looped") {
        clip.loopEnabled = true;
        eventOf(clip).loopStartSamples = static_cast<std::int64_t>(1.0 * kRate);
        eventOf(clip).sourceAnchorSamples = static_cast<std::int64_t>(1.3 * kRate);
        eventOf(clip).setLoopLengthSeconds(0.9);
        eventOf(clip).loopLengthSamples = static_cast<std::int64_t>(0.9 * kRate);
    }
    SECTION("reversed and looped") {
        clip.loopEnabled = true;
        eventOf(clip).reversed = true;
        eventOf(clip).loopStartSamples = static_cast<std::int64_t>(1.0 * kRate);
        eventOf(clip).sourceAnchorSamples = static_cast<std::int64_t>(1.3 * kRate);
        eventOf(clip).setLoopLengthSeconds(0.9);
        eventOf(clip).loopLengthSamples = static_cast<std::int64_t>(0.9 * kRate);
    }
    SECTION("warped") {
        eventOf(clip).warpEnabled = true;
        eventOf(clip).warpMarkers = {{0.0, 0.0}, {1.0, 1.6}, {3.0, 3.2}, {6.0, 6.0}};
    }
    SECTION("warped and looped") {
        clip.loopEnabled = true;
        eventOf(clip).warpEnabled = true;
        eventOf(clip).warpMarkers = {{0.0, 0.0}, {1.0, 1.6}, {3.0, 3.2}, {6.0, 6.0}};
        eventOf(clip).loopStartSamples = static_cast<std::int64_t>(0.5 * kRate);
        eventOf(clip).sourceAnchorSamples = static_cast<std::int64_t>(0.5 * kRate);
        eventOf(clip).setLoopLengthSeconds(1.5);
        eventOf(clip).loopLengthSamples = static_cast<std::int64_t>(1.5 * kRate);
    }

    requireMatchesEngine(clip);
}

TEST_CASE("A source point is found once in every loop pass", "[clip][gain-envelope]") {
    auto clip = audioClip(0.0, 8.0);
    clip.loopEnabled = true;
    eventOf(clip).loopStartSamples = 0;
    eventOf(clip).setLoopLengthSeconds(1.0);
    eventOf(clip).loopLengthSamples = static_cast<std::int64_t>(kRate);

    const auto map = ClipSourceMap::of(clip, kBpm, kRate);
    REQUIRE(map.has_value());

    // A one-second loop at 120 bpm is two beats, so four passes in eight.
    const auto beats = map->clipBeatsOf(0.25, 0.0, 8.0, 800);
    REQUIRE(beats.size() == 4);
    for (std::size_t pass = 0; pass < beats.size(); ++pass)
        CHECK(beats[pass] == Catch::Approx(0.5 + (2.0 * static_cast<double>(pass))).margin(0.02));
}
