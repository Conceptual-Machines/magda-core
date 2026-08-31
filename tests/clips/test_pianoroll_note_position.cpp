#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

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

namespace {

/// Mirrors PianoRollGridComponent::beatToPixel (rounds to integer pixels).
int beatToPixel(double beat, double pixelsPerBeat) {
    return static_cast<int>(std::round(beat * pixelsPerBeat));
}

/// Mirrors PianoRollGridComponent::pixelToBeat.
double pixelToBeat(int x, double pixelsPerBeat) {
    return x / pixelsPerBeat;
}

/// Mirrors PianoRollGridComponent::snapBeatToGridFloor: picks the cell whose
/// rendered (rounded-pixel) start boundary is at or left of the clicked
/// pixel, matching the grid lines the user actually sees.
double snapBeatToGridFloor(int mouseX, double gridResolutionBeats, double pixelsPerBeat) {
    const double beat = pixelToBeat(mouseX, pixelsPerBeat);
    double cell = std::floor(beat / gridResolutionBeats + 1e-6);
    if (beatToPixel((cell + 1.0) * gridResolutionBeats, pixelsPerBeat) <= mouseX)
        cell += 1.0;
    else if (beatToPixel(cell * gridResolutionBeats, pixelsPerBeat) > mouseX)
        cell -= 1.0;
    return cell * gridResolutionBeats;
}

}  // namespace

TEST_CASE("Pencil insert floors to containing cell", "[pianoroll][snap]") {
    SECTION("Click inside a cell floors to the cell start") {
        // 100 px/beat, 0.25 grid: pixel 30 is beat 0.3, inside cell [0.25, 0.5)
        REQUIRE(snapBeatToGridFloor(30, 0.25, 100.0) == Catch::Approx(0.25));
    }

    SECTION("Click on a line whose pixel rounded down lands on its cell") {
        // At 93 px/beat the 0.25 line renders at round(23.25) = pixel 23,
        // which maps back to 23/93 = 0.2473 — below the true boundary. The
        // cell decision must follow the rendered pixel, not the raw beat.
        REQUIRE(beatToPixel(0.25, 93.0) == 23);
        REQUIRE(snapBeatToGridFloor(23, 0.25, 93.0) == Catch::Approx(0.25));
    }

    SECTION("Click left of a line whose pixel rounded up stays in its cell") {
        // At 50 px/beat the 0.25 line renders at round(12.5) = pixel 13.
        // Pixel 12 is visibly left of the line (beat 0.24): it must floor to
        // 0.0, not be pushed into the next cell by a uniform half-pixel bias.
        REQUIRE(beatToPixel(0.25, 50.0) == 13);
        REQUIRE(snapBeatToGridFloor(12, 0.25, 50.0) == Catch::Approx(0.0));
        REQUIRE(snapBeatToGridFloor(13, 0.25, 50.0) == Catch::Approx(0.25));
    }

    SECTION("Rendered lines map to their own cell; one pixel left stays before") {
        for (double ppb : {37.0, 50.0, 61.0, 93.0, 100.0, 131.0, 250.0}) {
            for (double grid : {0.125, 0.25, 0.5, 1.0}) {
                for (int cell = 1; cell <= 16; ++cell) {
                    const double boundary = cell * grid;
                    const int lineX = beatToPixel(boundary, ppb);

                    // On the rendered line: the cell the line starts
                    REQUIRE(snapBeatToGridFloor(lineX, grid, ppb) ==
                            Catch::Approx(boundary).margin(1e-9));

                    // One pixel left of the line: the preceding cell —
                    // unless that pixel is itself the previous rendered line
                    // (adjacent lines at very low zoom).
                    const double prev = boundary - grid;
                    if (beatToPixel(prev, ppb) < lineX - 1) {
                        REQUIRE(snapBeatToGridFloor(lineX - 1, grid, ppb) ==
                                Catch::Approx(prev).margin(1e-9));
                    }
                }
            }
        }
    }
}
