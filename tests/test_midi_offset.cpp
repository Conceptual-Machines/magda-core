#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

/**
 * Tests for MIDI content offset functionality
 *
 * These tests verify:
 * - Offset shifts the visible/playable portion of notes
 * - Split operation sets offset correctly for non-destructive trim
 * - Notes before offset are handled correctly
 * - Arrangement preview accounts for offset
 */

TEST_CASE("MidiOffset - Basic offset behavior", "[midi][offset]") {
    using namespace magda;

    SECTION("Offset shifts visible note window") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 4.0;  // 4 seconds = 8 beats at 120 BPM
        clip.midiOffset = 0.0;

        // Add notes at beats 0, 1, 2, 3, 4, 5
        for (int i = 0; i < 6; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i);
            note.lengthBeats = 0.5;
            note.noteNumber = 60;
            note.velocity = 100;
            clip.midiNotes.push_back(note);
        }

        REQUIRE(clip.midiNotes.size() == 6);

        // With offset = 0, all notes within clip length are visible
        double clipLengthInBeats = 8.0;  // 4 seconds = 8 beats at 120 BPM
        int visibleCount = 0;
        for (const auto& note : clip.midiNotes) {
            if (note.startBeat >= clip.midiOffset &&
                note.startBeat < clip.midiOffset + clipLengthInBeats) {
                visibleCount++;
            }
        }
        REQUIRE(visibleCount == 6);  // All notes visible

        // Set offset = 2, now only notes 2-5 should be in visible range
        clip.midiOffset = 2.0;
        visibleCount = 0;
        for (const auto& note : clip.midiNotes) {
            if (note.startBeat >= clip.midiOffset &&
                note.startBeat < clip.midiOffset + clipLengthInBeats) {
                visibleCount++;
            }
        }
        REQUIRE(visibleCount == 4);  // Notes at beats 2, 3, 4, 5
    }

    SECTION("Offset doesn't modify note data") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.midiOffset = 0.0;

        MidiNote note;
        note.startBeat = 2.5;
        note.lengthBeats = 1.0;
        note.noteNumber = 64;
        note.velocity = 80;
        clip.midiNotes.push_back(note);

        double originalBeat = clip.midiNotes[0].startBeat;

        // Change offset
        clip.midiOffset = 3.0;

        // Note position unchanged
        REQUIRE(clip.midiNotes[0].startBeat == originalBeat);
        REQUIRE(clip.midiNotes[0].startBeat == 2.5);
    }
}

TEST_CASE("MidiOffset - Split operation (destructive)", "[midi][offset][split]") {
    using namespace magda;

    SECTION("Destructive split partitions notes between clips") {
        // Reset ClipManager state
        ClipManager::getInstance().shutdown();

        // Create clip at timeline position 0-4 seconds (0-8 beats)
        ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0);
        REQUIRE(clipId != INVALID_CLIP_ID);

        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        // Add notes at beats 0, 2, 4, 6
        for (int i = 0; i < 4; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i * 2);
            note.lengthBeats = 1.0;
            note.noteNumber = 60 + i;
            note.velocity = 100;
            clip->midiNotes.push_back(note);
        }

        REQUIRE(clip->midiNotes.size() == 4);
        REQUIRE(clip->midiOffset == 0.0);

        // Split at 2 seconds (4 beats at 120 BPM)
        ClipId rightClipId = ClipManager::getInstance().splitClip(clipId, 2.0);
        REQUIRE(rightClipId != INVALID_CLIP_ID);

        const auto* leftClip = ClipManager::getInstance().getClip(clipId);
        const auto* rightClip = ClipManager::getInstance().getClip(rightClipId);

        REQUIRE(leftClip != nullptr);
        REQUIRE(rightClip != nullptr);

        // Left clip: notes before beat 4 (beats 0, 2)
        REQUIRE(leftClip->length == 2.0);
        REQUIRE(leftClip->midiNotes.size() == 2);
        REQUIRE(leftClip->midiNotes[0].startBeat == Catch::Approx(0.0));
        REQUIRE(leftClip->midiNotes[1].startBeat == Catch::Approx(2.0));

        // Right clip: notes at/after beat 4 (beats 4, 6) adjusted by -4 -> (0, 2)
        REQUIRE(rightClip->length == 2.0);
        REQUIRE(rightClip->midiNotes.size() == 2);
        REQUIRE(rightClip->midiNotes[0].startBeat == Catch::Approx(0.0));
        REQUIRE(rightClip->midiNotes[1].startBeat == Catch::Approx(2.0));
    }
}

TEST_CASE("MidiOffset - Display position calculation", "[midi][offset][display]") {
    using namespace magda;

    SECTION("Notes shift left by offset amount in display") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 10.0;  // Timeline position
        clip.length = 4.0;
        clip.midiOffset = 2.0;  // Skip first 2 beats

        MidiNote note;
        note.startBeat = 3.0;  // Relative to clip start
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        // In absolute mode, display position should be:
        // clipStartBeats + note.startBeat - clip.midiOffset
        // Assuming clipStartBeats = 20 (10 seconds * 2 beats/second)
        double clipStartBeats = 20.0;
        double displayBeat = clipStartBeats + note.startBeat - clip.midiOffset;

        // 20 + 3 - 2 = 21
        REQUIRE(displayBeat == 21.0);
    }

    SECTION("Note before offset should be identified") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.midiOffset = 3.0;

        // Note at beat 2 (before offset at 3)
        MidiNote noteBeforeOffset;
        noteBeforeOffset.startBeat = 2.0;
        noteBeforeOffset.lengthBeats = 0.5;
        noteBeforeOffset.noteNumber = 60;
        clip.midiNotes.push_back(noteBeforeOffset);

        // Note at beat 4 (after offset at 3)
        MidiNote noteAfterOffset;
        noteAfterOffset.startBeat = 4.0;
        noteAfterOffset.lengthBeats = 0.5;
        noteAfterOffset.noteNumber = 64;
        clip.midiNotes.push_back(noteAfterOffset);

        // Check which notes are before offset
        bool note0BeforeOffset = clip.midiNotes[0].startBeat < clip.midiOffset;
        bool note1BeforeOffset = clip.midiNotes[1].startBeat < clip.midiOffset;

        REQUIRE(note0BeforeOffset == true);   // Note at 2 < offset 3
        REQUIRE(note1BeforeOffset == false);  // Note at 4 >= offset 3
    }
}

TEST_CASE("MidiOffset - Arrangement preview with offset", "[midi][offset][preview]") {
    using namespace magda;

    SECTION("Preview shows only visible notes within offset range") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 2.0;  // 2 seconds = 4 beats at 120 BPM
        clip.midiOffset = 2.0;

        // Add notes at beats 0, 1, 2, 3, 4, 5
        for (int i = 0; i < 6; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i);
            note.lengthBeats = 0.5;
            note.noteNumber = 60 + i;
            note.velocity = 100;
            clip.midiNotes.push_back(note);
        }

        double clipLengthInBeats = 4.0;  // 2 seconds = 4 beats

        // Calculate visible notes for preview
        std::vector<int> visibleIndices;
        for (size_t i = 0; i < clip.midiNotes.size(); i++) {
            const auto& note = clip.midiNotes[i];
            double displayStart = note.startBeat - clip.midiOffset;
            double displayEnd = displayStart + note.lengthBeats;

            // Note is visible if it overlaps [0, clipLengthInBeats]
            if (!(displayEnd <= 0 || displayStart >= clipLengthInBeats)) {
                visibleIndices.push_back(i);
            }
        }

        // With offset=2, visible range is [2, 6) in original coordinates
        // which maps to [0, 4) in display coordinates
        // Notes 2, 3, 4, 5 should be visible
        REQUIRE(visibleIndices.size() == 4);
        REQUIRE(visibleIndices[0] == 2);
        REQUIRE(visibleIndices[1] == 3);
        REQUIRE(visibleIndices[2] == 4);
        REQUIRE(visibleIndices[3] == 5);
    }

    SECTION("Split clips show different content in preview") {
        // Simulate split scenario: L, C, R clips from same source
        ClipInfo sourceClip;
        sourceClip.type = ClipType::MIDI;
        sourceClip.startTime = 0.0;
        sourceClip.length = 6.0;  // 6 seconds = 12 beats
        sourceClip.midiOffset = 0.0;

        // Add notes at beats 0, 2, 4, 6, 8, 10
        for (int i = 0; i < 6; i++) {
            MidiNote note;
            note.startBeat = static_cast<double>(i * 2);
            note.lengthBeats = 1.0;
            note.noteNumber = 60 + i;  // C, D, E, F, G, A
            note.velocity = 100;
            sourceClip.midiNotes.push_back(note);
        }

        // Create L clip (0-2 seconds, offset 0)
        ClipInfo leftClip = sourceClip;
        leftClip.length = 2.0;  // 4 beats
        leftClip.midiOffset = 0.0;

        // Create C clip (2-4 seconds, offset 4)
        ClipInfo centerClip = sourceClip;
        centerClip.startTime = 2.0;
        centerClip.length = 2.0;  // 4 beats
        centerClip.midiOffset = 4.0;

        // Create R clip (4-6 seconds, offset 8)
        ClipInfo rightClip = sourceClip;
        rightClip.startTime = 4.0;
        rightClip.length = 2.0;  // 4 beats
        rightClip.midiOffset = 8.0;

        // Find which notes are visible in each clip's preview
        auto getVisibleNoteIndices = [](const ClipInfo& clip) {
            std::vector<int> indices;
            double clipLengthInBeats = clip.length * 2.0;  // 120 BPM
            for (size_t i = 0; i < clip.midiNotes.size(); i++) {
                const auto& note = clip.midiNotes[i];
                double displayStart = note.startBeat - clip.midiOffset;
                double displayEnd = displayStart + note.lengthBeats;
                if (!(displayEnd <= 0 || displayStart >= clipLengthInBeats)) {
                    indices.push_back(i);
                }
            }
            return indices;
        };

        auto leftVisible = getVisibleNoteIndices(leftClip);
        auto centerVisible = getVisibleNoteIndices(centerClip);
        auto rightVisible = getVisibleNoteIndices(rightClip);

        // L clip: shows notes 0, 1 (C, D)
        REQUIRE(leftVisible.size() == 2);
        REQUIRE(leftVisible[0] == 0);
        REQUIRE(leftVisible[1] == 1);

        // C clip: shows notes 2, 3 (E, F)
        REQUIRE(centerVisible.size() == 2);
        REQUIRE(centerVisible[0] == 2);
        REQUIRE(centerVisible[1] == 3);

        // R clip: shows notes 4, 5 (G, A)
        REQUIRE(rightVisible.size() == 2);
        REQUIRE(rightVisible[0] == 4);
        REQUIRE(rightVisible[1] == 5);

        // Verify they show DIFFERENT notes (different pitch numbers)
        int leftFirstNote = leftClip.midiNotes[leftVisible[0]].noteNumber;
        int centerFirstNote = centerClip.midiNotes[centerVisible[0]].noteNumber;
        int rightFirstNote = rightClip.midiNotes[rightVisible[0]].noteNumber;

        REQUIRE(leftFirstNote == 60);    // C
        REQUIRE(centerFirstNote == 62);  // E
        REQUIRE(rightFirstNote == 64);   // G

        // They should all be different
        REQUIRE(leftFirstNote != centerFirstNote);
        REQUIRE(centerFirstNote != rightFirstNote);
        REQUIRE(leftFirstNote != rightFirstNote);
    }
}

TEST_CASE("MidiOffset - Edge cases", "[midi][offset][edge]") {
    using namespace magda;

    SECTION("Offset equals clip length shows no notes") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 2.0;      // 4 beats
        clip.midiOffset = 4.0;  // Same as clip length in beats

        MidiNote note;
        note.startBeat = 2.0;
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        clip.midiNotes.push_back(note);

        double clipLengthInBeats = 4.0;

        // No notes should be visible (all before visible range)
        int visibleCount = 0;
        for (const auto& n : clip.midiNotes) {
            if (n.startBeat >= clip.midiOffset &&
                n.startBeat < clip.midiOffset + clipLengthInBeats) {
                visibleCount++;
            }
        }

        REQUIRE(visibleCount == 0);
    }

    SECTION("Negative offset is clamped to zero") {
        // When setting offset via ClipManager, it should be clamped
        ClipManager::getInstance().shutdown();

        ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0);
        REQUIRE(clipId != INVALID_CLIP_ID);

        // Try to set negative offset
        ClipManager::getInstance().setClipMidiOffset(clipId, -2.0);

        const auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        // Should be clamped to 0
        REQUIRE(clip->midiOffset == 0.0);
    }

    SECTION("Partial note visibility at offset boundary") {
        ClipInfo clip;
        clip.type = ClipType::MIDI;
        clip.startTime = 0.0;
        clip.length = 2.0;  // 4 beats
        clip.midiOffset = 2.0;

        // Note that starts before offset but extends into visible range
        MidiNote note;
        note.startBeat = 1.5;    // Starts before offset (2.0)
        note.lengthBeats = 1.0;  // Ends at 2.5 (after offset)
        note.noteNumber = 60;
        clip.midiNotes.push_back(note);

        double clipLengthInBeats = 4.0;
        double displayStart = note.startBeat - clip.midiOffset;  // 1.5 - 2.0 = -0.5
        double displayEnd = displayStart + note.lengthBeats;     // -0.5 + 1.0 = 0.5

        // Note should be partially visible (clips to [0, clipLengthInBeats])
        double visibleStart = std::max(0.0, displayStart);
        double visibleEnd = std::min(clipLengthInBeats, displayEnd);

        REQUIRE(visibleStart == 0.0);
        REQUIRE(visibleEnd == 0.5);
        REQUIRE(visibleEnd > visibleStart);  // Note is visible
    }
}
