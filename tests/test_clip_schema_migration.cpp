#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "core/ClipInfo.hpp"
#include "core/SourcePool.hpp"
#include "project/serialization/ProjectSerializer.hpp"

using namespace magda;
using Catch::Approx;

namespace {

constexpr double kProjectTempo = 120.0;
constexpr double kSourceRate = 48000.0;

struct MigrationFixture {
    MigrationFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
    ~MigrationFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
};

juce::DynamicObject* obj(juce::var& value) {
    return value.getDynamicObject();
}

/// A schema v1 audio clip: one source, one interpretation, one playback block,
/// and the interpretation fields sitting on the clip itself.
juce::var makeV1Clip(const juce::String& filePath, double sourceDurationSeconds) {
    auto* clip = new juce::DynamicObject();
    clip->setProperty("id", 1);
    clip->setProperty("trackId", 1);
    clip->setProperty("name", "Migrated");
    clip->setProperty("type", static_cast<int>(ClipType::Audio));
    clip->setProperty("view", static_cast<int>(ClipView::Arrangement));
    clip->setProperty("enabled", true);

    auto* placement = new juce::DynamicObject();
    placement->setProperty("startBeat", 4.0);
    placement->setProperty("lengthBeats", 8.0);
    clip->setProperty("placement", juce::var(placement));

    auto* source = new juce::DynamicObject();
    source->setProperty("filePath", filePath);
    source->setProperty("durationSeconds", sourceDurationSeconds);

    auto* interpretation = new juce::DynamicObject();
    interpretation->setProperty("bpm", 0.0);
    interpretation->setProperty("totalBeats", 0.0);
    interpretation->setProperty("totalBeatsLocked", false);

    auto* playback = new juce::DynamicObject();
    playback->setProperty("offsetSeconds", 0.0);
    playback->setProperty("offsetBeats", 0.0);
    playback->setProperty("loopStartSeconds", 0.0);
    playback->setProperty("loopLengthSeconds", 0.0);
    playback->setProperty("loopStartBeats", 0.0);
    playback->setProperty("loopLengthBeats", 0.0);
    playback->setProperty("speedRatio", 1.0);

    auto* audio = new juce::DynamicObject();
    audio->setProperty("source", juce::var(source));
    audio->setProperty("interpretation", juce::var(interpretation));
    audio->setProperty("playback", juce::var(playback));
    clip->setProperty("audio", juce::var(audio));

    return juce::var(clip);
}

juce::DynamicObject& playbackOf(juce::var& clip) {
    auto audio = obj(clip)->getProperty("audio");
    return *obj(audio)->getProperty("playback").getDynamicObject();
}

juce::DynamicObject& interpretationOf(juce::var& clip) {
    auto audio = obj(clip)->getProperty("audio");
    return *obj(audio)->getProperty("interpretation").getDynamicObject();
}

ClipInfo load(const juce::var& clipJson) {
    ClipInfo out;
    REQUIRE(ProjectSerializer::deserializeClipInfo(clipJson, out, kProjectTempo));
    return out;
}

}  // namespace

// =============================================================================
// A v1 clip becomes a clip holding exactly one event spanning it
// =============================================================================

TEST_CASE("A v1 audio clip migrates to a single event", "[clip][serialization][migration]") {
    MigrationFixture fixture;
    SourcePool::getInstance().seedFactsForTesting("/tmp/v1.wav", 4.0, kSourceRate);

    auto json = makeV1Clip("/tmp/v1.wav", 4.0);
    const auto clip = load(json);

    REQUIRE(clip.isAudio());
    REQUIRE(clip.events().size() == 1);

    const auto& event = *clip.primaryEvent();
    REQUIRE(event.id != INVALID_EVENT_ID);
    REQUIRE(event.sourceId != INVALID_SOURCE_ID);
    REQUIRE(event.sourceFilePath() == "/tmp/v1.wav");

    SECTION("The event spans the clip") {
        REQUIRE(clip.placement.lengthBeats == Approx(8.0));
        REQUIRE(event.startBeat == Approx(0.0));
        REQUIRE(event.lengthBeats == Approx(8.0));
    }

    SECTION("The file's duration lands on the pooled source, not the event") {
        REQUIRE(event.sourceDurationSeconds() == Approx(4.0));
    }
}

TEST_CASE("Two v1 clips on one file share a pooled source", "[clip][serialization][migration]") {
    MigrationFixture fixture;
    SourcePool::getInstance().seedFactsForTesting("/tmp/shared.wav", 4.0, kSourceRate);

    auto first = makeV1Clip("/tmp/shared.wav", 4.0);
    auto second = makeV1Clip("/tmp/shared.wav", 4.0);
    obj(second)->setProperty("id", 2);

    const auto a = load(first);
    const auto b = load(second);

    REQUIRE(a.primaryEvent()->sourceId == b.primaryEvent()->sourceId);
    REQUIRE(SourcePool::getInstance().snapshot().size() == 1);
}

// =============================================================================
// The source domain converts into samples
// =============================================================================

TEST_CASE("A non-autoTempo v1 clip converts its seconds to samples",
          "[clip][serialization][migration]") {
    MigrationFixture fixture;
    SourcePool::getInstance().seedFactsForTesting("/tmp/v1.wav", 8.0, kSourceRate);

    auto json = makeV1Clip("/tmp/v1.wav", 8.0);
    playbackOf(json).setProperty("offsetSeconds", 1.5);
    playbackOf(json).setProperty("loopStartSeconds", 1.0);
    playbackOf(json).setProperty("loopLengthSeconds", 2.0);
    playbackOf(json).setProperty("speedRatio", 1.25);
    obj(json)->setProperty("loopEnabled", true);

    const auto clip = load(json);
    const auto& event = *clip.primaryEvent();

    REQUIRE(event.sourceAnchorSamples == static_cast<int64_t>(1.5 * kSourceRate));
    REQUIRE(event.loopStartSamples == static_cast<int64_t>(1.0 * kSourceRate));
    REQUIRE(event.loopLengthSamples == static_cast<int64_t>(2.0 * kSourceRate));
    REQUIRE(clip.loopEnabled);
    REQUIRE(event.speedRatio == Approx(1.25));
}

TEST_CASE("An autoTempo v1 clip converts its source BEATS to samples",
          "[clip][serialization][migration]") {
    MigrationFixture fixture;
    SourcePool::getInstance().seedFactsForTesting("/tmp/v1.wav", 8.0, kSourceRate);

    // v1 kept beats authoritative under autoTempo and derived the seconds, so
    // the beat fields are what the migration has to read.
    auto json = makeV1Clip("/tmp/v1.wav", 8.0);
    obj(json)->setProperty("autoTempo", true);
    obj(json)->setProperty("loopEnabled", true);
    interpretationOf(json).setProperty("bpm", 120.0);
    interpretationOf(json).setProperty("totalBeats", 16.0);
    playbackOf(json).setProperty("offsetBeats", 4.0);      // 2 s at 120 bpm
    playbackOf(json).setProperty("loopStartBeats", 2.0);   // 1 s
    playbackOf(json).setProperty("loopLengthBeats", 8.0);  // 4 s
    playbackOf(json).setProperty("offsetSeconds", 999.0);  // stale derived cache
    playbackOf(json).setProperty("loopStartSeconds", 999.0);
    playbackOf(json).setProperty("loopLengthSeconds", 999.0);

    const auto clip = load(json);
    const auto& event = *clip.primaryEvent();

    REQUIRE(event.autoTempo);
    REQUIRE(event.interpBpm == Approx(120.0));
    REQUIRE(event.anchorSeconds() == Approx(2.0));
    REQUIRE(event.loopStartSeconds() == Approx(1.0));
    REQUIRE(event.loopLengthSeconds() == Approx(4.0));

    SECTION("The beat view survives the round trip") {
        REQUIRE(event.anchorBeats() == Approx(4.0));
        REQUIRE(event.loopStartBeats() == Approx(2.0));
        REQUIRE(event.loopLengthBeats() == Approx(8.0));
    }
}

TEST_CASE("A v1 clip whose file is missing migrates and rescales on relink",
          "[clip][serialization][migration]") {
    MigrationFixture fixture;
    // No seeded facts: the pool cannot probe, so the source stays unresolved.
    auto json = makeV1Clip("/tmp/magda_missing_source.wav", 6.0);
    playbackOf(json).setProperty("offsetSeconds", 1.0);

    auto clip = load(json);
    auto& event = *clip.primaryEvent();
    const auto* source = event.source();
    REQUIRE(source != nullptr);
    REQUIRE_FALSE(source->isResolved());

    SECTION("The anchor is computed at the nominal rate") {
        REQUIRE(event.sourceAnchorSamples ==
                static_cast<int64_t>(1.0 * kUnresolvedSourceSampleRate));
        REQUIRE(event.anchorSeconds() == Approx(1.0));
    }

    SECTION("The declared duration is kept so the clip is still measurable") {
        REQUIRE(event.sourceDurationSeconds() == Approx(6.0));
    }

    SECTION("Resolving the file reports the transition that triggers a rescale") {
        SourcePool::getInstance().seedFactsForTesting("/tmp/magda_missing_source.wav", 6.0,
                                                      kSourceRate);
        REQUIRE(SourcePool::getInstance().resolveFacts(event.sourceId));
        REQUIRE(event.source()->isResolved());
    }
}

// =============================================================================
// Clip-level v1 fields land on the event
// =============================================================================

TEST_CASE("A v1 clip's interpretation fields move onto its event",
          "[clip][serialization][migration]") {
    MigrationFixture fixture;
    SourcePool::getInstance().seedFactsForTesting("/tmp/v1.wav", 4.0, kSourceRate);

    auto json = makeV1Clip("/tmp/v1.wav", 4.0);
    obj(json)->setProperty("isReversed", true);
    obj(json)->setProperty("transpose", -5);
    obj(json)->setProperty("pitchChange", 2.5);
    obj(json)->setProperty("autoPitch", true);
    obj(json)->setProperty("autoPitchMode", 2);
    obj(json)->setProperty("autoDetectBeats", true);
    obj(json)->setProperty("beatSensitivity", 0.75);
    obj(json)->setProperty("leftChannelActive", false);
    obj(json)->setProperty("fadeIn", 0.25);
    obj(json)->setProperty("fadeOut", 0.5);
    obj(json)->setProperty("fadeInType", 3);
    obj(json)->setProperty("fadeOutBehaviour", 1);

    const auto clip = load(json);
    const auto& event = *clip.primaryEvent();

    REQUIRE(event.reversed);
    REQUIRE(event.transpose == -5);
    REQUIRE(event.pitchChange == Approx(2.5f));
    REQUIRE(event.autoPitch);
    REQUIRE(event.autoPitchMode == 2);
    REQUIRE(event.autoDetectBeats);
    REQUIRE(event.beatSensitivity == Approx(0.75f));
    REQUIRE_FALSE(event.leftChannelActive);
    REQUIRE(event.rightChannelActive);

    SECTION("Fades keep their seconds: the split is behaviour-neutral") {
        REQUIRE(event.fadeInSeconds == Approx(0.25));
        REQUIRE(event.fadeOutSeconds == Approx(0.5));
        REQUIRE(event.fadeInType == 3);
        REQUIRE(event.fadeOutBehaviour == 1);
    }
}

TEST_CASE("A v1 clip's warp markers move onto its event unchanged",
          "[clip][serialization][migration]") {
    MigrationFixture fixture;
    SourcePool::getInstance().seedFactsForTesting("/tmp/v1.wav", 4.0, kSourceRate);

    auto json = makeV1Clip("/tmp/v1.wav", 4.0);
    auto audio = obj(json)->getProperty("audio");
    obj(audio)->setProperty("warpEnabled", true);

    juce::Array<juce::var> markers;
    for (auto pair : {std::pair{0.0, 0.0}, std::pair{1.0, 1.5}}) {
        auto* marker = new juce::DynamicObject();
        marker->setProperty("sourceTime", pair.first);
        marker->setProperty("warpTime", pair.second);
        markers.add(juce::var(marker));
    }
    obj(audio)->setProperty("warpMarkers", markers);

    const auto clip = load(json);
    const auto& event = *clip.primaryEvent();

    REQUIRE(event.warpEnabled);
    REQUIRE(event.warpMarkers.size() == 2);
    REQUIRE(event.warpMarkers[1].sourceTime == Approx(1.0));
    REQUIRE(event.warpMarkers[1].warpTime == Approx(1.5));
}

TEST_CASE("A v1 MIDI clip's loop moves into the MIDI content",
          "[clip][serialization][migration][midi]") {
    MigrationFixture fixture;

    auto* clip = new juce::DynamicObject();
    clip->setProperty("id", 3);
    clip->setProperty("trackId", 1);
    clip->setProperty("type", static_cast<int>(ClipType::MIDI));
    clip->setProperty("view", static_cast<int>(ClipView::Arrangement));
    clip->setProperty("loopEnabled", true);
    clip->setProperty("loopStartBeats", 2.0);
    clip->setProperty("loopLengthBeats", 4.0);

    auto* placement = new juce::DynamicObject();
    placement->setProperty("startBeat", 0.0);
    placement->setProperty("lengthBeats", 16.0);
    clip->setProperty("placement", juce::var(placement));

    const auto loaded = load(juce::var(clip));

    REQUIRE(loaded.isMidi());
    REQUIRE(loaded.primaryEvent() == nullptr);
    REQUIRE(loaded.loopEnabled);
    REQUIRE(loaded.loopStartBeats == Approx(2.0));
    REQUIRE(loaded.loopLengthBeats == Approx(4.0));
}

// =============================================================================
// v2 round-trip
// =============================================================================

TEST_CASE("A v2 audio clip round-trips its events", "[clip][serialization]") {
    MigrationFixture fixture;

    ClipInfo original;
    original.id = 11;
    original.trackId = 1;
    original.name = "Round Trip";
    auto& event = magda::test::giveAudioEvent(original, "/tmp/rt.wav", 4.0, kSourceRate);
    original.setPlacementBeats(2.0, 8.0);

    event.interpBpm = 174.0;
    event.interpTotalBeats = 12.0;
    event.interpTotalBeatsLocked = true;
    event.keyRoot = "A";
    event.keyScale = "minor";
    event.autoTempo = true;
    event.warpEnabled = true;
    event.warpMarkers.push_back({0.5, 0.75});
    event.transpose = 3;
    event.pitchChange = -1.5f;
    event.reversed = true;
    event.gainDB = -2.5f;
    event.fadeInSeconds = 0.1;
    event.fadeOutType = 4;
    original.loopEnabled = true;
    event.setLoopStartSeconds(0.5);
    event.setLoopLengthSeconds(1.5);
    event.setAnchorSeconds(0.75);

    const auto json = ProjectSerializer::serializeClipInfo(original);
    const auto restored = load(json);
    const auto& restoredEvent = *restored.primaryEvent();

    REQUIRE(restored.events().size() == 1);
    REQUIRE(restoredEvent.id == event.id);
    REQUIRE(restoredEvent.sourceFilePath() == "/tmp/rt.wav");

    SECTION("Geometry") {
        REQUIRE(restored.placement.startBeat == Approx(2.0));
        REQUIRE(restoredEvent.startBeat == Approx(0.0));
        REQUIRE(restoredEvent.lengthBeats == Approx(8.0));
    }

    SECTION("The source domain survives to the sample") {
        REQUIRE(restoredEvent.sourceAnchorSamples == event.sourceAnchorSamples);
        REQUIRE(restoredEvent.loopStartSamples == event.loopStartSamples);
        REQUIRE(restoredEvent.loopLengthSamples == event.loopLengthSamples);
        REQUIRE(restored.loopEnabled);
    }

    SECTION("Interpretation and per-event mix") {
        REQUIRE(restoredEvent.interpBpm == Approx(174.0));
        REQUIRE(restoredEvent.interpTotalBeats == Approx(12.0));
        REQUIRE(restoredEvent.interpTotalBeatsLocked);
        REQUIRE(restoredEvent.keyRoot == "A");
        REQUIRE(restoredEvent.keyScale == "minor");
        REQUIRE(restoredEvent.autoTempo);
        REQUIRE(restoredEvent.transpose == 3);
        REQUIRE(restoredEvent.pitchChange == Approx(-1.5f));
        REQUIRE(restoredEvent.reversed);
        REQUIRE(restoredEvent.gainDB == Approx(-2.5f));
        REQUIRE(restoredEvent.fadeInSeconds == Approx(0.1));
        REQUIRE(restoredEvent.fadeOutType == 4);
    }

    SECTION("Warp markers keep their seconds pair") {
        REQUIRE(restoredEvent.warpEnabled);
        REQUIRE(restoredEvent.warpMarkers.size() == 1);
        REQUIRE(restoredEvent.warpMarkers[0].sourceTime == Approx(0.5));
        REQUIRE(restoredEvent.warpMarkers[0].warpTime == Approx(0.75));
    }
}

TEST_CASE("A long source keeps sample precision through the file", "[clip][serialization]") {
    MigrationFixture fixture;

    // juce::var has no 64-bit integer, so anchors travel as strings. A double
    // would start losing whole samples on a long file.
    ClipInfo original;
    auto& event = magda::test::giveAudioEvent(original, "/tmp/long.wav", 7200.0, kSourceRate);
    original.setPlacementBeats(0.0, 4.0);
    event.sourceAnchorSamples = 345'678'901'234LL;

    const auto restored = load(ProjectSerializer::serializeClipInfo(original));
    REQUIRE(restored.primaryEvent()->sourceAnchorSamples == 345'678'901'234LL);
}

TEST_CASE("A v2 clip round-trips several events", "[clip][serialization]") {
    MigrationFixture fixture;

    // Nothing builds these yet, but the schema and the loader carry N so the
    // event-level features do not need another migration.
    ClipInfo original;
    magda::test::giveAudioEvent(original, "/tmp/a.wav", 4.0, kSourceRate);
    original.setPlacementBeats(0.0, 8.0);

    auto& second = original.audio().addEvent({});
    second.sourceId = SourcePool::getInstance().acquire("/tmp/b.wav");
    second.startBeat = 4.0;
    second.lengthBeats = 4.0;
    second.setAnchorSeconds(1.0);

    const auto restored = load(ProjectSerializer::serializeClipInfo(original));

    REQUIRE(restored.events().size() == 2);
    REQUIRE(restored.events()[0].sourceFilePath() == "/tmp/a.wav");
    REQUIRE(restored.events()[1].sourceFilePath() == "/tmp/b.wav");
    REQUIRE(restored.events()[1].startBeat == Approx(4.0));
    REQUIRE(restored.events()[1].anchorSeconds() == Approx(1.0));

    SECTION("The next event id clears every restored event") {
        REQUIRE(restored.audio().nextEventId > restored.events()[1].id);
    }
}

TEST_CASE("v1 migration stages its source instead of pooling it",
          "[clip][serialization][migration][staging]") {
    // Staging runs on a background thread and can still fail afterwards, so it
    // must not write the message-thread pool the open project is reading.
    // Before this, a v1 clip acquired as it migrated: a failed load left its
    // sources behind and raced every paint-time sourceRateOf.
    MigrationFixture fixture;
    auto& pool = SourcePool::getInstance();

    auto v1 = makeV1Clip("/tmp/legacy-staged.wav", 4.0);

    std::vector<Source> legacySources;
    ClipInfo clip;
    REQUIRE(ProjectSerializer::deserializeClipInfo(v1, clip, kProjectTempo, &legacySources));

    SECTION("Nothing reached the live pool") {
        REQUIRE(pool.snapshot().empty());
        REQUIRE(pool.findByPath("/tmp/legacy-staged.wav") == INVALID_SOURCE_ID);
    }

    SECTION("The event holds a provisional id backed by the staged entry") {
        const auto* event = clip.primaryEvent();
        REQUIRE(event != nullptr);
        REQUIRE(event->sourceId < INVALID_SOURCE_ID);  // negative, never -1
        REQUIRE(legacySources.size() == 1);
        REQUIRE(legacySources.front().id == event->sourceId);
        REQUIRE(legacySources.front().filePath == "/tmp/legacy-staged.wav");
        REQUIRE(legacySources.front().durationSeconds == Approx(4.0));
    }

    SECTION("Two clips on one file share one staged entry, keeping the longest duration") {
        // A short recorded duration would otherwise clamp the other clip's
        // anchors and cap its right-extension.
        auto shorter = makeV1Clip("/tmp/legacy-staged.wav", 1.0);
        ClipInfo second;
        REQUIRE(
            ProjectSerializer::deserializeClipInfo(shorter, second, kProjectTempo, &legacySources));

        REQUIRE(legacySources.size() == 1);
        REQUIRE(legacySources.front().durationSeconds == Approx(4.0));
        REQUIRE(second.primaryEvent()->sourceId == clip.primaryEvent()->sourceId);
    }

    SECTION("Without a sink the live pool is still used, for the single-clip callers") {
        ClipInfo direct;
        REQUIRE(ProjectSerializer::deserializeClipInfo(v1, direct, kProjectTempo));
        REQUIRE(pool.findByPath("/tmp/legacy-staged.wav") != INVALID_SOURCE_ID);
    }
}
