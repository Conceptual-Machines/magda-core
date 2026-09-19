// Phase 1 of the clip tempo ownership work (#2674): sequences that define done.
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "audio/AudioThumbnailManager.hpp"
#include "clip/ClipSnapshot.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/EventPlacement.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/SourcePool.hpp"
#include "core/TempoMap.hpp"
#include "io/SourceReaders.hpp"
#include "transport/TempoMap.hpp"

using Catch::Approx;
using magda::AudioEvent;
using magda::ClipInfo;
using magda::ClipManager;
using magda::ClipView;
using magda::SourcePool;
using magda::engine::AudioFileReader;
using magda::engine::ClipLane;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSourceInfo;
using magda::engine::compileClipSnapshot;
using magda::engine::readThrough;
using magda::engine::sourceReadFor;
using magda::engine::TempoChange;
using magda::engine::TempoMap;
using magda::engine::TimeSignatureChange;

namespace {

constexpr magda::TrackId kTrack = 1;
constexpr magda::SourceId kSource = 7;
constexpr const char* kSourcePath = "/tmp/magda-fixtures/loop.wav";

/// One tempo in 4/4 from the beginning.
TempoMap makeTempoMap(double bpm = 120.0) {
    return TempoMap({{0.0, bpm, 0.0f}}, {{0.0, 4, 4}});
}

/// 120 for two beats, then 60. A step is two changes at one beat; a single
/// change at beat 2 would ramp the first two beats towards it.
TempoMap makeStepTempoMap() {
    return TempoMap({TempoChange{.startBeat = 0.0, .bpm = 120.0},
                     TempoChange{.startBeat = 2.0, .bpm = 120.0},
                     TempoChange{.startBeat = 2.0, .bpm = 60.0}},
                    {TimeSignatureChange{.startBeat = 0.0, .numerator = 4, .denominator = 4}});
}

std::vector<ClipSourceInfo> makeSources(double sampleRate = 48000.0, double durationSeconds = 4.0) {
    return {ClipSourceInfo{kSource, kSourcePath, sampleRate, durationSeconds}};
}

ClipInfo makeAudioClip(magda::ClipId id, double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = kTrack;
    clip.view = ClipView::Arrangement;
    clip.name = "Clip " + juce::String(id);
    clip.setAudioContent();

    AudioEvent event;
    event.sourceId = kSource;
    event.interpBpm = 120.0;
    clip.audio().addEvent(event);

    clip.setPlacementBeats(startBeat, lengthBeats);
    return clip;
}

ClipInfo makeSessionClip(magda::ClipId id, int sceneIndex, double startBeat, double lengthBeats) {
    auto clip = makeAudioClip(id, startBeat, lengthBeats);
    clip.view = ClipView::Session;
    clip.sceneIndex = sceneIndex;
    return clip;
}

AudioEvent& eventOf(ClipInfo& clip) {
    return clip.audio().events.front();
}

ClipSnapshot compile(std::vector<ClipInfo> clips, const TempoMap& tempoMap,
                     std::vector<ClipSourceInfo> sources = makeSources()) {
    ClipLane lane;
    lane.trackId = kTrack;
    lane.clips = std::move(clips);
    return compileClipSnapshot({lane}, std::move(sources), tempoMap);
}

ClipSnapshot compileSession(std::vector<ClipInfo> slots, const TempoMap& tempoMap,
                            std::vector<ClipSourceInfo> sources = makeSources()) {
    ClipLane lane;
    lane.trackId = kTrack;
    lane.session = std::move(slots);
    return compileClipSnapshot({lane}, std::move(sources), tempoMap);
}

const magda::engine::AudioClipPlayback* audioClip(const ClipSnapshot& snapshot, magda::ClipId id) {
    const auto* track = snapshot.find(kTrack);
    if (track == nullptr)
        return nullptr;
    for (const auto& clip : track->audio)
        if (clip.clipId == id)
            return &clip;
    return nullptr;
}

/// Sample n reads back as n + 1, so the file's first sample and silence are
/// not the same value. Ends where it says it does, like a real file.
class OneBasedReader final : public AudioFileReader {
  public:
    OneBasedReader(std::int64_t length, double rate) : length_(length), rate_(rate) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return rate_;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        const auto available = static_cast<int>(std::clamp<std::int64_t>(
            length_ - startSample, 0, static_cast<std::int64_t>(numSamples)));
        if (available < numSamples)
            destination.clear(destinationOffset + available, numSamples - available);
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < available; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(startSample + sample + 1));
        return available;
    }

  private:
    std::int64_t length_ = 0;
    double rate_ = 0.0;
};

std::vector<float> readOut(AudioFileReader& reader, std::int64_t start, int count) {
    juce::AudioBuffer<float> destination(2, count);
    destination.clear();
    reader.read(destination, 0, start, count);
    return std::vector<float>(destination.getReadPointer(0), destination.getReadPointer(0) + count);
}

/// The model's facade over an engine map, as ClipInfo's map-aware accessors take it.
class EngineTempoMapView final : public magda::TempoMap {
  public:
    explicit EngineTempoMapView(const magda::engine::TempoMap& map) : map_(map) {}

    double beatToTime(double beat) const override {
        return map_.beatToTime(beat);
    }
    double timeToBeat(double seconds) const override {
        return map_.timeToBeat(seconds);
    }
    double bpmAt(double beat) const override {
        return map_.bpmAt(beat);
    }

  private:
    const magda::engine::TempoMap& map_;
};

/// The model singletons the ClipManager sequence goes through, emptied on both
/// sides so the test neither inherits nor leaves state.
struct ModelFixture {
    ModelFixture() {
        reset();
    }
    ~ModelFixture() {
        reset();
    }
    static void reset() {
        ClipManager::getInstance().clearAllClips();
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
        magda::AudioThumbnailManager::getInstance().clearCache();
    }
};

}  // namespace

// Fails today: sessionCycleBeats reads AudioEvent::loopLengthBeats, which ignores
// the warp map. Passes after phase 4.
TEST_CASE("A warped session slot's cycle is the warp-aware loop length",
          "[engine][clip][tempo][sequence][!mayfail]") {
    auto clip = makeSessionClip(1, 0, 0.0, 8.0);
    clip.loopEnabled = true;
    auto& event = eventOf(clip);
    event.interpBpm = 120.0;
    event.interpTotalBeats = 8.0;
    event.autoTempo = true;
    event.loopStartSamples = 0;
    event.setLoopLengthBeats(8.0);  // the whole 4 s file

    // Markers in source seconds: the last one drags the file's end from 4 s to
    // 5 s of warped time, so the region is 10 beats at 120 rather than 8.
    event.warpEnabled = true;
    event.warpMarkers = {{0.0, 0.0}, {2.0, 2.5}, {4.0, 5.0}};

    const double warpAware = clip.loopLengthInBeats(120.0);
    REQUIRE(warpAware == Approx(10.0));

    const auto snapshot = compileSession({clip}, makeTempoMap());
    REQUIRE(snapshot.tracks.size() == 1);
    const auto* slot = snapshot.tracks.front().slot(0);
    REQUIRE(slot != nullptr);

    INFO("warp-aware loop length " << warpAware << " beats, sessionCycleBeats "
                                   << clip.sessionCycleBeats(120.0) << ", slot "
                                   << slot->lengthBeats);
    CHECK(clip.sessionCycleBeats(120.0) == Approx(warpAware));
    CHECK(slot->lengthBeats == Approx(warpAware));
    REQUIRE(slot->audio.size() == 1);
    CHECK(slot->audio.front().span.beats.length() == Approx(warpAware));
}

TEST_CASE("The compiler places a clip across a tempo change by the map",
          "[engine][clip][tempo][sequence]") {
    const auto snapshot = compile({makeAudioClip(1, 0.0, 4.0)}, makeStepTempoMap());

    const auto* clip = audioClip(snapshot, 1);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->events.size() == 1);
    const auto& event = clip->events.front();

    // Two beats at 120 are a second, two at 60 are two.
    CHECK(event.span.beats.length() == Approx(4.0));
    CHECK(event.span.seconds.length() == Approx(3.0));
    CHECK(clip->span.seconds.length() == Approx(3.0));
}

// Fails today: refreshDerivedSeconds writes clip.length from a scalar BPM, and
// ClipManager has no way to be handed the map. Passes after phase 5.
TEST_CASE("A tempo change inside a clip is reflected in its seconds cache",
          "[engine][clip][tempo][sequence][!mayfail]") {
    ModelFixture fixture;
    auto& clips = ClipManager::getInstance();
    auto& pool = SourcePool::getInstance();

    const juce::String path = "/tmp/magda-fixtures/tempo-step.wav";
    pool.seedFactsForTesting(path, 4.0, 48000.0);
    pool.getMutable(pool.acquire(path))->detectedBpm = 120.0;

    const auto clipId =
        clips.createAudioClipBeats(kTrack, 0.0, 4.0, path, ClipView::Arrangement, 120.0);
    REQUIRE(clipId != magda::INVALID_CLIP_ID);

    clips.setSourceTempo(clipId, 120.0);
    clips.setAutoTempo(clipId, true, 120.0);
    REQUIRE(clips.getClip(clipId)->primaryEvent()->autoTempo);

    // The project's map: 120 for two beats, then 60. ClipManager never sees it,
    // only the 120 scalar every caller hands it.
    const auto engineMap = makeStepTempoMap();
    const EngineTempoMapView tempoMap(engineMap);

    clips.setSourceTempo(clipId, 100.0);

    const auto* clip = clips.getClip(clipId);
    REQUIRE(clip != nullptr);
    CHECK(clip->placement.lengthBeats == Approx(4.0));
    REQUIRE(clip->getTimelineLength(tempoMap) == Approx(3.0));

    INFO("map says " << clip->getTimelineLength(tempoMap) << " s, cache holds " << clip->length);
    CHECK(clip->length == Approx(3.0));
}

TEST_CASE("A loop whose region runs past the file's end reads silence there, not a wrap",
          "[engine][clip][tempo][sequence]") {
    // 16 beats at 174 is 5.517 s; the file is 5.486 s. Both counted at 48 kHz,
    // which is what an unresolved source is counted at too.
    constexpr double kRate = 48000.0;
    constexpr double kFileSeconds = 5.486;
    const auto fileSamples = static_cast<std::int64_t>(std::llround(kFileSeconds * kRate));
    const auto regionSamples = static_cast<std::int64_t>(std::llround(16.0 * 60.0 / 174.0 * kRate));
    REQUIRE(regionSamples > fileSamples);

    auto clip = makeSessionClip(1, 0, 0.0, 16.0);
    clip.loopEnabled = true;
    auto& event = eventOf(clip);
    event.interpBpm = 174.0;
    event.interpTotalBeats = 16.0;
    event.autoTempo = true;
    event.loopStartSamples = 0;
    event.setLoopLengthBeats(16.0);

    const auto snapshot = compileSession({clip}, makeTempoMap(), makeSources(kRate, kFileSeconds));
    REQUIRE(snapshot.tracks.size() == 1);
    const auto* slot = snapshot.tracks.front().slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->audio.size() == 1);
    REQUIRE(slot->audio.front().events.size() == 1);
    const auto& playback = slot->audio.front().events.front();

    CHECK(playback.loopLengthSamples == regionSamples);
    CHECK(static_cast<double>(playback.loopLengthSamples) / kRate == Approx(5.517).margin(1e-3));

    const auto how = sourceReadFor(playback, kRate);
    CHECK(how.lengthInSamples == fileSamples);
    CHECK(how.loopLengthSamples == regionSamples);

    auto reading = readThrough(std::make_unique<OneBasedReader>(fileSamples, kRate), how);
    REQUIRE(reading != nullptr);

    SECTION("the file's last sample is followed by silence, not its first") {
        const auto out = readOut(*reading, fileSamples - 2, 4);
        CHECK(out[0] == Approx(static_cast<float>(fileSamples - 1)));
        CHECK(out[1] == Approx(static_cast<float>(fileSamples)));
        CHECK(out[2] == 0.0f);
        CHECK(out[3] == 0.0f);
    }

    SECTION("the whole overrun is silent") {
        const auto tail = static_cast<int>(regionSamples - fileSamples);
        const auto out = readOut(*reading, fileSamples, tail);
        CHECK(std::all_of(out.begin(), out.end(), [](float s) { return s == 0.0f; }));
    }

    SECTION("the region's end is where the loop's first sample comes back") {
        const auto out = readOut(*reading, regionSamples - 2, 4);
        CHECK(out[0] == 0.0f);
        CHECK(out[1] == 0.0f);
        CHECK(out[2] == Approx(1.0f));
        CHECK(out[3] == Approx(2.0f));
    }
}

TEST_CASE("The native compiler resolves loop length in its authoritative unit",
          "[engine][clip][tempo][sequence][2675]") {
    constexpr double kRate = 48000.0;

    auto musical = makeAudioClip(1, 0.0, 8.0);
    musical.loopEnabled = true;
    auto& musicalEvent = eventOf(musical);
    musicalEvent.interpBpm = 120.0;
    musicalEvent.setLoopLengthBeats(4.0);
    REQUIRE(musicalEvent.adoptBpm(60.0, magda::Provenance::User));
    musicalEvent.loopLengthSamples = 123;

    auto source = makeAudioClip(2, 8.0, 8.0);
    source.loopEnabled = true;
    auto& sourceEvent = eventOf(source);
    sourceEvent.interpBpm = 120.0;
    sourceEvent.loopExtent = magda::RegionExtent::Explicit;
    sourceEvent.loopLengthIntent = magda::LoopLengthIntent::Source;
    sourceEvent.loopLengthSamples = static_cast<std::int64_t>(2.0 * kRate);
    REQUIRE(sourceEvent.adoptBpm(60.0, magda::Provenance::User));

    const auto snapshot = compile({musical, source}, makeTempoMap(), makeSources(kRate, 8.0));
    const auto* compiledMusical = audioClip(snapshot, musical.id);
    const auto* compiledSource = audioClip(snapshot, source.id);
    REQUIRE(compiledMusical != nullptr);
    REQUIRE(compiledSource != nullptr);
    REQUIRE(compiledMusical->events.size() == 1);
    REQUIRE(compiledSource->events.size() == 1);

    CHECK(compiledMusical->events.front().loopLengthSamples ==
          static_cast<std::int64_t>(4.0 * kRate));
    CHECK(compiledSource->events.front().loopLengthSamples ==
          static_cast<std::int64_t>(2.0 * kRate));

    const auto musicalRead = sourceReadFor(compiledMusical->events.front(), kRate);
    const auto sourceRead = sourceReadFor(compiledSource->events.front(), kRate);
    CHECK(musicalRead.loopLengthSamples == static_cast<std::int64_t>(4.0 * kRate));
    CHECK(sourceRead.loopLengthSamples == static_cast<std::int64_t>(2.0 * kRate));
    // At the corrected 60 BPM, source seconds and interpreted beats are equal.
    CHECK(static_cast<double>(musicalRead.loopLengthSamples) / kRate == Approx(4.0));
    CHECK(static_cast<double>(sourceRead.loopLengthSamples) / kRate == Approx(2.0));
}
