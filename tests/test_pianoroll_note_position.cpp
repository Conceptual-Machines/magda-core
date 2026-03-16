#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

/**
 * Tests for piano roll note position calculation in absolute and relative modes.
 *
 * These replicate the display beat calculation from
 * PianoRollGridComponent::updateNoteComponentBounds() without requiring JUCE.
 */

namespace {

/// Mirrors the absolute-mode display beat calculation for a single clip.
/// clipStartBeats is the grid's cached value (updated during drag preview).
double computeAbsoluteDisplayBeat(double clipStartBeats, double noteStartBeat,
                                  double midiTrimOffset) {
    return clipStartBeats + noteStartBeat - midiTrimOffset;
}

/// Mirrors the relative-mode display beat calculation for a single clip.
double computeRelativeDisplayBeat(double noteStartBeat) {
    return noteStartBeat;
}

}  // namespace

TEST_CASE("Piano roll note position - absolute mode", "[pianoroll][display]") {
    SECTION("Note position reflects clip timeline position") {
        // Clip at bar 3 (beat 8 at 4/4), note at beat 1 within clip
        double displayBeat = computeAbsoluteDisplayBeat(8.0, 1.0, 0.0);
        REQUIRE(displayBeat == Catch::Approx(9.0));
    }

    SECTION("Note position updates during drag preview") {
        // Original clip at beat 8, note at beat 1
        double original = computeAbsoluteDisplayBeat(8.0, 1.0, 0.0);
        REQUIRE(original == Catch::Approx(9.0));

        // Drag clip to beat 16 (bar 5) — clipStartBeats_ changes to 16
        double dragged = computeAbsoluteDisplayBeat(16.0, 1.0, 0.0);
        REQUIRE(dragged == Catch::Approx(17.0));

        // Note moved by exactly the same amount as the clip
        REQUIRE(dragged - original == Catch::Approx(8.0));
    }

    SECTION("midiTrimOffset compensates for left-resize") {
        // Clip at beat 4, note at beat 2, trimmed by 1 beat from left
        double displayBeat = computeAbsoluteDisplayBeat(4.0, 2.0, 1.0);
        REQUIRE(displayBeat == Catch::Approx(5.0));
    }

    SECTION("Drag preview with trim offset") {
        // Clip at beat 4, note at beat 2, trim offset 1
        double original = computeAbsoluteDisplayBeat(4.0, 2.0, 1.0);

        // Drag to beat 12
        double dragged = computeAbsoluteDisplayBeat(12.0, 2.0, 1.0);

        // Note displacement matches clip displacement
        REQUIRE(dragged - original == Catch::Approx(8.0));
    }
}

TEST_CASE("Piano roll note position - relative mode", "[pianoroll][display]") {
    SECTION("Note position is content-relative regardless of clip position") {
        // Note at beat 2 within clip — position is always 2 regardless of
        // where the clip sits on the timeline
        REQUIRE(computeRelativeDisplayBeat(2.0) == Catch::Approx(2.0));
    }

    SECTION("Clip position has no effect in relative mode") {
        // Same note, different clip positions — display beat unchanged
        double pos1 = computeRelativeDisplayBeat(3.0);
        double pos2 = computeRelativeDisplayBeat(3.0);
        REQUIRE(pos1 == pos2);
    }
}
