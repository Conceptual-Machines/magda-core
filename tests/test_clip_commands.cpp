#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <unordered_set>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipCommands.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOcclusion.hpp"
#include "magda/daw/core/ClipPropertyCommands.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/TempoMap.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

/**
 * Tests for ClipCommand undo/redo operations
 *
 * Covers: DuplicateClipCommand, JoinClipsCommand, DeleteClipCommand,
 *         MoveClipCommand, MoveClipToTrackCommand, ResizeClipCommand,
 *         CreateClipCommand, PasteClipCommand
 *
 * At 120 BPM: 1 second = 2 beats.
 */

using namespace magda;

// ============================================================================
// Helper: reset state before each test
// ============================================================================
static void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
}

static TrackId createTrack(const char* name = "Track", TrackType type = TrackType::Audio) {
    return TrackManager::getInstance().createTrack(name, type);
}

static ClipId createMidi(TrackId trackId, double start, double length,
                         const std::vector<double>& noteBeatPositions = {}) {
    auto& cm = ClipManager::getInstance();
    ClipId id = cm.createMidiClip(trackId, start, length, ClipView::Arrangement);
    if (!noteBeatPositions.empty()) {
        auto* clip = cm.getClip(id);
        for (double beat : noteBeatPositions) {
            MidiNote note;
            note.noteNumber = 60;
            note.startBeat = beat;
            note.lengthBeats = 1.0;
            note.velocity = 100;
            clip->midiNotes.push_back(note);
        }
    }
    return id;
}

static ClipId createAudio(TrackId trackId, double start, double length) {
    return ClipManager::getInstance().createAudioClip(trackId, start, length, "test.wav",
                                                      ClipView::Arrangement);
}

static BeatPosition secondsToBeatPosition(double seconds, double bpm = 120.0) {
    return BeatPosition{seconds * bpm / 60.0};
}

static BeatDuration secondsToBeatDuration(double seconds, double bpm = 120.0) {
    return BeatDuration{seconds * bpm / 60.0};
}

class TestTempoMap final : public TempoMap {
  public:
    double beatToTime(double beat) const override {
        return 3.0 + beat * 1.5;
    }

    double timeToBeat(double seconds) const override {
        return (seconds - 3.0) / 1.5;
    }

    double bpmAt(double beat) const override {
        juce::ignoreUnused(beat);
        return 40.0;
    }
};

static ClipInfo makeBounceClip(double startBeats, double lengthBeats) {
    ClipInfo clip;
    clip.view = ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(startBeats, lengthBeats);
    return clip;
}

struct RecordingClipListener : ClipManagerListener {
    int clipsChangedCount = 0;
    std::vector<ClipId> propertyChangedClipIds;

    void clipsChanged() override {
        ++clipsChangedCount;
    }

    void clipPropertyChanged(ClipId clipId) override {
        propertyChangedClipIds.push_back(clipId);
    }

    bool sawPropertyChangeFor(ClipId clipId) const {
        return std::find(propertyChangedClipIds.begin(), propertyChangedClipIds.end(), clipId) !=
               propertyChangedClipIds.end();
    }
};

// ============================================================================
// DuplicateClipCommand
// ============================================================================

TEST_CASE("DuplicateClipCommand - basic duplicate", "[clip][command][duplicate]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 2.0, {0.0, 1.0, 2.0});

    SECTION("Duplicate places copy after source") {
        DuplicateClipCommand cmd(original);
        REQUIRE(cmd.canExecute());
        cmd.execute();

        ClipId dupId = cmd.getDuplicatedClipId();
        REQUIRE(dupId != INVALID_CLIP_ID);

        auto& cm = ClipManager::getInstance();
        auto* orig = cm.getClip(original);
        auto* dup = cm.getClip(dupId);

        REQUIRE(orig != nullptr);
        REQUIRE(dup != nullptr);

        // Duplicate starts after original
        REQUIRE(dup->startTime == Catch::Approx(orig->startTime + orig->length));
        REQUIRE(dup->length == Catch::Approx(orig->length));
        REQUIRE(dup->trackId == orig->trackId);
        REQUIRE(dup->getType() == orig->getType());

        // MIDI notes copied
        REQUIRE(dup->midiNotes.size() == orig->midiNotes.size());
        for (size_t i = 0; i < orig->midiNotes.size(); ++i) {
            REQUIRE(dup->midiNotes[i].startBeat == Catch::Approx(orig->midiNotes[i].startBeat));
        }
    }

    SECTION("Duplicate at specific position and track") {
        TrackId track2 = createTrack("Track 2");
        DuplicateClipCommand cmd(original, secondsToBeatPosition(5.0), track2);
        cmd.execute();

        auto* dup = ClipManager::getInstance().getClip(cmd.getDuplicatedClipId());
        REQUIRE(dup != nullptr);
        REQUIRE(dup->startTime == Catch::Approx(5.0));
        REQUIRE(dup->trackId == track2);
    }

    SECTION("Cannot duplicate invalid clip") {
        DuplicateClipCommand cmd(9999);
        REQUIRE_FALSE(cmd.canExecute());
    }
}

TEST_CASE("DuplicateClipCommand - undo/redo", "[clip][command][duplicate][undo]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 2.0, {0.0, 2.0});

    DuplicateClipCommand cmd(original);
    cmd.execute();
    ClipId dupId = cmd.getDuplicatedClipId();
    REQUIRE(ClipManager::getInstance().getClip(dupId) != nullptr);

    // Undo removes duplicate
    cmd.undo();
    REQUIRE(ClipManager::getInstance().getClip(dupId) == nullptr);

    // Original untouched
    auto* orig = ClipManager::getInstance().getClip(original);
    REQUIRE(orig != nullptr);
    REQUIRE(orig->length == Catch::Approx(2.0));
    REQUIRE(orig->midiNotes.size() == 2);

    // Redo recreates it
    cmd.execute();
    // Note: redo may create a new clip ID
    // Just verify original still exists and a duplicate was created
    REQUIRE(ClipManager::getInstance().getClip(original) != nullptr);
}

TEST_CASE("DuplicateClipCommand - session undo removes duplicate only",
          "[clip][command][duplicate][session][undo]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId original = cm.createMidiClip(track, 0.0, 4.0, ClipView::Session);
    cm.setClipSceneIndex(original, 0);

    auto cmd = DuplicateClipCommand::forSessionSlot(original, INVALID_TRACK_ID, 2);
    cmd->execute();

    ClipId duplicateId = cmd->getDuplicatedClipId();
    REQUIRE(duplicateId != INVALID_CLIP_ID);
    REQUIRE(cm.getClipInSlot(track, 0) == original);
    REQUIRE(cm.getClipInSlot(track, 2) == duplicateId);

    cmd->undo();

    REQUIRE(cm.getClip(duplicateId) == nullptr);
    REQUIRE(cm.getClipInSlot(track, 0) == original);
    REQUIRE(cm.getClipInSlot(track, 2) == INVALID_CLIP_ID);

    cmd->execute();
    ClipId redoDuplicateId = cmd->getDuplicatedClipId();
    REQUIRE(redoDuplicateId != INVALID_CLIP_ID);
    REQUIRE(cm.getClipInSlot(track, 0) == original);
    REQUIRE(cm.getClipInSlot(track, 2) == redoDuplicateId);
}

TEST_CASE("DuplicateClipCommand - audio clip", "[clip][command][duplicate]") {
    resetState();
    TrackId track = createTrack("Audio Track", TrackType::Audio);
    ClipId original = createAudio(track, 1.0, 3.0);

    DuplicateClipCommand cmd(original);
    cmd.execute();

    auto* dup = ClipManager::getInstance().getClip(cmd.getDuplicatedClipId());
    REQUIRE(dup != nullptr);
    REQUIRE(dup->isAudio());
    REQUIRE(dup->startTime == Catch::Approx(4.0));  // 1.0 + 3.0
    REQUIRE(dup->length == Catch::Approx(3.0));
}

// Regression: ClipManager::duplicateClip used to update startTime on audio
// clips but leave startBeats equal to the original's, so any later
// beats-driven re-derivation (BPM change, beats-aware paint) snapped the
// duplicate back on top of the original. Position is beats-authoritative
// for every clip type — startBeats must reflect the new startTime.
TEST_CASE("DuplicateClipCommand - audio clip startBeats stays in sync with startTime",
          "[clip][command][duplicate][bpm-snap-regression]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(90.0);
    REQUIRE(proj.getCurrentProjectInfo().tempo == Catch::Approx(90.0));

    TrackId track = createTrack("Audio Track", TrackType::Audio);
    ClipId original = createAudio(track, 2.0, 4.0);

    auto* origBeforeDup = ClipManager::getInstance().getClip(original);
    REQUIRE(origBeforeDup != nullptr);
    // Sanity: createAudio under bpm=90 should give startBeats = 2 * 90/60 = 3.0.
    REQUIRE(origBeforeDup->startBeats == Catch::Approx(3.0));

    DuplicateClipCommand cmd(original);
    cmd.execute();

    auto* dup = ClipManager::getInstance().getClip(cmd.getDuplicatedClipId());
    REQUIRE(dup != nullptr);
    // Position math in beats: original at 2.0s @ 90 BPM = beat 3, length 4.0s = 6 beats.
    // Duplicate should sit at beat 9, derived seconds 6.0.
    REQUIRE(dup->startBeats == Catch::Approx(9.0));
    REQUIRE(dup->startTime == Catch::Approx(6.0));
    // Pre-fix dup->startBeats was the original's (3.0) — would have snapped
    // back to startTime 2.0 the moment anything re-derived from beats.
    auto* origAfter = ClipManager::getInstance().getClip(original);
    REQUIRE(origAfter != nullptr);
    REQUIRE(dup->startBeats != Catch::Approx(origAfter->startBeats));

    proj.setTempo(originalTempo);
}

TEST_CASE("Arrangement multi-clip duplicate preserves selection spacing",
          "[clip][command][duplicate][multi]") {
    resetState();
    TrackId track = createTrack();
    ClipId left = createMidi(track, 0.0, 1.0);
    ClipId right = createMidi(track, 1.0, 1.0);

    auto commands = createArrangementBlockDuplicateCommands({left, right}, 120.0);
    REQUIRE(commands.size() == 2);

    std::vector<ClipId> duplicates;
    for (auto& command : commands) {
        auto* commandPtr = command.get();
        command->execute();
        duplicates.push_back(commandPtr->getDuplicatedClipId());
    }

    REQUIRE(duplicates.size() == 2);
    auto* dupLeft = ClipManager::getInstance().getClip(duplicates[0]);
    auto* dupRight = ClipManager::getInstance().getClip(duplicates[1]);
    REQUIRE(dupLeft != nullptr);
    REQUIRE(dupRight != nullptr);

    REQUIRE(dupLeft->startTime == Catch::Approx(2.0));
    REQUIRE(dupRight->startTime == Catch::Approx(3.0));
    REQUIRE(dupLeft->trackId == track);
    REQUIRE(dupRight->trackId == track);
}

// Regression: time-range duplicate (Cmd+D over an active time selection)
// must preserve the relative spacing of clips inside the range, regardless
// of project tempo. Each pasted clip's newStartTime = clipData.startTime +
// (pasteTime - referenceTime), so the gap between A and B copies should
// equal the gap between A and B in the source range.
TEST_CASE("copyTimeRangeToClipboard + paste - preserves internal clip spacing",
          "[clip][duplicate][time-selection][bpm-snap-regression]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(90.0);

    TrackId track = createTrack("Audio Track", TrackType::Audio);
    // Selection [2.0, 6.0] (4 seconds = 6 beats at 90 BPM).
    // Two audio clips inside the range, 0.5s of internal gap.
    ClipId a = createAudio(track, 2.0, 1.5);  // ends at 3.5
    ClipId b = createAudio(track, 4.0, 1.5);  // starts 0.5s after A's end

    auto& cm = ClipManager::getInstance();
    cm.copyTimeRangeToClipboard(2.0, 6.0, {track}, /*tempoBPM=*/90.0);

    PasteClipCommand paste(secondsToBeatPosition(6.0, 90.0));
    paste.execute();

    auto pastedIds = paste.getPastedClipIds();
    REQUIRE(pastedIds.size() == 2);

    // Sort pasted by startTime to align with originals.
    std::sort(pastedIds.begin(), pastedIds.end(), [&](ClipId x, ClipId y) {
        return cm.getClip(x)->startTime < cm.getClip(y)->startTime;
    });
    auto* pa = cm.getClip(pastedIds[0]);
    auto* pb = cm.getClip(pastedIds[1]);
    REQUIRE(pa != nullptr);
    REQUIRE(pb != nullptr);

    // A copy: 2.0 + (6 - 2) = 6.0. B copy: 4.0 + 4 = 8.0. Gap preserved.
    REQUIRE(pa->startTime == Catch::Approx(6.0));
    REQUIRE(pb->startTime == Catch::Approx(8.0));
    REQUIRE((pb->startTime - pa->startTime) ==
            Catch::Approx(cm.getClip(b)->startTime - cm.getClip(a)->startTime));

    proj.setTempo(originalTempo);
    juce::ignoreUnused(a, b);
}

TEST_CASE("copyTimeRangeToClipboard + paste - trimmed audio keeps beat placement in sync",
          "[clip][duplicate][time-selection][ui-placement]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(90.0);

    TrackId track = createTrack("Audio Track", TrackType::Audio);
    ClipId sourceId = createAudio(track, 1.0, 8.0);

    auto& cm = ClipManager::getInstance();
    auto* source = cm.getClip(sourceId);
    REQUIRE(source != nullptr);
    REQUIRE(source->startBeats == Catch::Approx(1.5));
    REQUIRE(source->lengthBeats == Catch::Approx(12.0));

    // Selection cuts a 4s slice out of the middle. The clipboard entry must
    // carry that 4s / 6-beat placement, not the original 8s / 12-beat width.
    cm.copyTimeRangeToClipboard(2.0, 6.0, {track}, /*tempoBPM=*/90.0);

    PasteClipCommand paste(secondsToBeatPosition(6.0, 90.0));
    paste.execute();

    auto pastedIds = paste.getPastedClipIds();
    REQUIRE(pastedIds.size() == 1);

    auto* pasted = cm.getClip(pastedIds.front());
    REQUIRE(pasted != nullptr);
    REQUIRE(pasted->startTime == Catch::Approx(6.0));
    REQUIRE(pasted->length == Catch::Approx(4.0));
    REQUIRE(pasted->startBeats == Catch::Approx(9.0));
    REQUIRE(pasted->lengthBeats == Catch::Approx(6.0));
    REQUIRE(pasted->placement.startBeat == Catch::Approx(9.0));
    REQUIRE(pasted->placement.lengthBeats == Catch::Approx(6.0));

    proj.setTempo(originalTempo);
}

TEST_CASE(
    "copyTimeRangeToClipboard + paste - trims from beat placement when seconds cache is stale",
    "[clip][duplicate][time-selection][ui-placement][beat-cache]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(120.0);

    TrackId track = createTrack("Audio Track", TrackType::Audio);
    ClipId sourceId = createAudio(track, 1.0, 2.0);

    auto& cm = ClipManager::getInstance();
    auto* source = cm.getClip(sourceId);
    REQUIRE(source != nullptr);
    primaryEventOf(source)->autoTempo = true;
    source->loopEnabled = true;
    primaryEventOf(source)->interpBpm = 172.0;
    primaryEventOf(source)->interpTotalBeats = 16.0;
    magda::SourcePool::getInstance().seedFactsForTesting(primaryEventOf(source)->sourceFilePath(),
                                                         16.0 * 60.0 / 172.0,
                                                         magda::test::kTestSourceSampleRate);
    magda::SourcePool::getInstance().resolveFacts(primaryEventOf(source)->sourceId);
    primaryEventOf(source)->setLoopStartBeats(0.0);
    primaryEventOf(source)->setLoopLengthBeats(16.0);
    primaryEventOf(source)->setAnchorBeats(0.0);
    source->setPlacementBeats(2.0, 4.0);  // actual timeline: 1s..3s at 120 BPM
    source->startTime = 99.0;             // stale transitional cache
    source->length = 99.0;

    cm.copyTimeRangeToClipboard(1.5, 2.0, {track}, /*tempoBPM=*/120.0);

    PasteClipCommand paste(secondsToBeatPosition(3.0, 120.0));
    paste.execute();

    auto pastedIds = paste.getPastedClipIds();
    REQUIRE(pastedIds.size() == 1);

    auto* pasted = cm.getClip(pastedIds.front());
    REQUIRE(pasted != nullptr);
    REQUIRE(pasted->startTime == Catch::Approx(3.0));
    REQUIRE(pasted->length == Catch::Approx(0.5));
    REQUIRE(pasted->placement.startBeat == Catch::Approx(6.0));
    REQUIRE(pasted->placement.lengthBeats == Catch::Approx(1.0));
    REQUIRE(primaryEventOf(pasted)->anchorBeats() == Catch::Approx(1.0));

    proj.setTempo(originalTempo);
}

TEST_CASE("copyTimeRangeToClipboard + paste - exact beat slice keeps waveform identity",
          "[clip][duplicate][time-selection][ui-placement][waveform]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(120.0);

    TrackId track = createTrack("Audio Track", TrackType::Audio);
    ClipId sourceId = createAudio(track, 0.5, 0.5);

    auto& cm = ClipManager::getInstance();
    auto* source = cm.getClip(sourceId);
    REQUIRE(source != nullptr);
    primaryEventOf(source)->autoTempo = true;
    source->loopEnabled = true;
    primaryEventOf(source)->interpBpm = 172.0;
    primaryEventOf(source)->interpTotalBeats = 16.0;
    magda::SourcePool::getInstance().seedFactsForTesting(primaryEventOf(source)->sourceFilePath(),
                                                         16.0 * 60.0 / 172.0,
                                                         magda::test::kTestSourceSampleRate);
    magda::SourcePool::getInstance().resolveFacts(primaryEventOf(source)->sourceId);
    primaryEventOf(source)->setLoopStartBeats(0.0);
    primaryEventOf(source)->setLoopLengthBeats(16.0);
    primaryEventOf(source)->setAnchorBeats(3.0);
    primaryEventOf(source)->setAnchorSeconds(primaryEventOf(source)->anchorBeats() * 60.0 /
                                             primaryEventOf(source)->interpBpm);
    primaryEventOf(source)->setLoopStartSeconds(0.0);
    primaryEventOf(source)->setLoopLengthSeconds(primaryEventOf(source)->sourceDurationSeconds());
    source->setPlacementBeats(1.0, 1.0);
    source->deriveTimesFromBeats(120.0);

    cm.copyTimeRangeToClipboard(source->getTimelineStart(120.0), source->getTimelineEnd(120.0),
                                {track}, /*tempoBPM=*/120.0);

    PasteClipCommand paste(secondsToBeatPosition(2.0, 120.0));
    paste.execute();

    auto pastedIds = paste.getPastedClipIds();
    REQUIRE(pastedIds.size() == 1);

    auto* pasted = cm.getClip(pastedIds.front());
    REQUIRE(pasted != nullptr);

    // A copied slice should present the same waveform source identity as A.
    REQUIRE(primaryEventOf(pasted)->sourceFilePath() == primaryEventOf(source)->sourceFilePath());
    REQUIRE(primaryEventOf(pasted)->autoTempo == primaryEventOf(source)->autoTempo);
    REQUIRE(pasted->loopEnabled == source->loopEnabled);
    REQUIRE(primaryEventOf(pasted)->anchorBeats() ==
            Catch::Approx(primaryEventOf(source)->anchorBeats()));
    REQUIRE(primaryEventOf(pasted)->anchorSeconds() ==
            Catch::Approx(primaryEventOf(source)->anchorSeconds()));
    REQUIRE(primaryEventOf(pasted)->loopStartBeats() ==
            Catch::Approx(primaryEventOf(source)->loopStartBeats()));
    REQUIRE(primaryEventOf(pasted)->loopLengthBeats() ==
            Catch::Approx(primaryEventOf(source)->loopLengthBeats()));
    REQUIRE(primaryEventOf(pasted)->loopStartSeconds() ==
            Catch::Approx(primaryEventOf(source)->loopStartSeconds()));
    REQUIRE(primaryEventOf(pasted)->loopLengthSeconds() ==
            Catch::Approx(primaryEventOf(source)->loopLengthSeconds()));
    REQUIRE(primaryEventOf(pasted)->interpBpm == Catch::Approx(primaryEventOf(source)->interpBpm));
    REQUIRE(primaryEventOf(pasted)->interpTotalBeats ==
            Catch::Approx(primaryEventOf(source)->interpTotalBeats));
    REQUIRE(pasted->placement.lengthBeats == Catch::Approx(source->placement.lengthBeats));

    REQUIRE(pasted->placement.startBeat != Catch::Approx(source->placement.startBeat));
    REQUIRE(pasted->placement.startBeat == Catch::Approx(4.0));

    proj.setTempo(originalTempo);
}

// ============================================================================
// JoinClipsCommand
// ============================================================================

TEST_CASE("JoinClipsCommand - basic MIDI join", "[clip][command][join]") {
    resetState();
    TrackId track = createTrack();

    SECTION("Join two adjacent MIDI clips") {
        ClipId left = createMidi(track, 0.0, 2.0, {0.0, 2.0});
        ClipId right = createMidi(track, 2.0, 2.0, {0.0, 1.0});

        JoinClipsCommand cmd(left, right);
        REQUIRE(cmd.canExecute());
        cmd.execute();

        auto& cm = ClipManager::getInstance();
        auto* joined = cm.getClip(left);
        REQUIRE(joined != nullptr);
        REQUIRE(joined->startTime == Catch::Approx(0.0));
        REQUIRE(joined->length == Catch::Approx(4.0));

        // Right clip deleted
        REQUIRE(cm.getClip(right) == nullptr);

        // Notes merged: left had [0, 2], right had [0, 1] shifted by 4 beats -> [4, 5]
        REQUIRE(joined->midiNotes.size() == 4);
        REQUIRE(joined->midiNotes[0].startBeat == Catch::Approx(0.0));
        REQUIRE(joined->midiNotes[1].startBeat == Catch::Approx(2.0));
        REQUIRE(joined->midiNotes[2].startBeat == Catch::Approx(4.0));
        REQUIRE(joined->midiNotes[3].startBeat == Catch::Approx(5.0));
    }

    SECTION("Join looped MIDI clips flattens the joined result") {
        ClipId left = createMidi(track, 0.0, 2.0, {0.0});
        ClipId right = createMidi(track, 2.0, 2.0, {0.0});

        auto& cm = ClipManager::getInstance();
        auto* leftClip = cm.getClip(left);
        REQUIRE(leftClip != nullptr);
        leftClip->loopEnabled = true;
        leftClip->loopLengthBeats = leftClip->placement.lengthBeats;

        JoinClipsCommand cmd(left, right);
        REQUIRE(cmd.canExecute());
        cmd.execute();

        auto* joined = cm.getClip(left);
        REQUIRE(joined != nullptr);
        REQUIRE(joined->lengthBeats == Catch::Approx(8.0));
        REQUIRE_FALSE(joined->loopEnabled);
        REQUIRE(joined->loopLengthBeats == Catch::Approx(0.0));
        REQUIRE(joined->midiOffset == Catch::Approx(0.0));
        REQUIRE(joined->midiTrimOffset == Catch::Approx(0.0));
        REQUIRE(joined->midiNotes.size() == 2);
        REQUIRE(joined->midiNotes[0].startBeat == Catch::Approx(0.0));
        REQUIRE(joined->midiNotes[1].startBeat == Catch::Approx(4.0));
    }

    SECTION("Join notifies that the left MIDI clip changed") {
        ClipId left = createMidi(track, 0.0, 2.0, {0.0});
        ClipId right = createMidi(track, 2.0, 2.0, {0.0});

        RecordingClipListener listener;
        ClipManager::getInstance().addListener(&listener);

        JoinClipsCommand cmd(left, right);
        REQUIRE(cmd.canExecute());
        cmd.execute();

        ClipManager::getInstance().removeListener(&listener);

        REQUIRE(listener.clipsChangedCount > 0);
        REQUIRE(listener.sawPropertyChangeFor(left));
    }

    SECTION("Join three clips sequentially") {
        ClipId c1 = createMidi(track, 0.0, 2.0, {0.0});
        ClipId c2 = createMidi(track, 2.0, 2.0, {0.0});
        ClipId c3 = createMidi(track, 4.0, 2.0, {0.0});

        // Join c1+c2
        JoinClipsCommand cmd1(c1, c2);
        REQUIRE(cmd1.canExecute());
        cmd1.execute();

        // Now c1 is 0-4s, c3 is 4-6s
        JoinClipsCommand cmd2(c1, c3);
        REQUIRE(cmd2.canExecute());
        cmd2.execute();

        auto* joined = ClipManager::getInstance().getClip(c1);
        REQUIRE(joined != nullptr);
        REQUIRE(joined->length == Catch::Approx(6.0));
        REQUIRE(joined->midiNotes.size() == 3);
        REQUIRE(joined->midiNotes[0].startBeat == Catch::Approx(0.0));
        REQUIRE(joined->midiNotes[1].startBeat == Catch::Approx(4.0));
        REQUIRE(joined->midiNotes[2].startBeat == Catch::Approx(8.0));
    }
}

// ============================================================================
// FlattenMidiClipCommand
// ============================================================================

TEST_CASE("FlattenMidiClipCommand - renders a looped MIDI clip across its full length",
          "[clip][command][flatten]") {
    resetState();
    const TrackId track = createTrack();
    auto& cm = ClipManager::getInstance();
    const ClipId clipId = cm.createMidiClipBeats(track, 0.0, 10.0, ClipView::Arrangement);
    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);

    for (const double startBeat : {0.0, 1.0}) {
        MidiNote note;
        note.noteNumber = 60;
        note.startBeat = startBeat;
        note.lengthBeats = 1.0;
        note.velocity = 100;
        clip->midiNotes.push_back(note);
    }

    // A two-beat pattern with one beat phase offset renders at beats 0..9,
    // including controller data and pitch bends.
    clip->loopEnabled = true;
    clip->loopLengthBeats = 2.0;
    clip->midiOffset = 1.0;
    clip->midiTrimOffset = 1.25;
    MidiCCData cc;
    cc.controller = 1;
    cc.value = 64;
    cc.beatPosition = 0.5;
    clip->midiCCData.push_back(cc);
    MidiPitchBendData pitchBend;
    pitchBend.value = 4096;
    pitchBend.beatPosition = 0.5;
    clip->midiPitchBendData.push_back(pitchBend);

    const ClipInfo original = *clip;
    FlattenMidiClipCommand cmd(clipId);
    cmd.execute();

    REQUIRE_FALSE(clip->loopEnabled);
    REQUIRE(clip->loopLengthBeats == Catch::Approx(0.0));
    REQUIRE(clip->midiOffset == Catch::Approx(0.0));
    REQUIRE(clip->midiTrimOffset == Catch::Approx(0.0));
    REQUIRE(clip->midiNotes.size() == 10);
    for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
        REQUIRE(clip->midiNotes[i].startBeat == Catch::Approx(static_cast<double>(i)));
        REQUIRE(clip->midiNotes[i].lengthBeats == Catch::Approx(1.0));
    }
    REQUIRE(clip->midiCCData.size() == 5);
    REQUIRE(clip->midiPitchBendData.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        const double expectedBeat = 1.5 + static_cast<double>(i) * 2.0;
        REQUIRE(clip->midiCCData[i].beatPosition == Catch::Approx(expectedBeat));
        REQUIRE(clip->midiPitchBendData[i].beatPosition == Catch::Approx(expectedBeat));
    }

    cmd.undo();
    REQUIRE(clip->loopEnabled == original.loopEnabled);
    REQUIRE(clip->loopLengthBeats == Catch::Approx(original.loopLengthBeats));
    REQUIRE(clip->midiOffset == Catch::Approx(original.midiOffset));
    REQUIRE(clip->midiTrimOffset == Catch::Approx(original.midiTrimOffset));
    REQUIRE(clip->midiNotes.size() == original.midiNotes.size());
    REQUIRE(clip->midiCCData.size() == original.midiCCData.size());
    REQUIRE(clip->midiPitchBendData.size() == original.midiPitchBendData.size());
}

TEST_CASE("FlattenMidiClipCommand - respects loop and clip boundaries",
          "[clip][command][flatten]") {
    resetState();
    const TrackId track = createTrack();
    auto& cm = ClipManager::getInstance();

    SECTION("an exact number of cycles does not add an extra repetition") {
        const ClipId clipId = cm.createMidiClipBeats(track, 0.0, 8.0, ClipView::Arrangement);
        auto* clip = cm.getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->loopEnabled = true;
        clip->loopLengthBeats = 2.0;
        MidiNote note;
        note.startBeat = 0.0;
        clip->midiNotes.push_back(note);

        FlattenMidiClipCommand cmd(clipId);
        cmd.execute();

        REQUIRE(clip->midiNotes.size() == 4);
        for (size_t i = 0; i < clip->midiNotes.size(); ++i)
            REQUIRE(clip->midiNotes[i].startBeat == Catch::Approx(static_cast<double>(i) * 2.0));
    }

    SECTION("notes crossing a loop boundary are trimmed in every repetition") {
        const ClipId clipId = cm.createMidiClipBeats(track, 0.0, 6.0, ClipView::Arrangement);
        auto* clip = cm.getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->loopEnabled = true;
        clip->loopLengthBeats = 2.0;
        MidiNote note;
        note.startBeat = 1.5;
        clip->midiNotes.push_back(note);

        FlattenMidiClipCommand cmd(clipId);
        cmd.execute();

        REQUIRE(clip->midiNotes.size() == 3);
        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            REQUIRE(clip->midiNotes[i].startBeat ==
                    Catch::Approx(1.5 + static_cast<double>(i) * 2.0));
            REQUIRE(clip->midiNotes[i].lengthBeats == Catch::Approx(0.5));
        }
    }

    SECTION("phase offsets are wrapped into the loop range") {
        const ClipId clipId = cm.createMidiClipBeats(track, 0.0, 6.0, ClipView::Arrangement);
        auto* clip = cm.getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->loopEnabled = true;
        clip->loopLengthBeats = 2.0;
        clip->midiOffset = 3.0;
        for (const double startBeat : {0.0, 1.0}) {
            MidiNote note;
            note.startBeat = startBeat;
            clip->midiNotes.push_back(note);
        }

        FlattenMidiClipCommand cmd(clipId);
        cmd.execute();

        REQUIRE(clip->midiNotes.size() == 6);
        for (size_t i = 0; i < clip->midiNotes.size(); ++i)
            REQUIRE(clip->midiNotes[i].startBeat == Catch::Approx(static_cast<double>(i)));
    }

    SECTION("negative serialized phase offsets wrap without dropping the clip head") {
        const ClipId clipId = cm.createMidiClipBeats(track, 0.0, 6.0, ClipView::Arrangement);
        auto* clip = cm.getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->loopEnabled = true;
        clip->loopLengthBeats = 2.0;
        clip->midiOffset = -1.0;
        for (const double startBeat : {0.0, 1.0}) {
            MidiNote note;
            note.startBeat = startBeat;
            clip->midiNotes.push_back(note);
        }

        FlattenMidiClipCommand cmd(clipId);
        cmd.execute();

        REQUIRE(clip->midiNotes.size() == 6);
        for (size_t i = 0; i < clip->midiNotes.size(); ++i)
            REQUIRE(clip->midiNotes[i].startBeat == Catch::Approx(static_cast<double>(i)));
    }
}

TEST_CASE("JoinClipsCommand - canExecute validation", "[clip][command][join]") {
    resetState();
    TrackId track1 = createTrack("T1");
    TrackId track2 = createTrack("T2");

    SECTION("Cannot join non-adjacent clips") {
        ClipId c1 = createMidi(track1, 0.0, 2.0);
        ClipId c2 = createMidi(track1, 3.0, 2.0);  // gap at 2-3s
        JoinClipsCommand cmd(c1, c2);
        REQUIRE_FALSE(cmd.canExecute());
    }

    SECTION("Cannot join clips on different tracks") {
        ClipId c1 = createMidi(track1, 0.0, 2.0);
        ClipId c2 = createMidi(track2, 2.0, 2.0);
        JoinClipsCommand cmd(c1, c2);
        REQUIRE_FALSE(cmd.canExecute());
    }

    SECTION("Cannot join clips of different types") {
        TrackId audioTrack = createTrack("Audio", TrackType::Audio);
        ClipId midi = createMidi(track1, 0.0, 2.0);
        ClipId audio = createAudio(audioTrack, 2.0, 2.0);
        JoinClipsCommand cmd(midi, audio);
        REQUIRE_FALSE(cmd.canExecute());
    }

    SECTION("Cannot join with invalid clip IDs") {
        ClipId c1 = createMidi(track1, 0.0, 2.0);
        JoinClipsCommand cmd(c1, 9999);
        REQUIRE_FALSE(cmd.canExecute());
    }
}

TEST_CASE("JoinClipsCommand - undo/redo", "[clip][command][join][undo]") {
    resetState();
    TrackId track = createTrack();
    ClipId left = createMidi(track, 0.0, 2.0, {0.0, 2.0});
    ClipId right = createMidi(track, 2.0, 2.0, {0.0, 1.0});

    // Capture original state
    auto& cm = ClipManager::getInstance();
    double leftOrigLen = cm.getClip(left)->length;
    size_t leftOrigNotes = cm.getClip(left)->midiNotes.size();
    size_t rightOrigNotes = cm.getClip(right)->midiNotes.size();

    JoinClipsCommand cmd(left, right);
    cmd.execute();

    // Verify joined
    REQUIRE(cm.getClip(left)->length == Catch::Approx(4.0));
    REQUIRE(cm.getClip(right) == nullptr);

    // Undo restores both clips
    cmd.undo();

    auto* leftClip = cm.getClip(left);
    auto* rightClip = cm.getClip(right);
    REQUIRE(leftClip != nullptr);
    REQUIRE(rightClip != nullptr);
    REQUIRE(leftClip->length == Catch::Approx(leftOrigLen));
    REQUIRE(leftClip->midiNotes.size() == leftOrigNotes);
    REQUIRE(rightClip->startTime == Catch::Approx(2.0));
    REQUIRE(rightClip->length == Catch::Approx(2.0));
    REQUIRE(rightClip->midiNotes.size() == rightOrigNotes);
}

TEST_CASE("JoinClipsCommand - split then join roundtrip", "[clip][command][join][split]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 4.0, {0.0, 2.0, 4.0, 6.0});

    auto& cm = ClipManager::getInstance();
    size_t originalNoteCount = cm.getClip(original)->midiNotes.size();

    // Split at 2 seconds (4 beats at 120 BPM)
    SplitClipCommand splitCmd(original, secondsToBeatPosition(2.0));
    splitCmd.execute();
    ClipId rightId = splitCmd.getRightClipId();

    // Join them back
    JoinClipsCommand joinCmd(original, rightId);
    REQUIRE(joinCmd.canExecute());
    joinCmd.execute();

    auto* joined = cm.getClip(original);
    REQUIRE(joined != nullptr);
    REQUIRE(joined->length == Catch::Approx(4.0));
    REQUIRE(joined->midiNotes.size() == originalNoteCount);
}

TEST_CASE("SplitClipCommand - time-based audio ignores detected BPM for source offset",
          "[clip][command][split][audio]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createAudio(track, 0.0, 4.0);

    auto& cm = ClipManager::getInstance();
    auto* source = cm.getClip(original);
    REQUIRE(source != nullptr);
    primaryEventOf(source)->autoTempo = false;
    primaryEventOf(source)->warpEnabled = false;
    primaryEventOf(source)->interpBpm = 180.0;
    primaryEventOf(source)->speedRatio = 1.25;
    primaryEventOf(source)->setAnchorSeconds(0.4);

    SplitClipCommand cmd(original, secondsToBeatPosition(2.0), 120.0);
    REQUIRE(cmd.canExecute());
    cmd.execute();

    const auto* right = cm.getClip(cmd.getRightClipId());
    REQUIRE(right != nullptr);
    // Detected source BPM is metadata when tempo-follow and warp are disabled.
    // The right side must continue at timeline delta * speed ratio.
    REQUIRE(magda::audioEventRef(*right).anchorSeconds() == Catch::Approx(0.4 + 2.0 * 1.25));
}

TEST_CASE("SplitClipCommand - undo notifies restored left clip property",
          "[clip][command][split][undo]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createAudio(track, 0.0, 4.0);

    RecordingClipListener listener;
    auto& cm = ClipManager::getInstance();
    cm.addListener(&listener);

    SplitClipCommand cmd(original, secondsToBeatPosition(2.0), 120.0);
    REQUIRE(cmd.canExecute());
    cmd.execute();

    listener.clipsChangedCount = 0;
    listener.propertyChangedClipIds.clear();
    cmd.undo();

    cm.removeListener(&listener);

    REQUIRE(cm.getClip(original) != nullptr);
    REQUIRE(cm.getClip(original)->length == Catch::Approx(4.0));
    REQUIRE(listener.clipsChangedCount > 0);
    REQUIRE(listener.sawPropertyChangeFor(original));
}

// ============================================================================
// DeleteClipCommand
// ============================================================================

TEST_CASE("DeleteClipCommand - basic delete", "[clip][command][delete]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 2.0, {0.0, 1.0});

    DeleteClipCommand cmd(clipId);
    cmd.execute();

    REQUIRE(ClipManager::getInstance().getClip(clipId) == nullptr);
}

TEST_CASE("DeleteClipCommand - undo/redo", "[clip][command][delete][undo]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 1.0, 3.0, {0.0, 2.0, 4.0});

    auto& cm = ClipManager::getInstance();

    DeleteClipCommand cmd(clipId);
    cmd.execute();
    REQUIRE(cm.getClip(clipId) == nullptr);

    // Undo restores clip
    cmd.undo();
    auto* restored = cm.getClip(clipId);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->startTime == Catch::Approx(1.0));
    REQUIRE(restored->length == Catch::Approx(3.0));
    REQUIRE(restored->trackId == track);
    REQUIRE(restored->midiNotes.size() == 3);
    REQUIRE(restored->midiNotes[0].startBeat == Catch::Approx(0.0));
    REQUIRE(restored->midiNotes[1].startBeat == Catch::Approx(2.0));
    REQUIRE(restored->midiNotes[2].startBeat == Catch::Approx(4.0));

    // Redo deletes again
    cmd.execute();
    REQUIRE(cm.getClip(clipId) == nullptr);
}

// ============================================================================
// MoveClipCommand
// ============================================================================

TEST_CASE("MoveClipCommand - basic move", "[clip][command][move]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 2.0, {0.0, 1.0});

    MoveClipCommand cmd(clipId, secondsToBeatPosition(5.0));
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->startTime == Catch::Approx(5.0));
    REQUIRE(clip->length == Catch::Approx(2.0));
    // Notes unchanged (they're relative to clip)
    REQUIRE(clip->midiNotes[0].startBeat == Catch::Approx(0.0));
}

TEST_CASE("MoveClipCommand - undo/redo", "[clip][command][move][undo]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 1.0, 2.0);

    MoveClipCommand cmd(clipId, secondsToBeatPosition(5.0));
    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->startTime == Catch::Approx(5.0));

    cmd.undo();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->startTime == Catch::Approx(1.0));

    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->startTime == Catch::Approx(5.0));
}

// Regression: ClipManager::moveClip used to default tempo to 120 BPM, and
// MoveClipCommand::execute called it without an override. Anyone running a
// project at any other tempo therefore got a wrong startBeats baked in,
// which the next BPM change then translated into a wrong startTime — clips
// snapping to bizarre positions. The fix has moveClip read the live project
// tempo from ProjectManager when no explicit tempo is passed.
TEST_CASE("MoveClipCommand - startBeats derived from live project tempo, not 120",
          "[clip][command][move][bpm-snap-regression]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(90.0);

    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 0.0, 2.0);

    MoveClipCommand cmd(clipId, secondsToBeatPosition(6.0, 90.0));
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->startTime == Catch::Approx(6.0));
    // 6 seconds * 90 BPM / 60 = 9 beats. Pre-fix this was 12 (using the
    // hard-coded 120 default).
    REQUIRE(clip->startBeats == Catch::Approx(9.0));

    proj.setTempo(originalTempo);
}

// Companion to the regression above: with startBeats correctly derived from
// the live tempo, a subsequent BPM change keeps the clip at the same bar
// position (i.e. the clip's startTime tracks the new BPM via beats).
// Operates on the ClipManager state directly — TimelineController's
// SetTempoEvent does the same beats→seconds re-derivation on tempo change,
// just orchestrated through more layers.
TEST_CASE("MoveClipCommand - clip stays bar-anchored across BPM change",
          "[clip][command][move][bpm-snap-regression]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(90.0);

    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 0.0, 2.0);

    MoveClipCommand cmd(clipId, secondsToBeatPosition(6.0, 90.0));
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->startBeats == Catch::Approx(9.0));

    // Simulate the SetTempoEvent re-derivation: at 60 BPM, beat 9 lives at
    // 9 * 60/60 = 9 seconds.
    clip->startTime = (clip->startBeats * 60.0) / 60.0;
    REQUIRE(clip->startTime == Catch::Approx(9.0));

    proj.setTempo(originalTempo);
}

TEST_CASE("MoveClipCommand - merge consecutive moves", "[clip][command][move][merge]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 2.0);

    MoveClipCommand cmd1(clipId, secondsToBeatPosition(1.0));
    MoveClipCommand cmd2(clipId, secondsToBeatPosition(3.0));
    MoveClipCommand cmdOther(clipId + 1, secondsToBeatPosition(5.0));

    REQUIRE(cmd1.canMergeWith(&cmd2));
    REQUIRE_FALSE(cmd1.canMergeWith(&cmdOther));

    cmd1.mergeWith(&cmd2);
    cmd1.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->startTime == Catch::Approx(3.0));
}

// ============================================================================
// MoveClipToTrackCommand
// ============================================================================

TEST_CASE("MoveClipToTrackCommand - basic", "[clip][command][move][track]") {
    resetState();
    TrackId track1 = createTrack("T1");
    TrackId track2 = createTrack("T2");
    ClipId clipId = createMidi(track1, 0.0, 2.0);

    MoveClipToTrackCommand cmd(clipId, track2);
    REQUIRE(cmd.canExecute());
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->trackId == track2);
}

TEST_CASE("MoveClipToTrackCommand - undo/redo", "[clip][command][move][track][undo]") {
    resetState();
    TrackId track1 = createTrack("T1");
    TrackId track2 = createTrack("T2");
    ClipId clipId = createMidi(track1, 0.0, 2.0);

    MoveClipToTrackCommand cmd(clipId, track2);
    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->trackId == track2);

    cmd.undo();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->trackId == track1);

    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->trackId == track2);
}

TEST_CASE("MoveClipToTrackCommand - cannot move to invalid track", "[clip][command][move][track]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 2.0);

    MoveClipToTrackCommand cmd(clipId, INVALID_TRACK_ID);
    REQUIRE_FALSE(cmd.canExecute());
}

// ============================================================================
// ResizeClipCommand
// ============================================================================

TEST_CASE("ResizeClipCommand - resize from right", "[clip][command][resize]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 4.0);

    ResizeClipCommand cmd(clipId, secondsToBeatDuration(2.0), false);
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->length == Catch::Approx(2.0));
    REQUIRE(clip->startTime == Catch::Approx(0.0));  // Start unchanged
}

TEST_CASE("ResizeClipCommand - resize from left", "[clip][command][resize]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 2.0, 4.0);

    ResizeClipCommand cmd(clipId, secondsToBeatDuration(2.0), true);
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->length == Catch::Approx(2.0));
    // Start shifts right when resizing from left
    REQUIRE(clip->startTime == Catch::Approx(4.0));
}

TEST_CASE("ResizeClipCommand - undo/redo", "[clip][command][resize][undo]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 4.0);

    ResizeClipCommand cmd(clipId, secondsToBeatDuration(2.0), false);
    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->length == Catch::Approx(2.0));

    cmd.undo();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->length == Catch::Approx(4.0));

    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->length == Catch::Approx(2.0));
}

TEST_CASE("ResizeClipCommand - merge consecutive resizes", "[clip][command][resize][merge]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 4.0);

    ResizeClipCommand cmd1(clipId, secondsToBeatDuration(3.0), false);
    ResizeClipCommand cmd2(clipId, secondsToBeatDuration(2.0), false);
    ResizeClipCommand cmdFromLeft(clipId, secondsToBeatDuration(2.0), true);

    // Same clip, same direction: can merge
    REQUIRE(cmd1.canMergeWith(&cmd2));
    // Same clip, different direction: cannot merge
    REQUIRE_FALSE(cmd1.canMergeWith(&cmdFromLeft));
}

TEST_CASE("MIDI loop start command stores beats without seconds reinterpretation",
          "[clip][command][midi][loop][beats]") {
    resetState();
    constexpr double bpm = 90.0;
    TrackId track = createTrack("MIDI");
    ClipId clipId = createMidi(track, 0.0, 4.0);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    clip->loopStartBeats = 3.0;

    SetMidiClipLoopStartBeatsCommand cmd(clipId, 6.0, bpm);
    cmd.execute();

    clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->loopStartBeats == Catch::Approx(6.0));

    cmd.undo();

    clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->loopStartBeats == Catch::Approx(3.0));
}

TEST_CASE("MIDI loop length command stores beats without seconds reinterpretation",
          "[clip][command][midi][loop][beats]") {
    resetState();
    constexpr double bpm = 90.0;
    TrackId track = createTrack("MIDI");
    ClipId clipId = createMidi(track, 0.0, 4.0);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    clip->loopLengthBeats = 3.0;

    SetMidiClipLoopLengthBeatsCommand cmd(clipId, 6.0, bpm);
    cmd.execute();

    clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->loopLengthBeats == Catch::Approx(6.0));

    cmd.undo();

    clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->loopLengthBeats == Catch::Approx(3.0));
}

TEST_CASE("MIDI loop start setter takes seconds and stores clip beats",
          "[clip][midi][loop][beats]") {
    resetState();
    constexpr double bpm = 90.0;
    TrackId track = createTrack("MIDI");
    ClipId clipId = createMidi(track, 0.0, 4.0);

    ClipManager::getInstance().setLoopStart(clipId, 4.0, bpm);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->loopStartBeats == Catch::Approx(6.0));
}

TEST_CASE("DeleteTimeSelectionCommand - trim keeps beat placement in sync",
          "[clip][command][time-selection][delete]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 0.0, 4.0);

    DeleteTimeSelectionCommand cmd(2.0, 6.0, {track}, 120.0);
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->startTime == Catch::Approx(0.0));
    REQUIRE(clip->length == Catch::Approx(2.0));
    REQUIRE(clip->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(clip->placement.lengthBeats == Catch::Approx(4.0));
    REQUIRE(clip->startBeats == Catch::Approx(0.0));
    REQUIRE(clip->lengthBeats == Catch::Approx(4.0));

    cmd.undo();

    clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->length == Catch::Approx(4.0));
    REQUIRE(clip->placement.lengthBeats == Catch::Approx(8.0));
}

TEST_CASE("DeleteTimeSelectionCommand - looped trim keeps beat placement in sync",
          "[clip][command][time-selection][delete][loop]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 2.0, 4.0);

    auto* before = ClipManager::getInstance().getClip(clipId);
    REQUIRE(before != nullptr);
    before->loopEnabled = true;
    magda::primaryEventOf(before)->setLoopLengthSeconds(4.0);

    DeleteTimeSelectionCommand cmd(0.0, 3.0, {track}, 120.0);
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->startTime == Catch::Approx(3.0));
    REQUIRE(clip->length == Catch::Approx(3.0));
    REQUIRE(clip->placement.startBeat == Catch::Approx(6.0));
    REQUIRE(clip->placement.lengthBeats == Catch::Approx(6.0));
    REQUIRE(clip->startBeats == Catch::Approx(6.0));
    REQUIRE(clip->lengthBeats == Catch::Approx(6.0));
}

// ============================================================================
// InsertTimeCommand (ripple insert, beats-native)
// ============================================================================
// At 120 BPM: 1 second = 2 beats. Helpers create clips in seconds, so the
// beat placements below are 2x the seconds passed to createAudio().

TEST_CASE("InsertTimeCommand - shifts later clip right",
          "[clip][command][time-selection][insert]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 2.0, 1.0);  // beats [4, 6]

    // Insert 2 beats at beat 2: the clip starts after the insert point, so it
    // shifts right by 2 beats to [6, 8].
    InsertTimeCommand cmd(2.0, 2.0, {track}, 120.0);
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->placement.startBeat == Catch::Approx(6.0));
    REQUIRE(clip->placement.lengthBeats == Catch::Approx(2.0));

    cmd.undo();
    clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->placement.startBeat == Catch::Approx(4.0));
}

TEST_CASE("InsertTimeCommand - splits clip spanning the insert beat",
          "[clip][command][time-selection][insert]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 0.0, 2.0);  // beats [0, 4]

    // Insert 2 beats at beat 2: the clip spans the insert point, so it splits
    // into a head [0, 2] (keeps clipId) and a tail pushed right to [4, 6].
    InsertTimeCommand cmd(2.0, 2.0, {track}, 120.0);
    cmd.execute();

    auto* head = ClipManager::getInstance().getClip(clipId);
    REQUIRE(head != nullptr);
    REQUIRE(head->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(head->placement.lengthBeats == Catch::Approx(2.0));

    auto clips = ClipManager::getInstance().getArrangementClips();
    REQUIRE(clips.size() == 2);
    const ClipInfo* tail = nullptr;
    for (const auto& c : clips)
        if (c.id != clipId)
            tail = &c;
    REQUIRE(tail != nullptr);
    REQUIRE(tail->placement.startBeat == Catch::Approx(4.0));
    REQUIRE(tail->placement.lengthBeats == Catch::Approx(2.0));

    cmd.undo();
    clips = ClipManager::getInstance().getArrangementClips();
    REQUIRE(clips.size() == 1);
    auto* restored = ClipManager::getInstance().getClip(clipId);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(restored->placement.lengthBeats == Catch::Approx(4.0));
}

TEST_CASE("InsertTimeCommand - looped clip spanning insert beat grows",
          "[clip][command][time-selection][insert][loop]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 0.0, 2.0);  // beats [0, 4]

    auto* before = ClipManager::getInstance().getClip(clipId);
    REQUIRE(before != nullptr);
    before->loopEnabled = true;
    magda::primaryEventOf(before)->setLoopLengthSeconds(2.0);

    // Looped clip spanning the insert beat grows by the inserted duration
    // instead of splitting.
    InsertTimeCommand cmd(2.0, 2.0, {track}, 120.0);
    cmd.execute();

    auto clips = ClipManager::getInstance().getArrangementClips();
    REQUIRE(clips.size() == 1);
    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(clip->placement.lengthBeats == Catch::Approx(6.0));
}

TEST_CASE("InsertTimeCommand - clip entirely before insert beat is untouched",
          "[clip][command][time-selection][insert]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createAudio(track, 0.0, 1.0);  // beats [0, 2]

    InsertTimeCommand cmd(4.0, 2.0, {track}, 120.0);
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(clip->placement.lengthBeats == Catch::Approx(2.0));
}

// ============================================================================
// SplitClipsAtBeatCommand
// ============================================================================

TEST_CASE("SplitClipsAtBeatCommand - splits crossing clips on all tracks",
          "[clip][command][split]") {
    resetState();
    TrackId t1 = createTrack("T1");
    TrackId t2 = createTrack("T2");
    ClipId a = createAudio(t1, 0.0, 2.0);  // beats [0, 4] - crosses beat 2
    createAudio(t2, 0.0, 2.0);             // beats [0, 4] - crosses beat 2
    createAudio(t2, 3.0, 1.0);             // beats [6, 8] - entirely after beat 2

    SplitClipsAtBeatCommand cmd(2.0, {}, 120.0);  // empty trackIds = all tracks
    cmd.execute();

    // The two clips crossing beat 2 split; the [6,8] clip is untouched.
    auto clips = ClipManager::getInstance().getArrangementClips();
    REQUIRE(clips.size() == 5);
    REQUIRE(cmd.getCreatedClipIds().size() == 2);

    auto* leftA = ClipManager::getInstance().getClip(a);
    REQUIRE(leftA != nullptr);
    REQUIRE(leftA->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(leftA->placement.lengthBeats == Catch::Approx(2.0));  // [0, 2]

    cmd.undo();
    clips = ClipManager::getInstance().getArrangementClips();
    REQUIRE(clips.size() == 3);
    REQUIRE(ClipManager::getInstance().getClip(a)->placement.lengthBeats == Catch::Approx(4.0));
}

TEST_CASE("SplitClipsAtBeatCommand - clips on the boundary are not split",
          "[clip][command][split]") {
    resetState();
    TrackId t1 = createTrack("T1");
    createAudio(t1, 0.0, 2.0);  // beats [0, 4]

    // Beat 4 is the clip's end (not strictly inside) - no split.
    SplitClipsAtBeatCommand cmd(4.0, {}, 120.0);
    cmd.execute();

    REQUIRE(ClipManager::getInstance().getArrangementClips().size() == 1);
    REQUIRE(cmd.getCreatedClipIds().empty());
}

// ============================================================================
// RippleDeleteRangeCommand
// ============================================================================

TEST_CASE("RippleDeleteRangeCommand - shifts later clips left to close the gap",
          "[clip][command][ripple-delete]") {
    resetState();
    TrackId t1 = createTrack("T1");
    ClipId a = createAudio(t1, 0.0, 2.0);  // beats [0, 4]
    ClipId b = createAudio(t1, 3.0, 1.0);  // beats [6, 8]

    // Delete beats [4, 6] (the empty gap): a is untouched, b shifts left by 2.
    RippleDeleteRangeCommand cmd(4.0, 6.0, {}, 120.0);
    cmd.execute();

    auto* clipA = ClipManager::getInstance().getClip(a);
    auto* clipB = ClipManager::getInstance().getClip(b);
    REQUIRE(clipA != nullptr);
    REQUIRE(clipB != nullptr);
    REQUIRE(clipA->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(clipB->placement.startBeat == Catch::Approx(4.0));  // [6,8] -> [4,6]
    REQUIRE(clipB->placement.lengthBeats == Catch::Approx(2.0));

    cmd.undo();
    REQUIRE(ClipManager::getInstance().getClip(b)->placement.startBeat == Catch::Approx(6.0));
}

TEST_CASE("RippleDeleteRangeCommand - splits a spanning clip and closes the gap",
          "[clip][command][ripple-delete]") {
    resetState();
    TrackId t1 = createTrack("T1");
    ClipId a = createAudio(t1, 0.0, 4.0);  // beats [0, 8]

    // Delete beats [2, 4] from the middle: head [0,2] stays, the tail [4,8]
    // collapses left to [2,6].
    RippleDeleteRangeCommand cmd(2.0, 4.0, {}, 120.0);
    cmd.execute();

    auto clips = ClipManager::getInstance().getArrangementClips();
    REQUIRE(clips.size() == 2);
    auto* head = ClipManager::getInstance().getClip(a);
    REQUIRE(head != nullptr);
    REQUIRE(head->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(head->placement.lengthBeats == Catch::Approx(2.0));

    const ClipInfo* tail = nullptr;
    for (const auto& c : clips)
        if (c.id != a)
            tail = &c;
    REQUIRE(tail != nullptr);
    REQUIRE(tail->placement.startBeat == Catch::Approx(2.0));
    REQUIRE(tail->placement.lengthBeats == Catch::Approx(4.0));

    cmd.undo();
    clips = ClipManager::getInstance().getArrangementClips();
    REQUIRE(clips.size() == 1);
    REQUIRE(ClipManager::getInstance().getClip(a)->placement.lengthBeats == Catch::Approx(8.0));
}

// ============================================================================
// CreateClipCommand
// ============================================================================

TEST_CASE("CreateClipCommand - create MIDI clip", "[clip][command][create]") {
    resetState();
    TrackId track = createTrack();

    CreateClipCommand cmd(ClipType::MIDI, track, secondsToBeatPosition(1.0),
                          secondsToBeatDuration(3.0));
    REQUIRE(cmd.canExecute());
    cmd.execute();

    ClipId created = cmd.getCreatedClipId();
    REQUIRE(created != INVALID_CLIP_ID);

    auto* clip = ClipManager::getInstance().getClip(created);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->isMidi());
    REQUIRE(clip->startTime == Catch::Approx(1.0));
    REQUIRE(clip->length == Catch::Approx(3.0));
    REQUIRE(clip->trackId == track);
}

TEST_CASE("CreateClipCommand - undo/redo", "[clip][command][create][undo]") {
    resetState();
    TrackId track = createTrack();

    CreateClipCommand cmd(ClipType::MIDI, track, secondsToBeatPosition(0.0),
                          secondsToBeatDuration(2.0));
    cmd.execute();
    ClipId created = cmd.getCreatedClipId();
    REQUIRE(ClipManager::getInstance().getClip(created) != nullptr);

    cmd.undo();
    REQUIRE(ClipManager::getInstance().getClip(created) == nullptr);

    cmd.execute();
    // Clip should exist again after redo
    // Note: may have a different ID after redo
}

TEST_CASE("CreateClipCommand - validation", "[clip][command][create]") {
    resetState();

    SECTION("Cannot create with invalid track") {
        CreateClipCommand cmd(ClipType::MIDI, INVALID_TRACK_ID, secondsToBeatPosition(0.0),
                              secondsToBeatDuration(2.0));
        REQUIRE_FALSE(cmd.canExecute());
    }

    SECTION("Cannot create with zero length") {
        TrackId track = createTrack();
        CreateClipCommand cmd(ClipType::MIDI, track, secondsToBeatPosition(0.0),
                              secondsToBeatDuration(0.0));
        REQUIRE_FALSE(cmd.canExecute());
    }
}

// ============================================================================
// PasteClipCommand
// ============================================================================

TEST_CASE("PasteClipCommand - paste from clipboard", "[clip][command][paste]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 2.0, {0.0, 2.0});

    auto& cm = ClipManager::getInstance();

    // Copy to clipboard
    cm.copyToClipboard({original});
    REQUIRE(cm.hasClipsInClipboard());

    // Paste at time 5.0
    PasteClipCommand cmd(secondsToBeatPosition(5.0));
    REQUIRE(cmd.canExecute());
    cmd.execute();

    const auto& pastedIds = cmd.getPastedClipIds();
    REQUIRE(pastedIds.size() == 1);

    auto* pasted = cm.getClip(pastedIds[0]);
    REQUIRE(pasted != nullptr);
    REQUIRE(pasted->startTime == Catch::Approx(5.0));
    REQUIRE(pasted->length == Catch::Approx(2.0));
    REQUIRE(pasted->trackId == track);
}

TEST_CASE("PasteClipCommand - undo/redo", "[clip][command][paste][undo]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 2.0);

    auto& cm = ClipManager::getInstance();
    cm.copyToClipboard({original});

    PasteClipCommand cmd(secondsToBeatPosition(3.0));
    cmd.execute();
    const auto& pastedIds = cmd.getPastedClipIds();
    REQUIRE(!pastedIds.empty());
    ClipId pastedId = pastedIds[0];
    REQUIRE(cm.getClip(pastedId) != nullptr);

    // Undo removes pasted clip
    cmd.undo();
    REQUIRE(cm.getClip(pastedId) == nullptr);
    // Original untouched
    REQUIRE(cm.getClip(original) != nullptr);
}

TEST_CASE("PasteClipCommand - cannot paste empty clipboard", "[clip][command][paste]") {
    resetState();
    createTrack();

    // Clipboard is empty after reset
    ClipManager::getInstance().clearClipboard();
    PasteClipCommand cmd(secondsToBeatPosition(0.0));
    REQUIRE_FALSE(cmd.canExecute());
}

TEST_CASE("PasteClipCommand - paste multiple clips", "[clip][command][paste]") {
    resetState();
    TrackId track = createTrack();
    ClipId c1 = createMidi(track, 0.0, 2.0);
    ClipId c2 = createMidi(track, 2.0, 1.0);

    auto& cm = ClipManager::getInstance();
    cm.copyToClipboard({c1, c2});

    PasteClipCommand cmd(secondsToBeatPosition(10.0));
    cmd.execute();

    const auto& pastedIds = cmd.getPastedClipIds();
    REQUIRE(pastedIds.size() == 2);

    // Both pasted clips should exist
    for (ClipId id : pastedIds) {
        REQUIRE(cm.getClip(id) != nullptr);
    }
}

TEST_CASE("Paste preserves the enabled flag", "[clip][command][paste]") {
    resetState();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 2.0, {0.0});

    auto& cm = ClipManager::getInstance();
    cm.getClip(original)->enabled = false;
    cm.copyToClipboard({original});

    const auto pasted = cm.pasteFromClipboardBeats(16.0, track, ClipView::Arrangement);
    REQUIRE(pasted.size() == 1);
    REQUIRE_FALSE(cm.getClip(pasted.front())->enabled);
}

// ============================================================================
// resolveOverlaps - covering a clip never destroys it (#2003)
// ============================================================================

/// What a clip plays once the clips stacked over it are taken into account. The
/// model keeps every clip whole, so a cover only shows up here.
static AudibleSpan audibleSpanFor(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);

    std::vector<ClipInfo> lane;
    for (ClipId other : cm.getClipsOnTrack(clip->trackId, ClipView::Arrangement)) {
        if (const auto* c = cm.getClip(other))
            lane.push_back(*c);
    }
    return computeAudibleSpans(lane).at(clipId);
}

// Moving a clip used to read as an eraser: whatever it landed on top of was
// trimmed back or deleted outright. Nothing is cut now — a covered clip keeps
// its placement and its content and simply stops being heard, so dragging the
// covering clip away fills the gap with no restore step.
TEST_CASE("resolveOverlaps - covering a clip leaves it whole and gives it back",
          "[clip][overlap][regression]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(120.0);

    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack("Track", TrackType::Audio);

    SECTION("covered end to end") {
        ClipId covered = createAudio(track, 2.0, 2.0);  // [4,8] beats
        ClipId mover = createAudio(track, 8.0, 8.0);    // [16,32] beats
        // AUTO-XFADE off on both: with it on these two would fade into each
        // other instead, which is its own test in test_clip_crossfades.
        cm.setAutoCrossfade(covered, false);
        cm.setAutoCrossfade(mover, false);
        cm.moveClipBeats(mover, 0.0, 120.0);  // -> [0,16], swallows it

        REQUIRE(cm.getClipsOnTrack(track).size() == 2);
        const auto* still = cm.getClip(covered);
        REQUIRE(still != nullptr);
        CHECK(still->enabled);
        CHECK(still->placement.startBeat == Catch::Approx(4.0));
        CHECK(still->placement.lengthBeats == Catch::Approx(4.0));
        CHECK_FALSE(audibleSpanFor(covered).audible);

        cm.moveClipBeats(mover, 32.0, 120.0);
        CHECK(audibleSpanFor(covered).lengthBeats == Catch::Approx(4.0));
    }

    SECTION("covered at one edge") {
        ClipId covered = createMidi(track, 0.0, 4.0);  // [0,8] beats
        ClipId mover = createMidi(track, 8.0, 4.0);    // [16,24] beats
        cm.moveClipBeats(mover, 4.0, 120.0);           // -> [4,12], covers the tail

        const auto* still = cm.getClip(covered);
        REQUIRE(still != nullptr);
        CHECK(still->placement.lengthBeats == Catch::Approx(8.0));
        CHECK(audibleSpanFor(covered).lengthBeats == Catch::Approx(4.0));

        cm.moveClipBeats(mover, 16.0, 120.0);
        CHECK(audibleSpanFor(covered).lengthBeats == Catch::Approx(8.0));
    }

    proj.setTempo(originalTempo);
}

// The one case that still splits, because a head and a tail cannot be one span.
// It splits into THREE: the covered slice is kept, so nothing under the drop is
// lost — not the audio, and not the notes, which the split partitions between
// the pieces. Also the regression guard for #1447, where everything to the
// right of the drop used to disappear.
TEST_CASE("resolveOverlaps - a clip dropped inside another keeps the covered slice",
          "[clip][overlap][regression]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(120.0);

    auto& cm = ClipManager::getInstance();
    TrackId trackC = createTrack("Long", TrackType::Audio);
    TrackId trackS = createTrack("Source", TrackType::Audio);

    ClipId longClip = createAudio(trackC, 0.0, 8.0);  // [0,16] beats
    ClipId source = createAudio(trackS, 0.0, 2.0);    // [0,4] beats

    // AUTO-XFADE off: this test is about the split and the slice under the drop,
    // not about the fade two flagged clips would make instead.
    cm.setAutoCrossfade(longClip, false);
    cm.setAutoCrossfade(source, false);

    ClipId dropped = cm.duplicateClipAtBeats(source, 6.0, trackC, 120.0);  // [6,10]
    REQUIRE(dropped != INVALID_CLIP_ID);
    cm.setAutoCrossfade(dropped, false);

    // Head [0,6], the covered slice [6,10] and tail [10,16], plus the drop.
    const auto onTrack = cm.getClipsOnTrack(trackC);
    REQUIRE(onTrack.size() == 4);

    const auto* head = cm.getClip(longClip);
    REQUIRE(head != nullptr);
    CHECK(head->placement.startBeat == Catch::Approx(0.0));
    CHECK(head->placement.lengthBeats == Catch::Approx(6.0));

    // The three pieces cover the original span end to end, with no gap.
    double covered = 0.0;
    for (ClipId id : onTrack) {
        if (id == dropped)
            continue;
        const auto* piece = cm.getClip(id);
        REQUIRE(piece != nullptr);
        covered += piece->placement.lengthBeats;
    }
    CHECK(covered == Catch::Approx(16.0));

    // The slice under the drop is silent while the drop is on top of it, and
    // plays again the moment the drop moves away.
    ClipId slice = INVALID_CLIP_ID;
    for (ClipId id : onTrack) {
        const auto* piece = cm.getClip(id);
        if (id != dropped && piece->placement.startBeat == Catch::Approx(6.0))
            slice = id;
    }
    REQUIRE(slice != INVALID_CLIP_ID);
    CHECK_FALSE(audibleSpanFor(slice).audible);

    cm.moveClipBeats(dropped, 40.0, 120.0);
    CHECK(audibleSpanFor(slice).lengthBeats == Catch::Approx(4.0));

    proj.setTempo(originalTempo);
}

// The reported loss: a clip dropped into the middle of a MIDI part used to
// split it into pieces and delete the covered slice, taking the notes in that
// span with it. MIDI clips are not split at all now — the part stays one clip
// with every note still on it, and the notes under the drop simply are not
// played until the drop moves away.
TEST_CASE("resolveOverlaps - dropping a clip inside a MIDI clip keeps it whole",
          "[clip][overlap][regression]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(120.0);

    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack("Track", TrackType::Audio);

    // [0,16] beats with a note before, under and after the drop.
    ClipId part = createMidi(track, 0.0, 8.0, {2.0, 8.0, 14.0});
    ClipId dropped = cm.createMidiClipBeats(track, 6.0, 4.0, ClipView::Arrangement,
                                            ClipOverlapPolicy::ResolveOverlaps);
    REQUIRE(dropped != INVALID_CLIP_ID);

    // No split: two clips, and the part is untouched.
    REQUIRE(cm.getClipsOnTrack(track).size() == 2);
    const auto* whole = cm.getClip(part);
    REQUIRE(whole != nullptr);
    CHECK(whole->placement.startBeat == Catch::Approx(0.0));
    CHECK(whole->placement.lengthBeats == Catch::Approx(16.0));
    CHECK(whole->midiNotes.size() == 3);

    // It plays around the drop: full span, one hole where the drop sits.
    const auto span = audibleSpanFor(part);
    CHECK(span.lengthBeats == Catch::Approx(16.0));
    REQUIRE(span.silenced.size() == 1);
    CHECK(span.silenced[0].start.value == Catch::Approx(6.0));
    CHECK(span.silenced[0].end.value == Catch::Approx(10.0));

    // Move the drop away and the hole closes on its own.
    cm.moveClipBeats(dropped, 40.0, 120.0);
    CHECK(audibleSpanFor(part).silenced.empty());
    CHECK(cm.getClip(part)->midiNotes.size() == 3);

    proj.setTempo(originalTempo);
}

// ============================================================================
// Bounce ranges (#1731)
// ============================================================================

TEST_CASE("Bounce selection range prioritizes time selection over clip selection",
          "[clip][command][bounce]") {
    const std::vector<ClipInfo> selectedClips = {makeBounceClip(4.0, 4.0),
                                                 makeBounceClip(12.0, 6.0)};

    const auto timeRange = resolveBounceSelectionRange({2.0, 10.0}, true, selectedClips);
    REQUIRE(timeRange.startBeats == Catch::Approx(2.0));
    REQUIRE(timeRange.endBeats == Catch::Approx(10.0));

    const auto clipRange = resolveBounceSelectionRange({}, false, selectedClips);
    REQUIRE(clipRange.startBeats == Catch::Approx(4.0));
    REQUIRE(clipRange.endBeats == Catch::Approx(18.0));

    REQUIRE_FALSE(resolveBounceSelectionRange({}, false, {}).isValid());
}

TEST_CASE("Bounce render range keeps beats authoritative through tempo conversion",
          "[clip][command][bounce]") {
    const TestTempoMap tempoMap;
    const auto sourceClip = makeBounceClip(4.0, 12.0);

    SECTION("in-place bounce constrains the selection to the source clip") {
        const auto range = resolveBounceRenderRange(sourceClip, {2.0, 10.0}, &tempoMap, true);

        REQUIRE(range.startBeats == Catch::Approx(4.0));
        REQUIRE(range.endBeats == Catch::Approx(10.0));
        REQUIRE(range.startSeconds == Catch::Approx(9.0));
        REQUIRE(range.endSeconds == Catch::Approx(18.0));
    }

    SECTION("bounce to a new track keeps the requested range intact") {
        const auto range = resolveBounceRenderRange(sourceClip, {2.0, 18.0}, &tempoMap, false);

        REQUIRE(range.startBeats == Catch::Approx(2.0));
        REQUIRE(range.endBeats == Catch::Approx(18.0));
        REQUIRE(range.startSeconds == Catch::Approx(6.0));
        REQUIRE(range.endSeconds == Catch::Approx(30.0));
    }

    SECTION("a non-overlapping in-place selection falls back to the source clip") {
        const auto range = resolveBounceRenderRange(sourceClip, {20.0, 24.0}, &tempoMap, true);

        REQUIRE(range.startBeats == Catch::Approx(4.0));
        REQUIRE(range.endBeats == Catch::Approx(16.0));
    }
}

TEST_CASE("BounceInPlaceReplacement preserves unselected source and undo restores it",
          "[clip][command][bounce][undo]") {
    resetState();
    auto& clipManager = ClipManager::getInstance();
    const auto trackId = createTrack();
    const auto sourceClipId = clipManager.createMidiClipBeats(trackId, 0.0, 16.0);
    const auto* sourceClip = clipManager.getClip(sourceClipId);
    REQUIRE(sourceClip != nullptr);

    BounceInPlaceReplacement replacement;
    replacement.setOriginalClip(*sourceClip);
    const BounceRenderRange range{4.0, 12.0, 2.0, 6.0};
    REQUIRE(replacement.replace(clipManager, sourceClipId, range, "bounced.wav", 120.0));

    const auto bouncedClipId = replacement.getNewClipId();
    const auto* bouncedClip = clipManager.getClip(bouncedClipId);
    REQUIRE(bouncedClip != nullptr);
    REQUIRE(bouncedClip->isAudio());
    REQUIRE(bouncedClip->placement.startBeat == Catch::Approx(4.0));
    REQUIRE(bouncedClip->placement.lengthBeats == Catch::Approx(8.0));

    const auto* leftSource = clipManager.getClip(sourceClipId);
    REQUIRE(leftSource != nullptr);
    REQUIRE(leftSource->isMidi());
    REQUIRE(leftSource->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(leftSource->placement.lengthBeats == Catch::Approx(4.0));

    bool foundRightSource = false;
    for (const auto clipId : clipManager.getClipsOnTrack(trackId)) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip == nullptr || clipId == sourceClipId || clipId == bouncedClipId)
            continue;
        REQUIRE(clip->isMidi());
        REQUIRE(clip->placement.startBeat == Catch::Approx(12.0));
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(4.0));
        foundRightSource = true;
    }
    REQUIRE(foundRightSource);

    replacement.undo(clipManager);

    const auto clipsAfterUndo = clipManager.getClipsOnTrack(trackId);
    REQUIRE(clipsAfterUndo.size() == 1);
    const auto* restoredClip = clipManager.getClip(sourceClipId);
    REQUIRE(restoredClip != nullptr);
    REQUIRE(restoredClip->isMidi());
    REQUIRE(restoredClip->placement.startBeat == Catch::Approx(0.0));
    REQUIRE(restoredClip->placement.lengthBeats == Catch::Approx(16.0));
}

// ============================================================================
// FlattenClipStackCommand - fold overlapping MIDI clips into one (#2003)
// ============================================================================

TEST_CASE("FlattenClipStackCommand - commits what the stack plays into one clip",
          "[clip][command][flatten][overlap]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(120.0);
    auto& config = Config::getInstance();
    const bool originalPlaysBoth = config.getClipOverlapPlaysBoth();
    config.setClipOverlapPlaysBoth(false);

    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack("Track", TrackType::Audio);

    // [0,16] with notes at 2, 8 and 14; the drop covers [6,10], so the note at
    // 8 is the one nobody hears.
    ClipId part = createMidi(track, 0.0, 8.0, {2.0, 8.0, 14.0});
    ClipId dropped = cm.createMidiClipBeats(track, 6.0, 4.0, ClipView::Arrangement,
                                            ClipOverlapPolicy::ResolveOverlaps);
    REQUIRE(dropped != INVALID_CLIP_ID);
    // One note inside the dropped clip, at timeline beat 7.
    cm.addMidiNote(dropped, {72, 100, 1.0, 1.0});

    SECTION("top wins: the covered note is not carried over") {
        FlattenClipStackCommand cmd(dropped);
        cmd.execute();

        // One clip left, spanning the whole stack.
        const auto onTrack = cm.getClipsOnTrack(track);
        REQUIRE(onTrack.size() == 1);
        const auto* merged = cm.getClip(dropped);
        REQUIRE(merged != nullptr);
        CHECK(merged->placement.startBeat == Catch::Approx(0.0));
        CHECK(merged->placement.lengthBeats == Catch::Approx(16.0));
        CHECK_FALSE(merged->loopEnabled);

        // Notes at 2 and 14 from the part, 7 from the drop. The part's note at
        // 8 was silent under the drop, so flattening drops it too.
        REQUIRE(merged->midiNotes.size() == 3);
        CHECK(merged->midiNotes[0].startBeat == Catch::Approx(2.0));
        CHECK(merged->midiNotes[1].startBeat == Catch::Approx(7.0));
        CHECK(merged->midiNotes[2].startBeat == Catch::Approx(14.0));

        SECTION("and undo puts the stack back") {
            cmd.undo();
            CHECK(cm.getClipsOnTrack(track).size() == 2);
            const auto* restored = cm.getClip(part);
            REQUIRE(restored != nullptr);
            CHECK(restored->midiNotes.size() == 3);
        }
    }

    SECTION("play both: every note is carried over") {
        // The switch is on the clip now, not on the app.
        cm.setOverlapPlaysBoth(dropped, true);

        FlattenClipStackCommand cmd(dropped);
        cmd.execute();

        const auto* merged = cm.getClip(dropped);
        REQUIRE(merged != nullptr);
        REQUIRE(merged->midiNotes.size() == 4);
        CHECK(merged->midiNotes[1].startBeat == Catch::Approx(7.0));
        CHECK(merged->midiNotes[2].startBeat == Catch::Approx(8.0));
    }

    config.setClipOverlapPlaysBoth(originalPlaysBoth);
    proj.setTempo(originalTempo);
}

TEST_CASE("FlattenClipStackCommand - only offers itself when there is a stack to fold",
          "[clip][command][flatten][overlap]") {
    resetState();
    auto& proj = ProjectManager::getInstance();
    const double originalTempo = proj.getCurrentProjectInfo().tempo;
    proj.setTempo(120.0);

    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack("Track", TrackType::Audio);

    SECTION("abutting clips are not a stack") {
        ClipId a = createMidi(track, 0.0, 4.0, {0.0});
        createMidi(track, 4.0, 4.0, {0.0});
        CHECK(FlattenClipStackCommand::collectStack(a).empty());
    }

    SECTION("an audio clip in the way refuses — that is what Bounce is for") {
        ClipId midi = createMidi(track, 0.0, 8.0, {0.0});
        ClipId audio = createAudio(track, 2.0, 4.0);
        cm.moveClipBeats(audio, 8.0, 120.0);
        CHECK(FlattenClipStackCommand::collectStack(midi).empty());
    }

    SECTION("a chain of overlaps folds in one go") {
        ClipId a = createMidi(track, 0.0, 8.0, {0.0});
        ClipId b = cm.createMidiClipBeats(track, 12.0, 16.0, ClipView::Arrangement,
                                          ClipOverlapPolicy::ResolveOverlaps);
        ClipId c = cm.createMidiClipBeats(track, 24.0, 16.0, ClipView::Arrangement,
                                          ClipOverlapPolicy::ResolveOverlaps);
        // a[0,16] over b[12,28] over c[24,40]: a and c never touch.
        const auto stack = FlattenClipStackCommand::collectStack(a);
        CHECK(stack.size() == 3);
        CHECK(std::find(stack.begin(), stack.end(), c) != stack.end());
        juce::ignoreUnused(b);
    }

    proj.setTempo(originalTempo);
}
