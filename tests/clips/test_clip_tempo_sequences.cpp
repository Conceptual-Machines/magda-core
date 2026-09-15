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
constexpr double kFileBeats = 16.0;

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

// A drop chooses no range in either view. Pressing loop on an arrangement
// clip does: it loops what the clip shows at that moment.
TEST_CASE("A dropped loop's region is the whole source until a tempo lands",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropSessionClip(file.path());
    const auto* event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(clips.getClip(clipId)->loopEnabled);
    REQUIRE(event->loopExtent == RegionExtent::WholeSource);
    REQUIRE(event->loopLengthSamples == 0);
    REQUIRE(event->playbackIntent == PlaybackIntent::BeatWhenKnown);

    const auto arrangement =
        clips.createAudioClipBeats(1, 0.0, 4.0, file.path(), ClipView::Arrangement, kProjectBpm);
    const auto* arrangementEvent = clips.getClip(arrangement)->primaryEvent();
    REQUIRE(arrangementEvent->loopExtent == RegionExtent::WholeSource);
    REQUIRE(arrangementEvent->sourceLengthSeconds(2.0) == Approx(2.0));

    clips.setClipLoopEnabled(arrangement, true, kProjectBpm);
    REQUIRE(arrangementEvent->loopExtent == RegionExtent::Explicit);
    REQUIRE(arrangementEvent->loopLengthSeconds() == Approx(2.0));
}

// The file is 5.516 s: 15.996 beats at 174, a few samples short of the 16 it
// was exported as. The derived count is the whole beat, so the cycle is exact.
TEST_CASE("A tempo typed on a loop with no beat count derives a whole count from the file",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    juce::TemporaryFile temp(".wav");
    temp.getFile().replaceWithText("not audio");
    SourcePool::getInstance().seedFactsForTesting(temp.getFile().getFullPathName(), 5.516,
                                                  kFileRate);

    const auto clipId = clips.createAudioClipBeats(1, 0.0, 5.516 * kProjectBpm / 60.0,
                                                   temp.getFile().getFullPathName(),
                                                   ClipView::Session, kProjectBpm);
    clips.setAutoTempo(clipId, true, kProjectBpm);  // no tempo yet: intent only
    REQUIRE(!clips.getClip(clipId)->primaryEvent()->autoTempo);

    ClipManager::AudioClipBeatsUpdate typed;
    typed.interpretationBpm = 174.0;
    clips.applyAudioClipBeats(clipId, typed, kProjectBpm);

    const auto* clip = clips.getClip(clipId);
    const auto* event = clip->primaryEvent();
    REQUIRE(event->autoTempo);
    REQUIRE(event->interpTotalBeats == Approx(16.0));
    // The count was read off the file, whoever typed the tempo.
    REQUIRE(event->beatsFrom == Provenance::Analysis);
    REQUIRE(event->loopLengthSeconds() == Approx(16.0 * 60.0 / 174.0).margin(0.001));
    REQUIRE(clip->sessionCycleBeats() == Approx(16.0));

    // A file that is not a whole number of beats keeps its fraction.
    SourcePool::getInstance().clear();
    SourcePool::getInstance().clearSeededFactsForTesting();
    clips.clearAllClips();
    SourcePool::getInstance().seedFactsForTesting(temp.getFile().getFullPathName(), 3.3, kFileRate);
    const auto take = clips.createAudioClipBeats(1, 0.0, 6.6, temp.getFile().getFullPathName(),
                                                 ClipView::Arrangement, kProjectBpm);
    clips.applyAudioClipBeats(take, typed, kProjectBpm);
    REQUIRE(clips.getClip(take)->primaryEvent()->interpTotalBeats == Approx(3.3 * 174.0 / 60.0));
}

// A user-owned tempo is never replaced by the cache, even when it happens to
// equal the project tempo.
TEST_CASE("A tempo the user typed survives the BEAT toggle", "[clip][tempo][sequence]") {
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
    REQUIRE(event->loopExtent == RegionExtent::Explicit);
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

// A region sized by the interpretation is its beat count; a tempo correction
// refits it rather than leaving 15.9 beats at 174 to wrap early.
TEST_CASE("Correcting the tempo on a whole-file loop keeps its beat count and follows it",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropDetectedLoop(file.path());
    REQUIRE(clips.getClip(clipId)->primaryEvent()->loopExtent == RegionExtent::Interpretation);

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

// The paste goes through adoption so the copy keeps the user's ownership,
// and a slot loops, so the whole-source region becomes the beat count.
TEST_CASE("Pasting an arrangement clip into a slot keeps the user's interpretation and follows it",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto arrangement =
        clips.createAudioClipBeats(1, 0.0, 4.0, file.path(), ClipView::Arrangement, kProjectBpm);
    ClipManager::AudioClipBeatsUpdate typed;
    typed.interpretationBpm = kFileBpm;
    typed.interpretationTotalBeats = kFileBeats;
    clips.applyAudioClipBeats(arrangement, typed, kProjectBpm);
    const auto* srcEvent = clips.getClip(arrangement)->primaryEvent();
    REQUIRE(srcEvent->bpmFrom == Provenance::User);
    REQUIRE(srcEvent->beatsFrom == Provenance::User);
    REQUIRE(srcEvent->loopExtent == RegionExtent::WholeSource);

    clips.copyToClipboard({arrangement});
    const auto pasted = clips.pasteFromClipboardBeats(0.0, 1, ClipView::Session, 0);
    REQUIRE(pasted.size() == 1);

    const auto* clip = clips.getClip(pasted.front());
    const auto* event = clip->primaryEvent();
    REQUIRE(clip->loopEnabled);
    REQUIRE(event->interpBpm == Approx(kFileBpm));
    REQUIRE(event->bpmFrom == Provenance::User);
    REQUIRE(event->interpTotalBeats == Approx(kFileBeats));
    REQUIRE(event->beatsFrom == Provenance::User);
    REQUIRE(event->autoTempo);
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
    REQUIRE(event->loopLengthSeconds() == Approx(kFileBeats * 60.0 / kFileBpm).margin(0.001));
}

// 16 beats at 174 outrun the 5.486 s file by a few samples' rounding. A region
// that follows the interpretation keeps that length; only an explicit range
// is shortened to the file.
TEST_CASE("Sanitizing a loop that follows its interpretation does not shorten it to the file",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropSessionClip(file.path());
    ClipManager::AudioClipBeatsUpdate typed;
    typed.interpretationBpm = 174.0;
    typed.interpretationTotalBeats = 16.0;
    clips.applyAudioClipBeats(clipId, typed, kProjectBpm);

    const auto* event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
    REQUIRE(event->loopLengthSeconds() > kFileSeconds);
    const auto lengthSamples = event->loopLengthSamples;

    clips.setLoopPhase(clipId, 0.0);

    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->loopLengthSamples == lengthSamples);
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
}

TEST_CASE("Restoring a loop length puts back its samples and its extent",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropDetectedLoop(file.path());
    const auto* event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
    const auto lengthSamples = event->loopLengthSamples;

    clips.setLoopLength(clipId, 2.0, kProjectBpm);
    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->loopExtent == RegionExtent::Explicit);
    REQUIRE(event->loopLengthSeconds() == Approx(2.0));

    clips.restoreLoopLength(clipId, lengthSamples, RegionExtent::Interpretation, kProjectBpm);
    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->loopLengthSamples == lengthSamples);
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
}
