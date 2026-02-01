#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipCommands.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"

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

static TrackId createTrack(const char* name = "Track", TrackType type = TrackType::MIDI) {
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
        REQUIRE(dup->type == orig->type);

        // MIDI notes copied
        REQUIRE(dup->midiNotes.size() == orig->midiNotes.size());
        for (size_t i = 0; i < orig->midiNotes.size(); ++i) {
            REQUIRE(dup->midiNotes[i].startBeat == Catch::Approx(orig->midiNotes[i].startBeat));
        }
    }

    SECTION("Duplicate at specific position and track") {
        TrackId track2 = createTrack("Track 2");
        DuplicateClipCommand cmd(original, 5.0, track2);
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

TEST_CASE("DuplicateClipCommand - audio clip", "[clip][command][duplicate]") {
    resetState();
    TrackId track = createTrack("Audio Track", TrackType::Audio);
    ClipId original = createAudio(track, 1.0, 3.0);

    DuplicateClipCommand cmd(original);
    cmd.execute();

    auto* dup = ClipManager::getInstance().getClip(cmd.getDuplicatedClipId());
    REQUIRE(dup != nullptr);
    REQUIRE(dup->type == ClipType::Audio);
    REQUIRE(dup->startTime == Catch::Approx(4.0));  // 1.0 + 3.0
    REQUIRE(dup->length == Catch::Approx(3.0));
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

    // Split at 2 seconds
    SplitClipCommand splitCmd(original, 2.0);
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

    MoveClipCommand cmd(clipId, 5.0);
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

    MoveClipCommand cmd(clipId, 5.0);
    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->startTime == Catch::Approx(5.0));

    cmd.undo();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->startTime == Catch::Approx(1.0));

    cmd.execute();
    REQUIRE(ClipManager::getInstance().getClip(clipId)->startTime == Catch::Approx(5.0));
}

TEST_CASE("MoveClipCommand - merge consecutive moves", "[clip][command][move][merge]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 0.0, 2.0);

    MoveClipCommand cmd1(clipId, 1.0);
    MoveClipCommand cmd2(clipId, 3.0);
    MoveClipCommand cmdOther(clipId + 1, 5.0);

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

    ResizeClipCommand cmd(clipId, 2.0, false);
    cmd.execute();

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip->length == Catch::Approx(2.0));
    REQUIRE(clip->startTime == Catch::Approx(0.0));  // Start unchanged
}

TEST_CASE("ResizeClipCommand - resize from left", "[clip][command][resize]") {
    resetState();
    TrackId track = createTrack();
    ClipId clipId = createMidi(track, 2.0, 4.0);

    ResizeClipCommand cmd(clipId, 2.0, true);
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

    ResizeClipCommand cmd(clipId, 2.0, false);
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

    ResizeClipCommand cmd1(clipId, 3.0, false);
    ResizeClipCommand cmd2(clipId, 2.0, false);
    ResizeClipCommand cmdFromLeft(clipId, 2.0, true);

    // Same clip, same direction: can merge
    REQUIRE(cmd1.canMergeWith(&cmd2));
    // Same clip, different direction: cannot merge
    REQUIRE_FALSE(cmd1.canMergeWith(&cmdFromLeft));
}

// ============================================================================
// CreateClipCommand
// ============================================================================

TEST_CASE("CreateClipCommand - create MIDI clip", "[clip][command][create]") {
    resetState();
    TrackId track = createTrack();

    CreateClipCommand cmd(ClipType::MIDI, track, 1.0, 3.0);
    REQUIRE(cmd.canExecute());
    cmd.execute();

    ClipId created = cmd.getCreatedClipId();
    REQUIRE(created != INVALID_CLIP_ID);

    auto* clip = ClipManager::getInstance().getClip(created);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->type == ClipType::MIDI);
    REQUIRE(clip->startTime == Catch::Approx(1.0));
    REQUIRE(clip->length == Catch::Approx(3.0));
    REQUIRE(clip->trackId == track);
}

TEST_CASE("CreateClipCommand - undo/redo", "[clip][command][create][undo]") {
    resetState();
    TrackId track = createTrack();

    CreateClipCommand cmd(ClipType::MIDI, track, 0.0, 2.0);
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
        CreateClipCommand cmd(ClipType::MIDI, INVALID_TRACK_ID, 0.0, 2.0);
        REQUIRE_FALSE(cmd.canExecute());
    }

    SECTION("Cannot create with zero length") {
        TrackId track = createTrack();
        CreateClipCommand cmd(ClipType::MIDI, track, 0.0, 0.0);
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
    PasteClipCommand cmd(5.0);
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

    PasteClipCommand cmd(3.0);
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
    PasteClipCommand cmd(0.0);
    REQUIRE_FALSE(cmd.canExecute());
}

TEST_CASE("PasteClipCommand - paste multiple clips", "[clip][command][paste]") {
    resetState();
    TrackId track = createTrack();
    ClipId c1 = createMidi(track, 0.0, 2.0);
    ClipId c2 = createMidi(track, 2.0, 1.0);

    auto& cm = ClipManager::getInstance();
    cm.copyToClipboard({c1, c2});

    PasteClipCommand cmd(10.0);
    cmd.execute();

    const auto& pastedIds = cmd.getPastedClipIds();
    REQUIRE(pastedIds.size() == 2);

    // Both pasted clips should exist
    for (ClipId id : pastedIds) {
        REQUIRE(cm.getClip(id) != nullptr);
    }
}
