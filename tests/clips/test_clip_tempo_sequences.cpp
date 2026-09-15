// Phase 1 of the clip tempo ownership work (#2674): sequences that define done.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/SourcePool.hpp"
#include "core/UndoManager.hpp"
#include "project/serialization/ProjectSerializer.hpp"

using namespace magda;
using Catch::Approx;

namespace {

constexpr double kProjectBpm = 120.0;

// The loop that motivated this: 16 beats at 175 last 5.486 s.
constexpr double kFileSeconds = 5.486;
constexpr double kFileRate = 44100.0;
constexpr double kFileBpm = 175.0;
constexpr double kFileBeats = kFileSeconds * kFileBpm / 60.0;

struct TempoSequenceFixture {
    TempoSequenceFixture() {
        reset();
    }
    ~TempoSequenceFixture() {
        reset();
    }
    static void reset() {
        UndoManager::getInstance().clearHistory();
        ClipManager::getInstance().clearAllClips();
        AudioThumbnailManager::getInstance().clearCache();
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
};

/// A real file, because the detection pass at creation is guarded on one
/// existing. Its contents do not matter: the pool answers from seeded facts.
struct LoopFile {
    LoopFile() : temp(".wav") {
        temp.getFile().replaceWithText("not audio");
        SourcePool::getInstance().seedFactsForTesting(path(), kFileSeconds, kFileRate);
    }
    juce::String path() const {
        return temp.getFile().getFullPathName();
    }
    juce::TemporaryFile temp;
};

/// A session clip dropped at the file's length in a 120 project.
ClipId dropSessionClip(const juce::String& path) {
    const double placementBeats = kFileSeconds * kProjectBpm / 60.0;
    return ClipManager::getInstance().createAudioClipBeats(1, 0.0, placementBeats, path,
                                                           ClipView::Session, kProjectBpm);
}

/// A session clip whose tempo was cached before the drop: 175 / 16 beats,
/// beat mode on, and a region covering the whole file.
ClipId dropDetectedLoop(const juce::String& path) {
    AudioThumbnailManager::getInstance().cacheBPM(path, kFileBpm);
    const auto clipId = dropSessionClip(path);
    const auto* event = ClipManager::getInstance().getClip(clipId)->primaryEvent();
    REQUIRE(event != nullptr);
    REQUIRE(event->autoTempo);
    REQUIRE(event->interpBpm == Approx(kFileBpm));
    REQUIRE(event->interpTotalBeats == Approx(kFileBeats));
    REQUIRE(event->loopLengthSeconds() == Approx(kFileSeconds).margin(0.001));
    return clipId;
}

}  // namespace

// Fails today: setAutoTempo treats an interpretation equal to the project tempo
// as defaulted and replaces it from the cache. Passes after phase 2.
TEST_CASE("A tempo the user typed survives the BEAT toggle", "[clip][tempo][sequence][!mayfail]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropSessionClip(file.path());
    REQUIRE(!clips.getClip(clipId)->primaryEvent()->hasInterpretedBpm());

    ClipManager::AudioClipBeatsUpdate typed;
    typed.interpretationBpm = kProjectBpm;
    typed.interpretationTotalBeats = kFileSeconds * kProjectBpm / 60.0;
    clips.applyAudioClipBeats(clipId, typed, kProjectBpm);

    AudioThumbnailManager::getInstance().cacheBPM(file.path(), kFileBpm);
    clips.setAutoTempo(clipId, true, kProjectBpm);

    const auto* event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->autoTempo);
    REQUIRE(event->interpBpm == Approx(kProjectBpm));
}

// The #1157 contract, scoped to a region the user drew by hand.
TEST_CASE("Correcting the tempo on an explicit region moves its beat view, not its samples",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropDetectedLoop(file.path());
    auto* event = clips.getClip(clipId)->primaryEvent();
    event->setLoopStartSeconds(0.5);
    event->setLoopLengthSeconds(2.0);
    const auto startSamples = event->loopStartSamples;
    const auto lengthSamples = event->loopLengthSamples;

    ClipManager::AudioClipBeatsUpdate corrected;
    corrected.interpretationBpm = 174.0;
    clips.applyAudioClipBeats(clipId, corrected, kProjectBpm);

    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->interpBpm == Approx(174.0));
    REQUIRE(event->interpTotalBeats == Approx(kFileBeats));
    REQUIRE(event->loopStartSamples == startSamples);
    REQUIRE(event->loopLengthSamples == lengthSamples);
    REQUIRE(event->loopLengthSeconds() == Approx(2.0));
    REQUIRE(event->loopLengthBeats() == Approx(2.0 * 174.0 / 60.0));
}

// Fails today: the region stays the file's 5.486 s, which is 15.9 beats at 174,
// so the slot wraps early. Passes after phase 3.
TEST_CASE("Correcting the tempo on a whole-file loop keeps its beat count and follows it",
          "[clip][tempo][sequence][!mayfail]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropDetectedLoop(file.path());

    ClipManager::AudioClipBeatsUpdate corrected;
    corrected.interpretationBpm = 174.0;
    clips.applyAudioClipBeats(clipId, corrected, kProjectBpm);

    const auto* clip = clips.getClip(clipId);
    const auto* event = clip->primaryEvent();
    REQUIRE(event->interpBpm == Approx(174.0));
    REQUIRE(event->interpTotalBeats == Approx(kFileBeats));

    // A whole-file loop is its beat count; the region re-reads at the new tempo.
    REQUIRE(event->loopLengthSeconds() == Approx(kFileBeats * 60.0 / 174.0).margin(0.001));
    REQUIRE(clip->sessionCycleBeats() == Approx(kFileBeats).margin(0.01));
}

TEST_CASE("A loop's interpretation, region and cycle survive save and reload",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropDetectedLoop(file.path());
    const auto* saved = clips.getClip(clipId);
    const auto* savedEvent = saved->primaryEvent();

    const auto json = ProjectSerializer::serializeClipInfo(*saved);
    ClipInfo loaded;
    REQUIRE(ProjectSerializer::deserializeClipInfo(json, loaded, kProjectBpm));
    const auto* loadedEvent = loaded.primaryEvent();
    REQUIRE(loadedEvent != nullptr);

    REQUIRE(loadedEvent->interpBpm == Approx(savedEvent->interpBpm));
    REQUIRE(loadedEvent->interpTotalBeats == Approx(savedEvent->interpTotalBeats));
    REQUIRE(loadedEvent->autoTempo == savedEvent->autoTempo);
    REQUIRE(loaded.loopEnabled == saved->loopEnabled);
    REQUIRE(loadedEvent->loopStartSamples == savedEvent->loopStartSamples);
    REQUIRE(loadedEvent->loopLengthSamples == savedEvent->loopLengthSamples);
    REQUIRE(loaded.sessionCycleBeats() == Approx(saved->sessionCycleBeats()));
}

TEST_CASE("A tempo edit made through a command is undone as one step", "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    auto& undo = UndoManager::getInstance();
    LoopFile file;

    const auto clipId = dropDetectedLoop(file.path());
    const auto* event = clips.getClip(clipId)->primaryEvent();
    const auto startSamples = event->loopStartSamples;
    const auto lengthSamples = event->loopLengthSamples;

    undo.executeCommand(std::make_unique<SetClipPropertyCommand>(
        clipId, "Set source BPM", [](ClipManager& manager, ClipId id) {
            ClipManager::AudioClipBeatsUpdate corrected;
            corrected.interpretationBpm = 174.0;
            manager.applyAudioClipBeats(id, corrected, kProjectBpm);
        }));
    REQUIRE(clips.getClip(clipId)->primaryEvent()->interpBpm == Approx(174.0));

    REQUIRE(undo.undo());
    REQUIRE(!undo.canUndo());

    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->interpBpm == Approx(kFileBpm));
    REQUIRE(event->loopStartSamples == startSamples);
    REQUIRE(event->loopLengthSamples == lengthSamples);
}
