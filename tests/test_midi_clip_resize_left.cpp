#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for MIDI clip left-resize operations
 *
 * These tests verify:
 * - Non-looped MIDI: midiOffset adjusts so notes stay at timeline position
 * - Looped MIDI: midiOffset wraps within loop length (phase change)
 * - clip.offset is never touched for MIDI clips
 * - startTime and length update correctly
 * - Arrangement thumbnail preview midiOffset calculation
 * - Piano roll note display offset logic (isLoopedArrangement)
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
// Non-looped MIDI: midiOffset adjusts linearly
// ============================================================================

TEST_CASE("MIDI left-resize non-looped - shrink adjusts midiOffset",
          "[midi][resize][left][nonlooped]") {
    // 120 BPM default
    double bpm = 120.0;

    SECTION("Shrink by 1 second at 120 BPM adds 2 beats to midiOffset") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);

        ClipOperations::resizeContainerFromLeft(clip, 3.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(1.0));
        REQUIRE(clip.length == Catch::Approx(3.0));
        REQUIRE(clip.midiOffset == Catch::Approx(2.0));  // 1s * 120/60 = 2 beats
    }

    SECTION("Shrink by 2 seconds at 120 BPM adds 4 beats to midiOffset") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        REQUIRE(clip.startTime == Catch::Approx(2.0));
        REQUIRE(clip.length == Catch::Approx(6.0));
        REQUIRE(clip.midiOffset == Catch::Approx(4.0));
    }

    SECTION("Shrink at 140 BPM") {
        double bpm140 = 140.0;
        ClipInfo clip = makeMidiClip(0.0, 6.0);

        ClipOperations::resizeContainerFromLeft(clip, 3.0, bpm140);

        // delta = 3s, deltaBeat = 3 * 140/60 = 7.0
        REQUIRE(clip.midiOffset == Catch::Approx(7.0));
    }
}

TEST_CASE("MIDI left-resize non-looped - expand reduces midiOffset",
          "[midi][resize][left][nonlooped]") {
    double bpm = 120.0;

    SECTION("Expand from previously trimmed clip") {
        ClipInfo clip = makeMidiClip(2.0, 4.0);
        clip.midiOffset = 4.0;  // Previously trimmed by 2s at 120 BPM

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        // Expanding by 2s: delta = -2s, deltaBeat = -4 beats
        REQUIRE(clip.startTime == Catch::Approx(0.0));
        REQUIRE(clip.length == Catch::Approx(6.0));
        REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    }

    SECTION("Expand partially") {
        ClipInfo clip = makeMidiClip(3.0, 4.0);
        clip.midiOffset = 6.0;  // Previously trimmed by 3s

        ClipOperations::resizeContainerFromLeft(clip, 5.0, bpm);

        // Expanding by 1s: delta = -1s, deltaBeat = -2 beats
        REQUIRE(clip.startTime == Catch::Approx(2.0));
        REQUIRE(clip.length == Catch::Approx(5.0));
        REQUIRE(clip.midiOffset == Catch::Approx(4.0));
    }
}

TEST_CASE("MIDI left-resize non-looped - sequential resizes accumulate",
          "[midi][resize][left][nonlooped][sequential]") {
    double bpm = 120.0;
    ClipInfo clip = makeMidiClip(0.0, 8.0);

    // Shrink by 1s
    ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(2.0));

    // Shrink by another 1s
    ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(4.0));

    // Expand back by 1s
    ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(2.0));

    // Expand back to original
    ClipOperations::resizeContainerFromLeft(clip, 8.0, bpm);
    REQUIRE(clip.midiOffset == Catch::Approx(0.0));
    REQUIRE(clip.startTime == Catch::Approx(0.0));
}

TEST_CASE("MIDI left-resize non-looped - midiOffset can go negative",
          "[midi][resize][left][nonlooped]") {
    double bpm = 120.0;

    SECTION("Expanding past original start allows negative midiOffset") {
        // Clip starts at 2s with no prior offset — expanding left creates negative midiOffset
        ClipInfo clip = makeMidiClip(2.0, 4.0);
        clip.midiOffset = 0.0;

        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        // delta = -2s, deltaBeat = -4
        REQUIRE(clip.midiOffset == Catch::Approx(-4.0));
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
// Looped MIDI: loop disabled with loopLengthBeats > 0 (non-looped path)
// ============================================================================

TEST_CASE("MIDI left-resize - loopEnabled=false uses non-looped path even with loopLengthBeats",
          "[midi][resize][left][nonlooped]") {
    double bpm = 120.0;

    ClipInfo clip = makeMidiClip(0.0, 8.0, false, 4.0);

    ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

    // Should use linear path, not wrapPhase
    // delta = 2s, deltaBeat = 4.0
    REQUIRE(clip.midiOffset == Catch::Approx(4.0));
}

// ============================================================================
// Arrangement thumbnail preview: display midiOffset during drag
// ============================================================================

TEST_CASE("MIDI left-resize preview - non-looped display offset",
          "[midi][resize][left][preview][nonlooped]") {
    double bpm = 120.0;
    double beatsPerSecond = bpm / 60.0;

    SECTION("Shrink preview computes correct display offset") {
        double snapshotMidiOffset = 0.0;
        double dragStartLength = 4.0;
        double previewLength = 3.0;  // Shrunk by 1s

        double expandDelta = previewLength - dragStartLength;   // -1.0
        double deltaBeat = expandDelta * beatsPerSecond;        // -2.0
        double displayOffset = snapshotMidiOffset - deltaBeat;  // 0 - (-2) = 2.0

        REQUIRE(displayOffset == Catch::Approx(2.0));
    }

    SECTION("Expand preview computes correct display offset") {
        double snapshotMidiOffset = 4.0;
        double dragStartLength = 3.0;
        double previewLength = 5.0;  // Expanded by 2s

        double expandDelta = previewLength - dragStartLength;   // 2.0
        double deltaBeat = expandDelta * beatsPerSecond;        // 4.0
        double displayOffset = snapshotMidiOffset - deltaBeat;  // 4 - 4 = 0.0

        REQUIRE(displayOffset == Catch::Approx(0.0));
    }
}

TEST_CASE("MIDI left-resize preview - looped display offset wraps",
          "[midi][resize][left][preview][looped]") {
    double bpm = 120.0;
    double beatsPerSecond = bpm / 60.0;
    double loopLengthBeats = 4.0;

    SECTION("Shrink preview wraps within loop") {
        double snapshotMidiOffset = 3.0;
        double dragStartLength = 4.0;
        double previewLength = 3.0;  // Shrunk by 1s

        double expandDelta = previewLength - dragStartLength;  // -1.0
        double deltaBeat = expandDelta * beatsPerSecond;       // -2.0
        double displayOffset = wrapPhase(snapshotMidiOffset - deltaBeat, loopLengthBeats);
        // wrapPhase(3 - (-2), 4) = wrapPhase(5, 4) = 1.0

        REQUIRE(displayOffset == Catch::Approx(1.0));
    }

    SECTION("Expand preview wraps within loop") {
        double snapshotMidiOffset = 1.0;
        double dragStartLength = 4.0;
        double previewLength = 6.0;  // Expanded by 2s

        double expandDelta = previewLength - dragStartLength;  // 2.0
        double deltaBeat = expandDelta * beatsPerSecond;       // 4.0
        double displayOffset = wrapPhase(snapshotMidiOffset - deltaBeat, loopLengthBeats);
        // wrapPhase(1 - 4, 4) = wrapPhase(-3, 4) = 1.0

        REQUIRE(displayOffset == Catch::Approx(1.0));
    }
}

// ============================================================================
// Piano roll note display offset logic (isLoopedArrangement)
// ============================================================================

TEST_CASE("Piano roll note offset - looped arrangement clips use 0",
          "[midi][resize][left][pianoroll][looped]") {
    SECTION("Looped arrangement clip: noteOffset is 0 regardless of midiOffset") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);
        clip.midiOffset = 2.5;

        bool isLoopedArrangement = clip.loopEnabled && clip.view != ClipView::Session;
        double noteOffset = isLoopedArrangement ? 0.0 : clip.midiOffset;

        REQUIRE(isLoopedArrangement == true);
        REQUIRE(noteOffset == 0.0);
    }

    SECTION("Looped arrangement after resize: noteOffset still 0") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);
        ClipOperations::resizeContainerFromLeft(clip, 7.0, 120.0);  // Shrink by 1s = 2 beats

        // midiOffset changed by resize (2 beats, not a multiple of 4)
        REQUIRE(clip.midiOffset == Catch::Approx(2.0));

        bool isLoopedArrangement = clip.loopEnabled && clip.view != ClipView::Session;
        double noteOffset = isLoopedArrangement ? 0.0 : clip.midiOffset;

        REQUIRE(noteOffset == 0.0);
    }
}

TEST_CASE("Piano roll note offset - non-looped clips use midiOffset",
          "[midi][resize][left][pianoroll][nonlooped]") {
    SECTION("Non-looped arrangement clip uses midiOffset for display") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);
        clip.midiOffset = 3.0;

        bool isLoopedArrangement = clip.loopEnabled && clip.view != ClipView::Session;
        double noteOffset = isLoopedArrangement ? 0.0 : clip.midiOffset;

        REQUIRE(isLoopedArrangement == false);
        REQUIRE(noteOffset == 3.0);
    }

    SECTION("Non-looped after resize: display offset matches midiOffset") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        ClipOperations::resizeContainerFromLeft(clip, 6.0, 120.0);

        bool isLoopedArrangement = clip.loopEnabled && clip.view != ClipView::Session;
        double noteOffset = isLoopedArrangement ? 0.0 : clip.midiOffset;

        REQUIRE(noteOffset == Catch::Approx(clip.midiOffset));
        REQUIRE(noteOffset == Catch::Approx(4.0));
    }
}

TEST_CASE("Piano roll note offset - session clips use midiOffset",
          "[midi][resize][left][pianoroll][session]") {
    SECTION("Session clip with loop uses midiOffset (not 0)") {
        ClipInfo clip = makeMidiClip(0.0, 4.0, true, 4.0);
        clip.view = ClipView::Session;
        clip.midiOffset = 2.0;

        bool isLoopedArrangement = clip.loopEnabled && clip.view != ClipView::Session;
        double noteOffset = isLoopedArrangement ? 0.0 : clip.midiOffset;

        REQUIRE(isLoopedArrangement == false);
        REQUIRE(noteOffset == 2.0);
    }
}

// ============================================================================
// Notes stay at timeline position (the core invariant)
// ============================================================================

TEST_CASE("MIDI left-resize non-looped - notes stay at timeline position",
          "[midi][resize][left][nonlooped][invariant]") {
    double bpm = 120.0;
    double beatsPerSecond = bpm / 60.0;

    SECTION("Note display position unchanged after shrink") {
        ClipInfo clip = makeMidiClip(0.0, 4.0);

        // Note at beat 3 in clip-local coordinates
        MidiNote note;
        note.startBeat = 3.0;
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        // Before resize: absolute display position
        double clipStartBeats = clip.startTime * beatsPerSecond;
        double displayBefore = clipStartBeats + note.startBeat - clip.midiOffset;
        // 0 + 3 - 0 = 3.0

        // Resize: shrink by 1 second from left
        ClipOperations::resizeContainerFromLeft(clip, 3.0, bpm);

        // After resize: absolute display position
        clipStartBeats = clip.startTime * beatsPerSecond;
        bool isLoopedArr = clip.loopEnabled && clip.view != ClipView::Session;
        double noteOffset = isLoopedArr ? 0.0 : clip.midiOffset;
        double displayAfter = clipStartBeats + note.startBeat - noteOffset;
        // 2.0 + 3.0 - 2.0 = 3.0

        REQUIRE(displayBefore == Catch::Approx(displayAfter));
    }

    SECTION("Multiple notes maintain timeline position after resize") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        // Add notes at beats 0, 2, 4, 6
        for (int i = 0; i < 4; ++i) {
            MidiNote note;
            note.startBeat = i * 2.0;
            note.lengthBeats = 1.0;
            note.noteNumber = 60 + i;
            note.velocity = 100;
            clip.midiNotes.push_back(note);
        }

        // Capture pre-resize display positions
        std::vector<double> displayBefore;
        double clipStartBeats = clip.startTime * beatsPerSecond;
        for (const auto& n : clip.midiNotes) {
            displayBefore.push_back(clipStartBeats + n.startBeat - clip.midiOffset);
        }

        // Resize: shrink by 2 seconds
        ClipOperations::resizeContainerFromLeft(clip, 6.0, bpm);

        // Capture post-resize display positions
        clipStartBeats = clip.startTime * beatsPerSecond;
        bool isLoopedArr = clip.loopEnabled && clip.view != ClipView::Session;
        double noteOffset = isLoopedArr ? 0.0 : clip.midiOffset;

        for (size_t i = 0; i < clip.midiNotes.size(); ++i) {
            double displayAfter = clipStartBeats + clip.midiNotes[i].startBeat - noteOffset;
            REQUIRE(displayBefore[i] == Catch::Approx(displayAfter));
        }
    }

    SECTION("Note position stable through shrink then expand") {
        ClipInfo clip = makeMidiClip(0.0, 8.0);

        MidiNote note;
        note.startBeat = 5.0;
        note.lengthBeats = 1.0;
        note.noteNumber = 60;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        double clipStartBeats = clip.startTime * beatsPerSecond;
        double displayOriginal = clipStartBeats + note.startBeat - clip.midiOffset;

        // Shrink by 3s
        ClipOperations::resizeContainerFromLeft(clip, 5.0, bpm);

        clipStartBeats = clip.startTime * beatsPerSecond;
        double noteOffset = clip.midiOffset;  // non-looped
        double displayShrunk = clipStartBeats + note.startBeat - noteOffset;
        REQUIRE(displayOriginal == Catch::Approx(displayShrunk));

        // Expand back by 3s
        ClipOperations::resizeContainerFromLeft(clip, 8.0, bpm);

        clipStartBeats = clip.startTime * beatsPerSecond;
        noteOffset = clip.midiOffset;
        double displayExpanded = clipStartBeats + note.startBeat - noteOffset;
        REQUIRE(displayOriginal == Catch::Approx(displayExpanded));
    }
}

// ============================================================================
// Looped MIDI: notes stay at loop-relative positions
// ============================================================================

TEST_CASE("MIDI left-resize looped - notes stay at fixed loop positions",
          "[midi][resize][left][looped][invariant]") {
    double bpm = 120.0;
    double beatsPerSecond = bpm / 60.0;

    SECTION("Note display position unchanged (noteOffset=0 for looped arrangement)") {
        ClipInfo clip = makeMidiClip(0.0, 8.0, true, 4.0);

        MidiNote note;
        note.startBeat = 1.0;
        note.lengthBeats = 0.5;
        note.noteNumber = 64;
        note.velocity = 100;
        clip.midiNotes.push_back(note);

        // For looped arrangement clips, noteOffset is always 0
        bool isLoopedArr = clip.loopEnabled && clip.view != ClipView::Session;
        REQUIRE(isLoopedArr == true);

        // Note at beat 1 stays at beat 1 regardless of resize
        double displayBefore = note.startBeat;

        ClipOperations::resizeContainerFromLeft(clip, 7.0, bpm);  // Shrink by 1s = 2 beats

        // midiOffset changed (it's a phase now), but noteOffset stays 0
        REQUIRE(clip.midiOffset == Catch::Approx(2.0));
        double noteOffset = isLoopedArr ? 0.0 : clip.midiOffset;
        double displayAfter = note.startBeat - noteOffset;

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

TEST_CASE("MIDI left-resize - BPM affects midiOffset magnitude", "[midi][resize][left][bpm]") {
    SECTION("Same resize at different BPMs produces proportional offsets") {
        ClipInfo clip60 = makeMidiClip(0.0, 4.0);
        ClipInfo clip120 = makeMidiClip(0.0, 4.0);
        ClipInfo clip240 = makeMidiClip(0.0, 4.0);

        // All shrink by 1 second
        ClipOperations::resizeContainerFromLeft(clip60, 3.0, 60.0);
        ClipOperations::resizeContainerFromLeft(clip120, 3.0, 120.0);
        ClipOperations::resizeContainerFromLeft(clip240, 3.0, 240.0);

        // 1s * 60/60 = 1 beat, 1s * 120/60 = 2 beats, 1s * 240/60 = 4 beats
        REQUIRE(clip60.midiOffset == Catch::Approx(1.0));
        REQUIRE(clip120.midiOffset == Catch::Approx(2.0));
        REQUIRE(clip240.midiOffset == Catch::Approx(4.0));
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
        REQUIRE(clip.midiOffset > 0.0);
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
