#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipCommands.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"

/**
 * Tests for MIDI clip splitting operations
 *
 * These tests verify:
 * - Non-destructive MIDI clip splitting
 * - Correct note position adjustment after split
 * - Sequential splits maintain correct state
 * - Notes before split point are retained (negative positions)
 * - Both clips keep all original notes for non-destructive editing
 *
 * Bug fixed: After split, all clips showed notes starting from the first note.
 * Fix: Right clip notes are adjusted relative to split point, negative positions
 * retained for non-destructive editing.
 */

using namespace magda;

// Helper to create a MIDI clip with notes
static ClipId createMidiClipWithNotes(TrackId trackId, double startTime, double length,
                                      const std::vector<double>& noteBeatPositions) {
    auto& clipManager = ClipManager::getInstance();
    ClipId clipId = clipManager.createMidiClip(trackId, startTime, length, ClipView::Arrangement);

    auto* clip = clipManager.getClip(clipId);
    if (clip) {
        for (double beatPos : noteBeatPositions) {
            MidiNote note;
            note.noteNumber = 60;  // Middle C
            note.startBeat = beatPos;
            note.lengthBeats = 1.0;
            note.velocity = 100;
            clip->midiNotes.push_back(note);
        }
    }

    return clipId;
}

// ============================================================================
// Basic MIDI Clip Split
// ============================================================================

TEST_CASE("MIDI clip split - basic operation", "[midi][clip][split]") {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    // Setup
    clipManager.clearAllClips();
    trackManager.clearAllTracks();
    TrackId trackId = trackManager.createTrack("Test Track", TrackType::MIDI);

    SECTION("Split clip with notes at different positions") {
        // Create clip: 0-4 seconds (8 beats at 120 BPM)
        // Notes at beats: 0, 2, 4, 6
        ClipId clipId = createMidiClipWithNotes(trackId, 0.0, 4.0, {0.0, 2.0, 4.0, 6.0});

        // Split at 2 seconds (4 beats)
        SplitClipCommand splitCmd(clipId, 2.0);
        REQUIRE(splitCmd.canExecute());
        splitCmd.execute();

        ClipId rightClipId = splitCmd.getRightClipId();
        REQUIRE(rightClipId != INVALID_CLIP_ID);

        auto* leftClip = clipManager.getClip(clipId);
        auto* rightClip = clipManager.getClip(rightClipId);

        REQUIRE(leftClip != nullptr);
        REQUIRE(rightClip != nullptr);

        // Left clip: 0-2 seconds
        REQUIRE(leftClip->startTime == Catch::Approx(0.0));
        REQUIRE(leftClip->length == Catch::Approx(2.0));
        REQUIRE(leftClip->midiOffset == Catch::Approx(0.0));

        // Right clip: 2-4 seconds
        REQUIRE(rightClip->startTime == Catch::Approx(2.0));
        REQUIRE(rightClip->length == Catch::Approx(2.0));
        REQUIRE(rightClip->midiOffset == Catch::Approx(0.0));

        // Both clips keep all notes (non-destructive)
        REQUIRE(leftClip->midiNotes.size() == 4);
        REQUIRE(rightClip->midiNotes.size() == 4);
    }
}

// ============================================================================
// Note Position Adjustment
// ============================================================================

TEST_CASE("MIDI clip split - note position adjustment", "[midi][clip][split][notes]") {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    // Setup
    clipManager.clearAllClips();
    trackManager.clearAllTracks();
    TrackId trackId = trackManager.createTrack("Test Track", TrackType::MIDI);

    SECTION("Right clip notes adjusted relative to split point") {
        // Create clip with notes at beats: 1, 3, 5, 7
        ClipId clipId = createMidiClipWithNotes(trackId, 0.0, 4.0, {1.0, 3.0, 5.0, 7.0});

        // Split at 2 seconds (4 beats at 120 BPM)
        SplitClipCommand splitCmd(clipId, 2.0);
        splitCmd.execute();

        auto* leftClip = clipManager.getClip(clipId);
        auto* rightClip = clipManager.getClip(splitCmd.getRightClipId());

        // Left clip: notes unchanged (at beats 1, 3, 5, 7)
        REQUIRE(leftClip->midiNotes[0].startBeat == Catch::Approx(1.0));
        REQUIRE(leftClip->midiNotes[1].startBeat == Catch::Approx(3.0));
        REQUIRE(leftClip->midiNotes[2].startBeat == Catch::Approx(5.0));
        REQUIRE(leftClip->midiNotes[3].startBeat == Catch::Approx(7.0));

        // Right clip: notes adjusted by -4 beats (split point)
        // Original: [1, 3, 5, 7] -> Adjusted: [-3, -1, 1, 3]
        REQUIRE(rightClip->midiNotes[0].startBeat == Catch::Approx(-3.0));
        REQUIRE(rightClip->midiNotes[1].startBeat == Catch::Approx(-1.0));
        REQUIRE(rightClip->midiNotes[2].startBeat == Catch::Approx(1.0));
        REQUIRE(rightClip->midiNotes[3].startBeat == Catch::Approx(3.0));
    }

    SECTION("Notes with negative positions are retained (non-destructive)") {
        ClipId clipId = createMidiClipWithNotes(trackId, 0.0, 4.0, {0.0, 4.0, 8.0});

        SplitClipCommand splitCmd(clipId, 2.0);
        splitCmd.execute();

        auto* rightClip = clipManager.getClip(splitCmd.getRightClipId());

        // Notes adjusted: [0, 4, 8] -> [-4, 0, 4]
        // Note at -4 is retained (non-destructive) but won't be visible
        REQUIRE(rightClip->midiNotes.size() == 3);
        REQUIRE(rightClip->midiNotes[0].startBeat == Catch::Approx(-4.0));  // Retained!
        REQUIRE(rightClip->midiNotes[1].startBeat == Catch::Approx(0.0));
        REQUIRE(rightClip->midiNotes[2].startBeat == Catch::Approx(4.0));
    }
}

// ============================================================================
// Sequential Splits
// ============================================================================

TEST_CASE("MIDI clip split - sequential operations", "[midi][clip][split][sequential]") {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    // Setup
    clipManager.clearAllClips();
    trackManager.clearAllTracks();
    TrackId trackId = trackManager.createTrack("Test Track", TrackType::MIDI);

    SECTION("Multiple splits maintain correct note positions") {
        // Create clip: 0-8 seconds (16 beats)
        // Notes at beats: 0, 4, 8, 12
        ClipId clipId = createMidiClipWithNotes(trackId, 0.0, 8.0, {0.0, 4.0, 8.0, 12.0});

        // First split at 2 seconds (4 beats)
        SplitClipCommand split1(clipId, 2.0);
        split1.execute();
        ClipId clip2 = split1.getRightClipId();

        // Second split at 4 seconds (8 beats from original start)
        SplitClipCommand split2(clip2, 4.0);
        split2.execute();
        ClipId clip3 = split2.getRightClipId();

        // Third split at 6 seconds (12 beats from original start)
        SplitClipCommand split3(clip3, 6.0);
        split3.execute();
        ClipId clip4 = split3.getRightClipId();

        auto* clip1Ptr = clipManager.getClip(clipId);
        auto* clip2Ptr = clipManager.getClip(clip2);
        auto* clip3Ptr = clipManager.getClip(clip3);
        auto* clip4Ptr = clipManager.getClip(clip4);

        REQUIRE(clip1Ptr != nullptr);
        REQUIRE(clip2Ptr != nullptr);
        REQUIRE(clip3Ptr != nullptr);
        REQUIRE(clip4Ptr != nullptr);

        // Verify clip boundaries
        REQUIRE(clip1Ptr->startTime == Catch::Approx(0.0));
        REQUIRE(clip1Ptr->length == Catch::Approx(2.0));

        REQUIRE(clip2Ptr->startTime == Catch::Approx(2.0));
        REQUIRE(clip2Ptr->length == Catch::Approx(2.0));

        REQUIRE(clip3Ptr->startTime == Catch::Approx(4.0));
        REQUIRE(clip3Ptr->length == Catch::Approx(2.0));

        REQUIRE(clip4Ptr->startTime == Catch::Approx(6.0));
        REQUIRE(clip4Ptr->length == Catch::Approx(2.0));

        // Verify note positions in each clip
        // Clip1: notes at [0, 4, 8, 12] (unchanged)
        REQUIRE(clip1Ptr->midiNotes[0].startBeat == Catch::Approx(0.0));

        // Clip2: notes adjusted from clip2's perspective (offset by -4)
        // Original clip2 had midiOffset=4, notes at [0, 4, 8, 12]
        // After adjustment: [-4, 0, 4, 8]
        REQUIRE(clip2Ptr->midiNotes[0].startBeat == Catch::Approx(-4.0));
        REQUIRE(clip2Ptr->midiNotes[1].startBeat == Catch::Approx(0.0));

        // Clip3: further adjusted from previous split
        REQUIRE(clip3Ptr->midiNotes[1].startBeat == Catch::Approx(-4.0));
        REQUIRE(clip3Ptr->midiNotes[2].startBeat == Catch::Approx(0.0));

        // Clip4: final clip
        REQUIRE(clip4Ptr->midiNotes[3].startBeat == Catch::Approx(0.0));
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("MIDI clip split - edge cases", "[midi][clip][split][edge]") {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    // Setup
    clipManager.clearAllClips();
    trackManager.clearAllTracks();
    TrackId trackId = trackManager.createTrack("Test Track", TrackType::MIDI);

    SECTION("Split with existing midiOffset") {
        ClipId clipId = createMidiClipWithNotes(trackId, 0.0, 4.0, {2.0, 4.0, 6.0});

        auto* clip = clipManager.getClip(clipId);
        clip->midiOffset = 2.0;  // Clip already has offset (from previous operation)

        // Split at 2 seconds (4 beats), but clip has offset of 2 beats
        // Absolute split = offset + splitBeats = 2 + 4 = 6 beats
        SplitClipCommand splitCmd(clipId, 2.0);
        splitCmd.execute();

        auto* rightClip = clipManager.getClip(splitCmd.getRightClipId());

        // Right clip notes adjusted by -6 (absolute split point)
        // Original: [2, 4, 6] -> Adjusted: [-4, -2, 0]
        REQUIRE(rightClip->midiNotes[0].startBeat == Catch::Approx(-4.0));
        REQUIRE(rightClip->midiNotes[1].startBeat == Catch::Approx(-2.0));
        REQUIRE(rightClip->midiNotes[2].startBeat == Catch::Approx(0.0));
        REQUIRE(rightClip->midiOffset == Catch::Approx(0.0));
    }

    SECTION("Split empty MIDI clip") {
        ClipId clipId = clipManager.createMidiClip(trackId, 0.0, 4.0, ClipView::Arrangement);

        SplitClipCommand splitCmd(clipId, 2.0);
        REQUIRE(splitCmd.canExecute());
        splitCmd.execute();

        ClipId rightClipId = splitCmd.getRightClipId();
        REQUIRE(rightClipId != INVALID_CLIP_ID);

        auto* leftClip = clipManager.getClip(clipId);
        auto* rightClip = clipManager.getClip(rightClipId);

        REQUIRE(leftClip->midiNotes.empty());
        REQUIRE(rightClip->midiNotes.empty());
    }

    SECTION("Cannot split outside clip boundaries") {
        ClipId clipId = createMidiClipWithNotes(trackId, 2.0, 4.0, {0.0});

        // Try to split before clip start
        SplitClipCommand splitBefore(clipId, 1.0);
        REQUIRE_FALSE(splitBefore.canExecute());

        // Try to split after clip end
        SplitClipCommand splitAfter(clipId, 7.0);
        REQUIRE_FALSE(splitAfter.canExecute());

        // Valid split should work
        SplitClipCommand splitValid(clipId, 3.0);
        REQUIRE(splitValid.canExecute());
    }
}

// ============================================================================
// Undo/Redo
// ============================================================================

TEST_CASE("MIDI clip split - undo/redo", "[midi][clip][split][undo]") {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    // Setup
    clipManager.clearAllClips();
    trackManager.clearAllTracks();
    TrackId trackId = trackManager.createTrack("Test Track", TrackType::MIDI);

    SECTION("Undo restores original clip state") {
        ClipId clipId = createMidiClipWithNotes(trackId, 0.0, 4.0, {0.0, 2.0, 4.0, 6.0});

        // Capture original state
        auto* originalClip = clipManager.getClip(clipId);
        double originalLength = originalClip->length;
        size_t originalNoteCount = originalClip->midiNotes.size();
        double originalFirstNotePos = originalClip->midiNotes[0].startBeat;

        // Split
        SplitClipCommand splitCmd(clipId, 2.0);
        splitCmd.execute();
        ClipId rightClipId = splitCmd.getRightClipId();

        // Verify split happened
        REQUIRE(clipManager.getClip(clipId)->length < originalLength);
        REQUIRE(clipManager.getClip(rightClipId) != nullptr);

        // Undo
        splitCmd.undo();

        // Verify restoration
        auto* restoredClip = clipManager.getClip(clipId);
        REQUIRE(restoredClip->length == Catch::Approx(originalLength));
        REQUIRE(restoredClip->midiNotes.size() == originalNoteCount);
        REQUIRE(restoredClip->midiNotes[0].startBeat == Catch::Approx(originalFirstNotePos));

        // Right clip should be deleted
        REQUIRE(clipManager.getClip(rightClipId) == nullptr);
    }
}
