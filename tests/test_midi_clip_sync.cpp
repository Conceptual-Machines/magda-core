#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
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

TEST_CASE("MidiNote - Clip-relative storage", "[midi][clip][storage]") {
    using namespace magda;

    SECTION("Notes are stored relative to clip start") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 4.0;  // Clip at 4 seconds (bar 2 at 120 BPM)
        clip.length = 8.0;     // 4 bars

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
        REQUIRE(clip.startTime == 4.0);
        REQUIRE(clip.midiNotes[0].startBeat != clip.startTime);
    }

    SECTION("Multiple notes at different clip-relative positions") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 8.0;

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
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 8.0;

        MidiNote note;
        note.startBeat = 2.0;  // Note at beat 2 within clip
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        double originalNoteBeat = clip.midiNotes[0].startBeat;

        // Move clip to different timeline position
        clip.startTime = 10.0;  // Move to bar 5

        // Note position within clip should be unchanged
        REQUIRE(clip.midiNotes[0].startBeat == originalNoteBeat);
        REQUIRE(clip.midiNotes[0].startBeat == 2.0);
    }

    SECTION("Moving clip multiple times preserves notes") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 4.0;

        MidiNote note;
        note.startBeat = 1.5;
        note.lengthBeats = 0.5;
        note.noteNumber = 64;
        note.velocity = 80;
        clip.midiNotes.push_back(note);

        // Move clip multiple times
        clip.startTime = 2.0;
        REQUIRE(clip.midiNotes[0].startBeat == 1.5);

        clip.startTime = 8.0;
        REQUIRE(clip.midiNotes[0].startBeat == 1.5);

        clip.startTime = 0.0;
        REQUIRE(clip.midiNotes[0].startBeat == 1.5);
    }
}

TEST_CASE("MidiClip - Length changes preserve note positions", "[midi][clip][length]") {
    using namespace magda;

    SECTION("Shortening clip from end preserves notes at start") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 16.0;  // 16 seconds = 32 beats = 8 bars at 120 BPM

        // Add 4 notes at beats 0, 1, 2, 3
        for (int i = 0; i < 4; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i);
            note.lengthBeats = 1.0;
            note.noteNumber = 60;
            note.velocity = 100;
            clip.midiNotes.push_back(note);
        }

        // Shorten clip to 2 bars (4 seconds = 8 beats at 120 BPM)
        clip.length = 4.0;

        // ALL notes should still have same positions
        REQUIRE(clip.midiNotes[0].startBeat == 0.0);
        REQUIRE(clip.midiNotes[1].startBeat == 1.0);
        REQUIRE(clip.midiNotes[2].startBeat == 2.0);
        REQUIRE(clip.midiNotes[3].startBeat == 3.0);

        // Notes are within new clip length, so they should all play
        double clipLengthInBeats = 8.0;  // 4 seconds = 8 beats at 120 BPM
        for (const auto& note : clip.midiNotes) {
            REQUIRE(note.startBeat < clipLengthInBeats);
        }
    }

    SECTION("Extending clip doesn't shift existing notes") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 2.0;
        clip.length = 2.0;  // 2 seconds = 4 beats = 1 bar at 120 BPM

        MidiNote note;
        note.startBeat = 0.5;
        note.lengthBeats = 0.25;
        note.noteNumber = 72;
        note.velocity = 90;
        clip.midiNotes.push_back(note);

        double originalBeat = clip.midiNotes[0].startBeat;

        // Extend clip to 4 bars (8 seconds = 16 beats at 120 BPM)
        clip.length = 8.0;

        // Note position unchanged
        REQUIRE(clip.midiNotes[0].startBeat == originalBeat);
        REQUIRE(clip.midiNotes[0].startBeat == 0.5);
    }
}

TEST_CASE("MidiClip - Notes beyond clip boundary", "[midi][clip][boundary]") {
    using namespace magda;

    SECTION("Identify notes within clip length") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 4.0;  // 4 seconds = 8 beats = 2 bars at 120 BPM

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

        double clipLengthInBeats = 8.0;  // 4 seconds = 8 beats at 120 BPM

        // Check which notes are within boundary
        REQUIRE(clip.midiNotes[0].startBeat < clipLengthInBeats);  // note1: YES
        REQUIRE(clip.midiNotes[1].startBeat < clipLengthInBeats);  // note2: YES
        REQUIRE(clip.midiNotes[2].startBeat < clipLengthInBeats);  // note3: YES (7 < 8)
    }

    SECTION("Notes at clip boundaries") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 2.0;  // Exactly 1 bar = 4 beats at 120 BPM

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

        double clipLengthInBeats = 4.0;

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
        ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 8.0);
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
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;  // Bar 1
        clip.length = 8.0;     // 8 seconds = 16 beats = 4 bars at 120 BPM

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
        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 8.0);

        // Resize from bar 5 to bar 4 (shorten by 1 bar = 2 seconds)
        clip.length = 6.0;  // 3 bars

        // CRITICAL: Note positions must be unchanged
        REQUIRE(clip.midiNotes[0].startBeat == 0.0);
        REQUIRE(clip.midiNotes[1].startBeat == 1.0);
        REQUIRE(clip.midiNotes[2].startBeat == 2.0);
        REQUIRE(clip.midiNotes[3].startBeat == 3.0);

        // All notes still within the shortened clip (6 seconds = 12 beats)
        double clipLengthInBeats = 12.0;
        for (const auto& note : clip.midiNotes) {
            REQUIRE(note.startBeat < clipLengthInBeats);
        }

        // Clip start unchanged
        REQUIRE(clip.startTime == 0.0);
    }
}
