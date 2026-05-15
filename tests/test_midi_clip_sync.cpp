#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOperations.hpp"
#include "magda/daw/core/MidiNoteCommands.hpp"
#include "magda/daw/core/UndoManager.hpp"

/**
 * Tests for MIDI clip synchronization and note-clip relationship
 *
 * These tests verify:
 * - Notes are stored clip-relative (beat 0 = clip start)
 * - Clip position changes don't affect note positions
 * - Clip length changes preserve note positions
 * - Notes beyond clip boundary are handled correctly
 */

namespace {
constexpr double kTestBpm = 120.0;

void setPlacementBeats(magda::ClipInfo& clip, double startBeat, double lengthBeats) {
    clip.setPlacementBeats(startBeat, lengthBeats);
    clip.deriveTimesFromBeats(kTestBpm);
}
}  // namespace

TEST_CASE("MidiNote - Clip-relative storage", "[midi][clip][storage]") {
    using namespace magda;

    SECTION("Notes are stored relative to clip start") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 8.0, 16.0);  // Clip at 4 seconds, 8 seconds long at 120 BPM

        MidiNote note;
        note.startBeat = 0.0;  // Relative to clip start
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        note.velocity = 100;

        clip.midiNotes.push_back(note);

        // Note position is clip-relative, NOT absolute
        REQUIRE(clip.midiNotes[0].startBeat == 0.0);

        // Absolute timeline position would be: clipStart + noteStart
        // In this case: 4.0s + 0 beats = 4.0s (but note stores 0, not 4)
        REQUIRE(clip.placement.startBeat == Catch::Approx(8.0));
        REQUIRE(clip.startTime == Catch::Approx(4.0));
        REQUIRE(clip.midiNotes[0].startBeat != clip.placement.startBeat);
    }

    SECTION("Multiple notes at different clip-relative positions") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 0.0, 16.0);

        // Add notes at beats 0, 1, 2, 3 (clip-relative)
        for (int i = 0; i < 4; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i);
            note.lengthBeats = 1.0;
            note.noteNumber = 60;
            note.velocity = 100;
            clip.midiNotes.push_back(note);
        }

        REQUIRE(clip.midiNotes.size() == 4);
        REQUIRE(clip.midiNotes[0].startBeat == 0.0);
        REQUIRE(clip.midiNotes[1].startBeat == 1.0);
        REQUIRE(clip.midiNotes[2].startBeat == 2.0);
        REQUIRE(clip.midiNotes[3].startBeat == 3.0);
    }
}

TEST_CASE("MidiClip - Position changes don't affect notes", "[midi][clip][position]") {
    using namespace magda;

    SECTION("Moving clip preserves note positions") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 0.0, 16.0);

        MidiNote note;
        note.startBeat = 2.0;  // Note at beat 2 within clip
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        double originalNoteBeat = clip.midiNotes[0].startBeat;

        // Move clip to a different timeline position
        setPlacementBeats(clip, 20.0, clip.placement.lengthBeats);

        // Note position within clip should be unchanged
        REQUIRE(clip.midiNotes[0].startBeat == originalNoteBeat);
        REQUIRE(clip.midiNotes[0].startBeat == 2.0);
    }

    SECTION("Moving clip multiple times preserves notes") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 0.0, 8.0);

        MidiNote note;
        note.startBeat = 1.5;
        note.lengthBeats = 0.5;
        note.noteNumber = 64;
        note.velocity = 80;
        clip.midiNotes.push_back(note);

        // Move clip multiple times
        setPlacementBeats(clip, 4.0, clip.placement.lengthBeats);
        REQUIRE(clip.midiNotes[0].startBeat == 1.5);

        setPlacementBeats(clip, 16.0, clip.placement.lengthBeats);
        REQUIRE(clip.midiNotes[0].startBeat == 1.5);

        setPlacementBeats(clip, 0.0, clip.placement.lengthBeats);
        REQUIRE(clip.midiNotes[0].startBeat == 1.5);
    }
}

TEST_CASE("MidiClip - Length changes preserve note positions", "[midi][clip][length]") {
    using namespace magda;

    SECTION("Shortening clip from end preserves notes at start") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 0.0, 32.0);

        // Add 4 notes at beats 0, 1, 2, 3
        for (int i = 0; i < 4; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i);
            note.lengthBeats = 1.0;
            note.noteNumber = 60;
            note.velocity = 100;
            clip.midiNotes.push_back(note);
        }

        // Shorten clip to 2 bars
        setPlacementBeats(clip, clip.placement.startBeat, 8.0);

        // ALL notes should still have same positions
        REQUIRE(clip.midiNotes[0].startBeat == 0.0);
        REQUIRE(clip.midiNotes[1].startBeat == 1.0);
        REQUIRE(clip.midiNotes[2].startBeat == 2.0);
        REQUIRE(clip.midiNotes[3].startBeat == 3.0);

        // Notes are within new clip length, so they should all play
        double clipLengthInBeats = clip.placement.lengthBeats;
        for (const auto& note : clip.midiNotes) {
            REQUIRE(note.startBeat < clipLengthInBeats);
        }
    }

    SECTION("Extending clip doesn't shift existing notes") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 4.0, 4.0);

        MidiNote note;
        note.startBeat = 0.5;
        note.lengthBeats = 0.25;
        note.noteNumber = 72;
        note.velocity = 90;
        clip.midiNotes.push_back(note);

        double originalBeat = clip.midiNotes[0].startBeat;

        // Extend clip to 4 bars
        setPlacementBeats(clip, clip.placement.startBeat, 16.0);

        // Note position unchanged
        REQUIRE(clip.midiNotes[0].startBeat == originalBeat);
        REQUIRE(clip.midiNotes[0].startBeat == 0.5);
    }
}

TEST_CASE("MidiClip - Notes beyond clip boundary", "[midi][clip][boundary]") {
    using namespace magda;

    SECTION("Identify notes within clip length") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 0.0, 8.0);

        // Add notes at various positions
        MidiNote note1;
        note1.startBeat = 0.0;  // Within boundary
        note1.lengthBeats = 1.0;
        note1.noteNumber = 60;
        clip.midiNotes.push_back(note1);

        MidiNote note2;
        note2.startBeat = 3.0;  // Within boundary
        note2.lengthBeats = 0.5;
        note2.noteNumber = 64;
        clip.midiNotes.push_back(note2);

        MidiNote note3;
        note3.startBeat = 7.0;  // Within boundary (7 < 8 beats)
        note3.lengthBeats = 1.0;
        note3.noteNumber = 67;
        clip.midiNotes.push_back(note3);

        double clipLengthInBeats = clip.placement.lengthBeats;

        // Check which notes are within boundary
        REQUIRE(clip.midiNotes[0].startBeat < clipLengthInBeats);  // note1: YES
        REQUIRE(clip.midiNotes[1].startBeat < clipLengthInBeats);  // note2: YES
        REQUIRE(clip.midiNotes[2].startBeat < clipLengthInBeats);  // note3: YES (7 < 8)
    }

    SECTION("Notes at clip boundaries") {
        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 0.0, 4.0);

        MidiNote noteAtStart;
        noteAtStart.startBeat = 0.0;  // Exactly at start
        noteAtStart.lengthBeats = 1.0;
        noteAtStart.noteNumber = 60;
        clip.midiNotes.push_back(noteAtStart);

        MidiNote noteAtEnd;
        noteAtEnd.startBeat = 3.9;  // Just before end
        noteAtEnd.lengthBeats = 0.1;
        noteAtEnd.noteNumber = 64;
        clip.midiNotes.push_back(noteAtEnd);

        double clipLengthInBeats = clip.placement.lengthBeats;

        REQUIRE(clip.midiNotes[0].startBeat == 0.0);
        REQUIRE(clip.midiNotes[0].startBeat < clipLengthInBeats);
        REQUIRE(clip.midiNotes[1].startBeat < clipLengthInBeats);
    }
}

TEST_CASE("MidiNoteCommands - Observer pattern", "[midi][commands][observer]") {
    using namespace magda;

    SECTION("Commands trigger ClipManager notifications") {
        // Reset ClipManager state
        ClipManager::getInstance().shutdown();

        // Create a clip
        ClipId clipId = ClipManager::getInstance().createMidiClipBeats(1, 0.0, 16.0);
        REQUIRE(clipId != INVALID_CLIP_ID);

        // Add note via command
        auto cmd = std::make_unique<AddMidiNoteCommand>(clipId, 0.0, 60, 1.0, 100);
        UndoManager::getInstance().executeCommand(std::move(cmd));

        // Verify note was added
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);
        REQUIRE(clip->midiNotes.size() == 1);
        REQUIRE(clip->midiNotes[0].startBeat == 0.0);

        // Undo
        UndoManager::getInstance().undo();
        REQUIRE(clip->midiNotes.size() == 0);

        // Redo
        UndoManager::getInstance().redo();
        REQUIRE(clip->midiNotes.size() == 1);
    }
}

TEST_CASE("MidiClip - Real-world scenario", "[midi][clip][integration]") {
    using namespace magda;

    SECTION("Create clip at bar 1, resize from bar 5 to bar 4") {
        // This reproduces the exact bug that was fixed:
        // - Create clip bar 1-5 (4 bars)
        // - Add notes at beats 0,1,2,3
        // - Resize to bar 1-4 (3 bars)
        // - Notes should still play

        ClipInfo clip;
        clip.setMidiContent();
        setPlacementBeats(clip, 0.0, 16.0);

        // Add 4 notes at start
        for (int i = 0; i < 4; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i);
            note.lengthBeats = 1.0;
            note.noteNumber = 60;
            note.velocity = 100;
            clip.midiNotes.push_back(note);
        }

        // Verify initial state
        REQUIRE(clip.midiNotes.size() == 4);
        REQUIRE(clip.placement.startBeat == Catch::Approx(0.0));
        REQUIRE(clip.placement.lengthBeats == Catch::Approx(16.0));

        // Resize from bar 5 to bar 4
        setPlacementBeats(clip, clip.placement.startBeat, 12.0);

        // CRITICAL: Note positions must be unchanged
        REQUIRE(clip.midiNotes[0].startBeat == 0.0);
        REQUIRE(clip.midiNotes[1].startBeat == 1.0);
        REQUIRE(clip.midiNotes[2].startBeat == 2.0);
        REQUIRE(clip.midiNotes[3].startBeat == 3.0);

        // All notes still within the shortened clip (6 seconds = 12 beats)
        double clipLengthInBeats = clip.placement.lengthBeats;
        for (const auto& note : clip.midiNotes) {
            REQUIRE(note.startBeat < clipLengthInBeats);
        }

        // Clip start unchanged
        REQUIRE(clip.placement.startBeat == Catch::Approx(0.0));
    }
}

TEST_CASE("ClipOperations - MIDI visible range clips stale notes", "[midi][clip][boundary]") {
    using namespace magda;

    ClipInfo clip;
    clip.setMidiContent();
    clip.setPlacementBeats(0.0, 4.0);
    clip.deriveTimesFromBeats(120.0);

    MidiNote outside;
    outside.startBeat = 4.0;
    outside.lengthBeats = 1.0;
    REQUIRE_FALSE(ClipOperations::clipMidiNoteToVisibleRange(clip, outside));

    MidiNote partial;
    partial.startBeat = 3.5;
    partial.lengthBeats = 1.0;
    REQUIRE(ClipOperations::clipMidiNoteToVisibleRange(clip, partial));
    REQUIRE(partial.startBeat == Catch::Approx(3.5));
    REQUIRE(partial.lengthBeats == Catch::Approx(0.5));

    clip.midiTrimOffset = 2.0;
    MidiNote trimmedOut;
    trimmedOut.startBeat = 1.0;
    trimmedOut.lengthBeats = 0.5;
    REQUIRE_FALSE(ClipOperations::clipMidiNoteToVisibleRange(clip, trimmedOut));

    MidiNote trimmedIn;
    trimmedIn.startBeat = 2.0;
    trimmedIn.lengthBeats = 1.0;
    REQUIRE(ClipOperations::clipMidiNoteToVisibleRange(clip, trimmedIn));
    REQUIRE(trimmedIn.startBeat == Catch::Approx(2.0));
    REQUIRE(trimmedIn.lengthBeats == Catch::Approx(1.0));
}

TEST_CASE("ClipManager - addMidiNote rejects notes beyond MIDI clip extent",
          "[midi][clip][boundary][manager]") {
    using namespace magda;

    ClipManager::getInstance().clearAllClips();
    ClipId clipId = ClipManager::getInstance().createMidiClipBeats(1, 0.0, 4.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    MidiNote outside;
    outside.startBeat = 4.0;
    outside.lengthBeats = 1.0;
    outside.noteNumber = 60;
    REQUIRE_FALSE(ClipManager::getInstance().addMidiNote(clipId, outside));

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.empty());

    MidiNote partial;
    partial.startBeat = 3.5;
    partial.lengthBeats = 1.0;
    partial.noteNumber = 62;
    REQUIRE(ClipManager::getInstance().addMidiNote(clipId, partial));
    REQUIRE(clip->midiNotes.size() == 1);
    REQUIRE(clip->midiNotes[0].startBeat == Catch::Approx(3.5));
    REQUIRE(clip->midiNotes[0].lengthBeats == Catch::Approx(0.5));
}

TEST_CASE("MIDI note commands ignore invalid delete indices on undo", "[midi][undo][commands]") {
    using namespace magda;

    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();
    ClipId clipId = cm.createMidiClipBeats(1, 0.0, 4.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    MidiNote note{60, 100, 1.0, 0.5};
    REQUIRE(cm.addMidiNote(clipId, note));

    DeleteMidiNoteCommand cmd(clipId, 99);
    cmd.execute();
    cmd.undo();

    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 1);
    REQUIRE(clip->midiNotes[0].noteNumber == 60);
    REQUIRE(clip->midiNotes[0].startBeat == Catch::Approx(1.0));
}

TEST_CASE("MIDI batch move undo restores valid notes when invalid indices are skipped",
          "[midi][undo][commands]") {
    using namespace magda;

    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();
    ClipId clipId = cm.createMidiClipBeats(1, 0.0, 8.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    REQUIRE(cm.addMidiNote(clipId, {60, 100, 0.0, 0.5}));
    REQUIRE(cm.addMidiNote(clipId, {61, 100, 1.0, 0.5}));
    REQUIRE(cm.addMidiNote(clipId, {62, 100, 2.0, 0.5}));

    std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves{
        {99, 6.0, 80},
        {1, 3.0, 70},
    };
    MoveMultipleMidiNotesCommand cmd(clipId, std::move(moves));
    cmd.execute();

    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(3.0));
    REQUIRE(clip->midiNotes[1].noteNumber == 70);

    cmd.undo();

    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(1.0));
    REQUIRE(clip->midiNotes[1].noteNumber == 61);
}

TEST_CASE("MIDI quantize undo restores valid notes when invalid indices are skipped",
          "[midi][undo][commands]") {
    using namespace magda;

    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();
    ClipId clipId = cm.createMidiClipBeats(1, 0.0, 8.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    REQUIRE(cm.addMidiNote(clipId, {60, 100, 0.0, 0.5}));
    REQUIRE(cm.addMidiNote(clipId, {61, 100, 1.2, 0.7}));

    QuantizeMidiNotesCommand cmd(clipId, {99, 1}, 1.0, QuantizeMode::StartAndLength);
    cmd.execute();

    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(1.0));
    REQUIRE(clip->midiNotes[1].lengthBeats == Catch::Approx(1.0));

    cmd.undo();

    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(1.2));
    REQUIRE(clip->midiNotes[1].lengthBeats == Catch::Approx(0.7));
}

TEST_CASE("BendNoteTimingCommand bends raw MIDI note starts and undoes exactly",
          "[midi][commands][timebend][trim]") {
    using namespace magda;

    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();
    ClipId clipId = cm.createMidiClipBeats(1, 0.0, 4.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);

    REQUIRE(cm.addMidiNote(clipId, {60, 100, 0.5, 1.0}));
    REQUIRE(cm.addMidiNote(clipId, {61, 100, 1.5, 0.5}));
    REQUIRE(cm.addMidiNote(clipId, {62, 100, 3.0, 0.5}));
    clip->midiTrimOffset = 1.0;

    BendNoteTimingCommand cmd(clipId, {0, 1, 2}, 0.25f, 0.0f, 1, 0.0f, 64, true);
    cmd.execute();

    REQUIRE(clip->midiNotes[0].startBeat == Catch::Approx(1.0));
    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(2.0));
    REQUIRE(clip->midiNotes[2].startBeat == Catch::Approx(3.0));

    cmd.undo();

    REQUIRE(clip->midiNotes[0].startBeat == Catch::Approx(0.5));
    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(1.5));
    REQUIRE(clip->midiNotes[2].startBeat == Catch::Approx(3.0));
}

TEST_CASE("BendNoteTimingCommand preserves non-zero selected span", "[midi][commands][timebend]") {
    using namespace magda;

    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();
    ClipId clipId = cm.createMidiClipBeats(1, 0.0, 8.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    REQUIRE(cm.addMidiNote(clipId, {60, 100, 2.0, 0.5}));
    REQUIRE(cm.addMidiNote(clipId, {61, 100, 4.0, 0.5}));
    REQUIRE(cm.addMidiNote(clipId, {62, 100, 6.0, 0.5}));

    BendNoteTimingCommand cmd(clipId, {0, 1, 2}, 0.25f, 0.0f, 1, 0.0f, 64, true);
    cmd.execute();

    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes[0].startBeat == Catch::Approx(2.0));
    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(5.0));
    REQUIRE(clip->midiNotes[2].startBeat == Catch::Approx(6.0));
}

TEST_CASE("SliceMidiNotesCommand reports every new slice index", "[midi][commands][slice]") {
    using namespace magda;

    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();
    ClipId clipId = cm.createMidiClipBeats(1, 0.0, 8.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    REQUIRE(cm.addMidiNote(clipId, {60, 100, 0.0, 1.0}));
    REQUIRE(cm.addMidiNote(clipId, {61, 100, 2.0, 1.0}));
    REQUIRE(cm.addMidiNote(clipId, {62, 100, 4.0, 1.0}));

    SliceMidiNotesCommand cmd(clipId, {0, 2}, 4);
    cmd.execute();

    const auto& sliced = cmd.getSlicedNoteIndices();
    REQUIRE(sliced == std::vector<size_t>{0, 1, 2, 3, 5, 6, 7, 8});

    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 9);
    REQUIRE(clip->midiNotes[4].noteNumber == 61);

    cmd.undo();
    REQUIRE(cmd.getSlicedNoteIndices().empty());
    REQUIRE(clip->midiNotes.size() == 3);
}

TEST_CASE("MoveMidiNoteBetweenClipsCommand keeps source note when destination insert cannot run",
          "[midi][undo][commands]") {
    using namespace magda;

    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();
    ClipId sourceId = cm.createMidiClipBeats(1, 0.0, 4.0);
    ClipId destId = cm.createMidiClipBeats(1, 4.0, 4.0);
    REQUIRE(sourceId != INVALID_CLIP_ID);
    REQUIRE(destId != INVALID_CLIP_ID);

    REQUIRE(cm.addMidiNote(sourceId, {60, 100, 1.0, 0.5}));
    auto* dest = cm.getClip(destId);
    REQUIRE(dest != nullptr);
    dest->setPlacementBeats(4.0, 0.0);

    MoveMidiNoteBetweenClipsCommand cmd(sourceId, 0, destId, 0.0, 62);
    cmd.execute();
    cmd.undo();

    auto* source = cm.getClip(sourceId);
    REQUIRE(source != nullptr);
    REQUIRE(source->midiNotes.size() == 1);
    REQUIRE(source->midiNotes[0].noteNumber == 60);
    REQUIRE(dest->midiNotes.empty());
}
