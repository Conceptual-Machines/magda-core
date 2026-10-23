#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "clip/ClipSnapshotCompiler.hpp"
#include "magda/daw/audio/ara/AraMusicalContent.hpp"
#include "magda/daw/audio/ara/AraRegionTiming.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file test_ara_conversions.cpp
 * @brief The tempo grid and region times a host-side ARA document hands a plugin (#1579).
 */

using Catch::Approx;
using magda::daw::audio::ara::AraBarSignature;
using magda::daw::audio::ara::barSignaturesFor;
using magda::daw::audio::ara::regionTimingFor;
using magda::daw::audio::ara::tempoEntriesFor;
using magda::engine::TempoMap;

namespace {

constexpr magda::TrackId kTrack = 1;
constexpr magda::SourceId kSource = 7;
constexpr double kRate = 48000.0;
constexpr double kFileSeconds = 10.0;

/** @brief One arrangement clip of one event, compiled at 120 bpm. */
struct CompiledClip {
    magda::engine::ClipSnapshot snapshot;

    const magda::engine::AudioClipPlayback& clip() const {
        return snapshot.find(kTrack)->audio.front();
    }
    const magda::engine::AudioEventPlayback& event() const {
        return clip().events.front();
    }
};

CompiledClip compile(double startBeat, double lengthBeats, double anchorSeconds,
                     void (*shape)(magda::ClipInfo&, magda::AudioEvent&) = nullptr) {
    magda::ClipInfo clip;
    clip.id = 1;
    clip.trackId = kTrack;
    clip.view = magda::ClipView::Arrangement;
    clip.setAudioContent();

    magda::AudioEvent event;
    event.sourceId = kSource;
    event.interpBpm = 120.0;
    clip.audio().addEvent(event);
    clip.setPlacementBeats(startBeat, lengthBeats);
    clip.primaryEvent()->sourceAnchorSamples = static_cast<std::int64_t>(anchorSeconds * kRate);
    if (shape != nullptr)
        shape(clip, *clip.primaryEvent());

    magda::engine::ClipLane lane;
    lane.trackId = kTrack;
    lane.clips.push_back(clip);
    const TempoMap tempo({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
    auto snapshot = magda::engine::compileClipSnapshot(
        {lane},
        {{.id = kSource, .path = "a.wav", .sampleRate = kRate, .durationSeconds = kFileSeconds}},
        tempo);
    REQUIRE(snapshot.find(kTrack) != nullptr);
    REQUIRE(snapshot.find(kTrack)->audio.size() == 1);
    return {std::move(snapshot)};
}

}  // namespace

TEST_CASE("ARA tempo entries follow the baked tempo map", "[ara]") {
    SECTION("a flat map is two entries a beat apart") {
        const auto entries = tempoEntriesFor(TempoMap({{0.0, 120.0, 0.0f}}, {}));
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].seconds == Approx(0.0));
        CHECK(entries[0].quarters == Approx(0.0));
        CHECK(entries[1].seconds == Approx(0.5));
        CHECK(entries[1].quarters == Approx(1.0));
    }

    SECTION("a step lands one entry on its beat, not two") {
        const TempoMap map({{0.0, 120.0, 0.0f}, {4.0, 120.0, 0.0f}, {4.0, 60.0, 0.0f}}, {});
        const auto entries = tempoEntriesFor(map);
        for (std::size_t i = 1; i < entries.size(); ++i) {
            CHECK(entries[i].quarters > entries[i - 1].quarters);
            CHECK(entries[i].seconds > entries[i - 1].seconds);
        }
        CHECK(entries.back().quarters == Approx(5.0));
        CHECK(entries.back().seconds == Approx(3.0));
    }

    SECTION("a ramp is sampled where the map itself is") {
        const TempoMap map({{0.0, 120.0, 0.0f}, {4.0, 60.0, 0.0f}}, {});
        const auto entries = tempoEntriesFor(map);
        CHECK(entries.size() > 3);
        for (const auto& entry : entries)
            CHECK(entry.seconds == Approx(map.beatToTime(entry.quarters)));
    }
}

TEST_CASE("ARA bar signatures follow the signature changes", "[ara]") {
    const TempoMap map({{0.0, 120.0, 0.0f}, {2.0, 90.0, 0.0f}}, {{0.0, 4, 4}, {8.0, 3, 4}});
    const auto bars = barSignaturesFor(map);
    REQUIRE(bars.size() == 2);
    CHECK(bars[0] == AraBarSignature{.quarters = 0.0, .numerator = 4, .denominator = 4});
    CHECK(bars[1] == AraBarSignature{.quarters = 8.0, .numerator = 3, .denominator = 4});
}

TEST_CASE("An ARA region is the source stretch its event plays", "[ara]") {
    SECTION("at unity, the event's own anchor and length at its timeline start") {
        // Beats 4..8 at 120 bpm: two seconds from 2.0, reading from one second into the file.
        const auto compiled = compile(4.0, 4.0, 1.0);
        const auto timing = regionTimingFor(compiled.clip(), compiled.event());
        CHECK(timing.startInModification == Approx(1.0));
        CHECK(timing.durationInModification == Approx(2.0));
        CHECK(timing.startInPlayback == Approx(2.0));
        CHECK(timing.durationInPlayback == Approx(2.0));
    }

    SECTION("a trim past the file's end stops at the end") {
        const auto compiled = compile(0.0, 8.0, 9.0);
        const auto timing = regionTimingFor(compiled.clip(), compiled.event());
        CHECK(timing.startInModification == Approx(9.0));
        CHECK(timing.durationInModification == Approx(1.0));
    }

    SECTION("a looping clip covers its loop region") {
        const auto compiled =
            compile(0.0, 16.0, 0.5, [](magda::ClipInfo& clip, magda::AudioEvent& e) {
                clip.loopEnabled = true;
                e.loopStartSamples = static_cast<std::int64_t>(0.5 * kRate);
                e.loopLengthSamples = static_cast<std::int64_t>(1.0 * kRate);
            });
        const auto timing = regionTimingFor(compiled.clip(), compiled.event());
        CHECK(timing.startInModification == Approx(0.5));
        CHECK(timing.durationInModification == Approx(1.0));
    }

    SECTION("a reversed event covers the same stretch, read the other way") {
        const auto forward = compile(4.0, 4.0, 1.0);
        const auto reversed = compile(
            4.0, 4.0, 1.0, [](magda::ClipInfo&, magda::AudioEvent& e) { e.reversed = true; });
        const auto a = regionTimingFor(forward.clip(), forward.event());
        const auto b = regionTimingFor(reversed.clip(), reversed.event());
        CHECK(b.durationInModification == Approx(a.durationInModification));
        CHECK(b.startInModification >= 0.0);
        CHECK(b.startInModification + b.durationInModification <= Approx(kFileSeconds));
    }
}
