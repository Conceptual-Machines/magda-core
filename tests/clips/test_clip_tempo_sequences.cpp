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

/// A session clip whose tempo was cached before the drop, then picked up by
/// pressing BEAT: 175 / 16 beats, beat mode on, region covering the whole file.
ClipId dropAndPressBeat(const juce::String& path) {
    auto& clips = ClipManager::getInstance();
    AudioThumbnailManager::getInstance().cacheBPM(path, kFileBpm);
    const auto clipId = dropSessionClip(path);

    const auto* dropped = clips.getClip(clipId)->primaryEvent();
    REQUIRE(dropped != nullptr);
    REQUIRE_FALSE(dropped->hasInterpretedBpm());
    REQUIRE(dropped->loopExtent == RegionExtent::WholeSource);
    REQUIRE_FALSE(dropped->autoTempo);
    REQUIRE(dropped->playbackIntent == PlaybackIntent::Free);

    clips.detectMissingTempo({clipId}, kProjectBpm, nullptr);
    clips.setAutoTempo(clipId, true, kProjectBpm);

    const auto* event = clips.getClip(clipId)->primaryEvent();
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
    REQUIRE(event->playbackIntent == PlaybackIntent::Free);

    const auto arrangement =
        clips.createAudioClipBeats(1, 0.0, 4.0, file.path(), ClipView::Arrangement, kProjectBpm);
    const auto* arrangementEvent = clips.getClip(arrangement)->primaryEvent();
    REQUIRE(arrangementEvent->loopExtent == RegionExtent::WholeSource);
    REQUIRE(arrangementEvent->sourceLengthSeconds(2.0) == Approx(2.0));

    clips.setClipLoopEnabled(arrangement, true, kProjectBpm);
    REQUIRE(arrangementEvent->loopExtent == RegionExtent::Explicit);
    REQUIRE(arrangementEvent->loopLengthSeconds() == Approx(2.0));
}

// Typing 116 on a 32-beat file and then halving the beat count reinterprets
// the file as 16 beats long; the loop region is still the whole file.
TEST_CASE("Changing the beat count at a typed tempo keeps the loop region",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    constexpr double kSeconds = 32.0 * 60.0 / 116.0;
    juce::TemporaryFile temp(".wav");
    temp.getFile().replaceWithText("not audio");
    SourcePool::getInstance().seedFactsForTesting(temp.getFile().getFullPathName(), kSeconds,
                                                  kFileRate);

    const auto clipId = clips.createAudioClipBeats(1, 0.0, kSeconds * kProjectBpm / 60.0,
                                                   temp.getFile().getFullPathName(),
                                                   ClipView::Session, kProjectBpm);
    clips.setAutoTempo(clipId, true, kProjectBpm);

    clips.setSourceTempo(clipId, 116.0);

    const auto* clip = clips.getClip(clipId);
    const auto* event = clip->primaryEvent();
    REQUIRE(event->interpTotalBeats == Approx(32.0));
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
    const auto wholeFile = event->loopLengthSamples;
    REQUIRE(event->loopLengthSeconds() == Approx(kSeconds).margin(0.001));

    clips.setSourceBeatCount(clipId, 16.0);

    REQUIRE(event->interpTotalBeats == Approx(16.0));
    REQUIRE(event->interpBpm == Approx(58.0));
    REQUIRE(event->bpmFrom == Provenance::User);
    REQUIRE(event->loopLengthSamples == wholeFile);
    REQUIRE(clip->sessionCycleBeats(kProjectBpm) == Approx(16.0));
}

// A free-playing slot plays its region at its own rate, so its pass is the
// region's seconds at the project tempo, not the source beats.
TEST_CASE("A free-playing slot's pass is its region at the project tempo",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropSessionClip(file.path());
    const auto* clip = clips.getClip(clipId);
    REQUIRE(!clip->primaryEvent()->autoTempo);
    REQUIRE(clip->sessionCycleBeats(120.0) == Approx(kFileSeconds * 2.0).margin(0.01));
    REQUIRE(clip->sessionCycleBeats(60.0) == Approx(kFileSeconds).margin(0.01));

    // Sped up, the same region passes in half the time.
    clips.setSpeedRatio(clipId, 2.0);
    REQUIRE(clip->sessionCycleBeats(120.0) == Approx(kFileSeconds).margin(0.01));
    clips.setSpeedRatio(clipId, 1.0);

    // In beat mode the region's source beats are the pass whatever the project plays at.
    clips.setSourceTempo(clipId, kFileBpm);
    clips.setAutoTempo(clipId, true, kProjectBpm);
    REQUIRE(clip->primaryEvent()->autoTempo);
    REQUIRE(clip->sessionCycleBeats(60.0) == Approx(kFileBeats));
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

    clips.setSourceTempo(clipId, 174.0);

    const auto* clip = clips.getClip(clipId);
    const auto* event = clip->primaryEvent();
    REQUIRE(event->autoTempo);
    REQUIRE(event->interpTotalBeats == Approx(16.0));
    // The count is the typed tempo in other units, so it is the user's too.
    REQUIRE(event->beatsFrom == Provenance::User);
    REQUIRE(event->loopLengthSeconds() == Approx(16.0 * 60.0 / 174.0).margin(0.001));
    REQUIRE(clip->sessionCycleBeats(kProjectBpm) == Approx(16.0));

    // A file that is not a whole number of beats keeps its fraction.
    SourcePool::getInstance().clear();
    SourcePool::getInstance().clearSeededFactsForTesting();
    clips.clearAllClips();
    SourcePool::getInstance().seedFactsForTesting(temp.getFile().getFullPathName(), 3.3, kFileRate);
    const auto take = clips.createAudioClipBeats(1, 0.0, 6.6, temp.getFile().getFullPathName(),
                                                 ClipView::Arrangement, kProjectBpm);
    clips.setSourceTempo(take, 174.0);
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

    clips.setSourceTempo(clipId, kProjectBpm);

    // Pressing BEAT asks for a detection; the user's tempo already owns the
    // clip, so it is refused.
    AudioThumbnailManager::getInstance().cacheBPM(file.path(), kFileBpm);
    clips.detectMissingTempo({clipId}, kProjectBpm, nullptr);
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

    const auto clipId = dropAndPressBeat(file.path());
    auto* event = clips.getClip(clipId)->primaryEvent();
    event->setLoopStartSeconds(0.5);
    event->setLoopLengthSeconds(2.0);
    REQUIRE(event->loopExtent == RegionExtent::Explicit);
    const auto startSamples = event->loopStartSamples;
    const auto lengthSamples = event->loopLengthSamples;

    clips.setSourceTempo(clipId, 174.0);

    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->interpBpm == Approx(174.0));
    // The file is 5.486 s: at 174 it holds 15.91 beats, not the 16 it held at 175.
    REQUIRE(event->interpTotalBeats == Approx(kFileSeconds * 174.0 / 60.0));
    REQUIRE(event->loopStartSamples == startSamples);
    REQUIRE(event->loopLengthSamples == lengthSamples);
    REQUIRE(event->loopLengthSeconds() == Approx(2.0));
    REQUIRE(event->loopLengthBeats() == Approx(2.0 * 174.0 / 60.0));
}

// A tempo correction restates the beat count from the file length, so a
// region sized by the interpretation is still the whole file afterwards.
TEST_CASE("Correcting the tempo on a whole-file loop restates its beat count and keeps the file",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropAndPressBeat(file.path());
    REQUIRE(clips.getClip(clipId)->primaryEvent()->loopExtent == RegionExtent::Interpretation);

    clips.setSourceTempo(clipId, 174.0);

    const auto* clip = clips.getClip(clipId);
    const auto* event = clip->primaryEvent();
    REQUIRE(event->interpBpm == Approx(174.0));
    const double beatsAt174 = kFileSeconds * 174.0 / 60.0;  // 15.91, not a loop at 174
    REQUIRE(event->interpTotalBeats == Approx(beatsAt174));
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
    REQUIRE(event->loopLengthSeconds() == Approx(kFileSeconds).margin(0.001));
    REQUIRE(clip->sessionCycleBeats(kProjectBpm) == Approx(beatsAt174).margin(0.01));
}

TEST_CASE("A loop's interpretation, region and cycle survive save and reload",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropAndPressBeat(file.path());
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
    REQUIRE(loaded.sessionCycleBeats(kProjectBpm) == Approx(saved->sessionCycleBeats(kProjectBpm)));
}

TEST_CASE("A tempo edit made through a command is undone as one step", "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    auto& undo = UndoManager::getInstance();
    LoopFile file;

    const auto clipId = dropAndPressBeat(file.path());
    const auto* event = clips.getClip(clipId)->primaryEvent();
    const auto startSamples = event->loopStartSamples;
    const auto lengthSamples = event->loopLengthSamples;

    undo.executeCommand(std::make_unique<SetClipPropertyCommand>(
        clipId, "Set source BPM",
        [](ClipManager& manager, ClipId id) { manager.setSourceTempo(id, 174.0); }));
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
    clips.setSourceTempo(arrangement, kFileBpm);
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
    REQUIRE(!event->autoTempo);  // the source was in time mode, so is the copy
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
    REQUIRE(event->loopLengthSeconds() == Approx(kFileBeats * 60.0 / kFileBpm).margin(0.001));
}

// setSourceTempo ties tempo and beat count to the file length, so an
// interpretation-sized region can no longer be made to outrun the file the
// way an inconsistent bpm/beats pair once did; what stays to verify is that
// sanitizing (setLoopPhase) leaves such a region's samples and extent alone.
TEST_CASE("Sanitizing a loop that follows its interpretation does not shorten it to the file",
          "[clip][tempo][sequence]") {
    TempoSequenceFixture fixture;
    auto& clips = ClipManager::getInstance();
    LoopFile file;

    const auto clipId = dropSessionClip(file.path());
    clips.setSourceTempo(clipId, 174.0);

    const auto* event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->loopExtent == RegionExtent::Interpretation);
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

    const auto clipId = dropAndPressBeat(file.path());
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
