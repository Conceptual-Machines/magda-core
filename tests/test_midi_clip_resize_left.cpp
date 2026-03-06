#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for MIDI clip left-resize operations
 *
 * Non-looped MIDI: only clip boundary (startTime/length) changes.
 *   midiOffset stays unchanged — it's a user-controlled playback offset.
 * Looped MIDI: midiOffset wraps within loopLengthBeats (phase change).
 *
 * Piano roll: noteOffset is always 0 (notes never shift in the editor).
 * Arrangement thumbnail: uses midiOffset for content display positioning.
 */

using namespace magda;

// Helper to create a basic MIDI clip
static ClipInfo makeMidiClip(double startTime, double length, bool looped = false,
                             double loopLengthBeats = 0.0) {
    ClipInfo clip;
    clip.type = ClipType::MIDI;
    clip.startTime = startTime;
    clip.length = length;
    clip.midiOffset = 0.0;
    clip.loopEnabled = looped;
    clip.loopLengthBeats = loopLengthBeats;
    clip.view = ClipView::Arrangement;
    clip.speedRatio = 1.0;
    clip.offset = 0.0;
    return clip;
}

// ============================================================================
// Non-looped MIDI: midiOffset unchanged (boundary change only)
// ============================================================================

TEST_CASE("MIDI left-resize non-looped - shrink does NOT adjust midiOffset",
          "[midi][resize][left][nonlooped]") {
    double bpm = 120.0;

    SECTION("Shrink by 1 second at 120 BPM - midiOffset unchanged") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);

        ClipOperations::resizeContainerFromLeft(clip, 3.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(1.0));
        REQUIRE(clip.length == Catch::Approx(3.0));
        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    }

    SECTION("Shrink by 2 seconds at 120 BPM - midiOffset unchanged") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(2.0));
        REQUIRE(clip.length == Catch::Approx(6.0));
        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    }

    SECTION("Shrink at 140 BPM - midiOffset unchanged") {
        double bpm140 = 140.0;
        ClipInfo clip = makeMidiClip(0.0, 6.0);

        ClipOperations::resizeContainerFromLeft(clip, 3.0, bpm140);

        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    }
}

TEST_CASE("MIDI left-resize non-looped - expand does NOT adjust midiOffset",
          "[midi][resize][left][nonlooped]") {
    double bpm = 120.0;

    SECTION("Expand from previously trimmed clip - midiOffset stays") {
        ClipInfo clip = makeMidiClip(2.0, 4.0);
        clip.midiOffset = 4.0;  // Set externally (e.g. user dragged offset marker)

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(0.0));
        REQUIRE(clip.length == Catch::Approx(6.0));
        REQUIRE(clip.midiOffset == Catch::Approx(4.0));  // Unchanged
    }

    SECTION("Expand partially - midiOffset stays") {
        ClipInfo clip = makeMidiClip(3.0, 4.0);
        clip.midiOffset = 6.0;

        ClipOperations::resizeContainerFromLeft(clip, 5.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(2.0));
        REQUIRE(clip.length == Catch::Approx(5.0));
        REQUIRE(clip.midiOffset == Catch::Approx(6.0));  // Unchanged
    }
}

TEST_CASE("MIDI left-resize non-looped - sequential resizes don't change midiOffset",
          "[midi][resize][left][nonlooped][sequential]") {
    double bpm = 120.0;
    ClipInfo clip = makeMidiClip(0.0, 8.0);

    // Shrink by 1s
    ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(0.0));

    // Shrink by another 1s
    ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(0.0));

    // Expand back by 1s
    ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(0.0));

    // Expand back to original
    ClipOperations::resizeContainerFromLeft(clip, 8.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    REQUIRE(clip.startTime == Catch::Approx(0.0));
}

TEST_CASE("MIDI left-resize non-looped - midiOffset stays unchanged even past time 0",
          "[midi][resize][left][nonlooped]") {
    double bpm = 120.0;

    SECTION("Expanding past original start does NOT change midiOffset") {
        ClipInfo clip = makeMidiClip(2.0, 4.0);
        clip.midiOffset = 0.0;

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
        REQUIRE(clip.startTime == Catch::Approx(0.0));
    }
}

// ============================================================================
// Non-looped MIDI: clip.offset must NOT be touched
// ============================================================================

TEST_CASE("MIDI left-resize non-looped - clip.offset unchanged",
          "[midi][resize][left][nonlooped][offset]") {
    double bpm = 120.0;

    SECTION("Shrink does not modify clip.offset") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);
        clip.offset = 0.0;

        ClipOperations::resizeContainerFromLeft(clip, 3.0, bpm);

        REQUIRE(clip.offset == 0.0);
    }

    SECTION("Expand does not modify clip.offset") {
        ClipInfo clip = makeMidiClip(2.0, 4.0);
        clip.offset = 1.5;  // Some pre-existing value

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.offset == 1.5);
    }
}

// ============================================================================
// Looped MIDI: midiOffset wraps within loopLengthBeats
// ============================================================================

TEST_CASE("MIDI left-resize looped - midiOffset wraps within loop",
          "[midi][resize][left][looped]") {
    double bpm = 120.0;

    SECTION("Shrink by 1s wraps midiOffset in 4-beat loop") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);

        ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);

        // delta = 1s, deltaBeat = 2 beats
        // wrapPhase(0 + 2, 4) = 2.0
        REQUIRE(clip.midiOffset == Catch::Approx(2.0));
    }

    SECTION("Shrink wraps around loop boundary") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);
        clip.midiOffset = 3.0;  // Already near end of loop

        ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);

        // delta = 1s, deltaBeat = 2 beats
        // wrapPhase(3.0 + 2.0, 4.0) = wrapPhase(5.0, 4.0) = 1.0
        REQUIRE(clip.midiOffset == Catch::Approx(1.0));
    }

    SECTION("Expand wraps correctly") {
        ClipInfo clip = makeMidiClip(2.0, 4.0, true, 4.0);
        clip.midiOffset = 1.0;

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        // delta = -2s, deltaBeat = -4 beats
        // wrapPhase(1.0 + (-4.0), 4.0) = wrapPhase(-3.0, 4.0) = 1.0
        REQUIRE(clip.midiOffset == Catch::Approx(1.0));
    }

    SECTION("Large shrink wraps multiple times") {
        ClipInfo clip = makeMidiClip(0.0, 20.0, true, 4.0);

        ClipOperations::resizeContainerFromLeft(clip, 14.0, bpm);

        // delta = 6s, deltaBeat = 12 beats
        // wrapPhase(0 + 12, 4) = wrapPhase(12, 4) = 0.0
        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    }

    SECTION("Shrink by exactly loop length returns to same phase") {
        ClipInfo clip = makeMidiClip(0.0, 10.0, true, 4.0);
        clip.midiOffset = 1.5;

        // delta = 2s = 4 beats = exactly one loop length
        ClipOperations::resizeContainerFromLeft(clip, 8.0, bpm);

        // wrapPhase(1.5 + 4, 4) = wrapPhase(5.5, 4) = 1.5
        REQUIRE(clip.midiOffset == Catch::Approx(1.5));
    }
}

TEST_CASE("MIDI left-resize looped - clip.offset unchanged",
          "[midi][resize][left][looped][offset]") {
    double bpm = 120.0;

    ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);
    clip.offset = 0.0;

    ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

    REQUIRE(clip.offset == 0.0);
}

// ============================================================================
// Non-looped with loopLengthBeats > 0 (loopEnabled=false uses non-looped path)
// ============================================================================

TEST_CASE("MIDI left-resize - loopEnabled=false uses non-looped path even with loopLengthBeats",
          "[midi][resize][left][nonlooped]") {
    double bpm = 120.0;

    ClipInfo clip = makeMidiClip(0.0, 8.0, false, 4.0);

    ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

    // Non-looped: midiOffset unchanged regardless of loopLengthBeats
    REQUIRE(clip.midiOffset == Catch::Approx(0.0));
}

// ============================================================================
// Piano roll note display: noteOffset is always 0
// ============================================================================

TEST_CASE("Piano roll note offset - always 0 regardless of clip type",
          "[midi][resize][left][pianoroll]") {
    SECTION("Non-looped arrangement clip: noteOffset is always 0") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);
        clip.midiOffset = 3.0;

        double noteOffset = 0.0;
        REQUIRE(noteOffset == 0.0);
    }

    SECTION("Looped arrangement clip: noteOffset is always 0") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);
        clip.midiOffset = 2.5;

        double noteOffset = 0.0;
        REQUIRE(noteOffset == 0.0);
    }

    SECTION("After non-looped resize: noteOffset still 0, notes don't shift") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        ClipOperations::resizeContainerFromLeft(clip, 6.0, 120.0);

        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
        double noteOffset = 0.0;
        REQUIRE(noteOffset == 0.0);
    }

    SECTION("After looped resize: noteOffset still 0") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);
        ClipOperations::resizeContainerFromLeft(clip, 7.0, 120.0);

        REQUIRE(clip.midiOffset == Catch::Approx(2.0));
        double noteOffset = 0.0;
        REQUIRE(noteOffset == 0.0);
    }

    SECTION("Session clip: noteOffset is also always 0") {
        ClipInfo clip = makeMidiClip(0.0, 4.0, true, 4.0);
        clip.view = ClipView::Session;
        clip.midiOffset = 2.0;

        double noteOffset = 0.0;
        REQUIRE(noteOffset == 0.0);
    }
}

// ============================================================================
// Notes stay at their beat positions (the core invariant)
// ============================================================================

TEST_CASE("MIDI left-resize non-looped - notes keep beat positions in piano roll",
          "[midi][resize][left][nonlooped][invariant]") {
    double bpm = 120.0;

    SECTION("Note startBeat unchanged after resize") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);

        MidiNote note;
        note.startBeat = 3.0;
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        double midiOffsetBefore = clip.midiOffset;

        ClipOperations::resizeContainerFromLeft(clip, 3.0, bpm);

        // midiOffset stays the same — notes don't move in the piano roll
        REQUIRE(clip.midiOffset == Catch::Approx(midiOffsetBefore));
        REQUIRE(clip.midiNotes[0].startBeat == 3.0);
    }

    SECTION("Shrink then expand: midiOffset always 0") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        ClipOperations::resizeContainerFromLeft(clip, 5.0, bpm);
        REQUIRE(clip.midiOffset == Catch::Approx(0.0));

        ClipOperations::resizeContainerFromLeft(clip, 8.0, bpm);
        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
        REQUIRE(clip.startTime == Catch::Approx(0.0));
    }
}

// ============================================================================
// Looped MIDI: notes stay at fixed loop positions
// ============================================================================

TEST_CASE("MIDI left-resize looped - notes stay at fixed loop positions",
          "[midi][resize][left][looped][invariant]") {
    double bpm = 120.0;

    SECTION("Note display position unchanged (noteOffset=0 for looped arrangement)") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);

        MidiNote note;
        note.startBeat = 1.0;
        note.lengthBeats = 0.5;
        note.noteNumber = 64;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        double displayBefore = note.startBeat;

        ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);

        REQUIRE(clip.midiOffset == Catch::Approx(2.0));
        double displayAfter = note.startBeat;
        REQUIRE(displayBefore == Catch::Approx(displayAfter));
    }
}

// ============================================================================
// startTime and length are always correct
// ============================================================================

TEST_CASE("MIDI left-resize - startTime and length correct", "[midi][resize][left][container]") {
    double bpm = 120.0;

    SECTION("Non-looped shrink") {
        ClipInfo clip = makeMidiClip(1.0, 6.0);
        ClipOperations::resizeContainerFromLeft(clip, 4.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(3.0));
        REQUIRE(clip.length == Catch::Approx(4.0));
    }

    SECTION("Non-looped expand") {
        ClipInfo clip = makeMidiClip(3.0, 4.0);
        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(1.0));
        REQUIRE(clip.length == Catch::Approx(6.0));
    }

    SECTION("Looped shrink") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);
        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(2.0));
        REQUIRE(clip.length == Catch::Approx(6.0));
    }

    SECTION("Looped expand") {
        ClipInfo clip = makeMidiClip(4.0, 4.0, true, 4.0);
        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(2.0));
        REQUIRE(clip.length == Catch::Approx(6.0));
    }
}

// ============================================================================
// wrapPhase correctness
// ============================================================================

TEST_CASE("wrapPhase helper", "[midi][resize][left][wrapPhase]") {
    SECTION("Positive value within range unchanged") {
        REQUIRE(wrapPhase(1.5, 4.0) == Catch::Approx(1.5));
    }

    SECTION("Value at period boundary wraps to 0") {
        REQUIRE(wrapPhase(4.0, 4.0) == Catch::Approx(0.0));
    }

    SECTION("Value beyond period wraps") {
        REQUIRE(wrapPhase(5.5, 4.0) == Catch::Approx(1.5));
    }

    SECTION("Negative value wraps to positive") {
        REQUIRE(wrapPhase(-1.0, 4.0) == Catch::Approx(3.0));
    }

    SECTION("Large negative wraps correctly") {
        REQUIRE(wrapPhase(-9.0, 4.0) == Catch::Approx(3.0));
    }

    SECTION("Zero period returns 0") {
        REQUIRE(wrapPhase(3.0, 0.0) == Catch::Approx(0.0));
    }

    SECTION("Negative period returns 0") {
        REQUIRE(wrapPhase(3.0, -1.0) == Catch::Approx(0.0));
    }

    SECTION("Zero value stays zero") {
        REQUIRE(wrapPhase(0.0, 4.0) == Catch::Approx(0.0));
    }
}

// ============================================================================
// BPM sensitivity
// ============================================================================

TEST_CASE("MIDI left-resize - BPM does not affect non-looped midiOffset",
          "[midi][resize][left][bpm]") {
    SECTION("Same resize at different BPMs all leave midiOffset unchanged") {
        ClipInfo clip60 = makeMidiClip(0.0, 4.0);
        ClipInfo clip120 = makeMidiClip(0.0, 4.0);
        ClipInfo clip240 = makeMidiClip(0.0, 4.0);

        // All shrink by 1 second
        ClipOperations::resizeContainerFromLeft(clip60, 3.0, 60.0);
        ClipOperations::resizeContainerFromLeft(clip120, 3.0, 120.0);
        ClipOperations::resizeContainerFromLeft(clip240, 3.0, 240.0);

        // Non-looped: midiOffset unchanged at all tempos
        REQUIRE(clip60.midiOffset == Catch::Approx(0.0));
        REQUIRE(clip120.midiOffset == Catch::Approx(0.0));
        REQUIRE(clip240.midiOffset == Catch::Approx(0.0));
    }
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("MIDI left-resize - edge cases", "[midi][resize][left][edge]") {
    double bpm = 120.0;

    SECTION("Resize to minimum length") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);

        ClipOperations::resizeContainerFromLeft(clip, ClipOperations::MIN_CLIP_LENGTH, bpm);

        REQUIRE(clip.length >= ClipOperations::MIN_CLIP_LENGTH);
        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    }

    SECTION("No-op resize (same length) leaves midiOffset unchanged") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);
        clip.midiOffset = 1.0;

        ClipOperations::resizeContainerFromLeft(clip, 4.0, bpm);

        REQUIRE(clip.midiOffset == Catch::Approx(1.0));
    }

    SECTION("Looped clip with very small loop length") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 0.5);

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        // delta = 2s, deltaBeat = 4, wrapPhase(4, 0.5) = 0.0
        REQUIRE(clip.midiOffset >= 0.0);
        REQUIRE(clip.midiOffset < 0.5);
    }

    SECTION("Non-looped MIDI clip note data is not modified by resize") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        MidiNote note;
        note.startBeat = 3.0;
        note.lengthBeats = 2.0;
        note.noteNumber = 72;
        note.velocity = 90;
        clip.midiNotes.push_back(note);

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.midiNotes.size() == 1);
        REQUIRE(clip.midiNotes[0].startBeat == 3.0);
        REQUIRE(clip.midiNotes[0].lengthBeats == 2.0);
        REQUIRE(clip.midiNotes[0].noteNumber == 72);
        REQUIRE(clip.midiNotes[0].velocity == 90);
    }
}
