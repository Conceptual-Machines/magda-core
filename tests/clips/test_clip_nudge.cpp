// Tests for the keyboard clip-nudge model (#1957): the pure delta rules behind
// Shift+arrow clip moves. Everything here runs headless on plain data.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/interaction/ClipNudge.hpp"

using Catch::Approx;
using namespace magda::interaction;

namespace {

HorizontalNudge horizontal(double anchorPos, int direction, double grid = 1.0, bool snap = true) {
    HorizontalNudge n;
    n.anchorPosition = anchorPos;
    n.gridStep = grid;
    n.snapToGrid = snap;
    n.direction = direction;
    return n;
}

VerticalNudge vertical(int minIndex, int maxIndex, int trackCount, int direction) {
    VerticalNudge n;
    n.minTrackIndex = minIndex;
    n.maxTrackIndex = maxIndex;
    n.trackCount = trackCount;
    n.direction = direction;
    return n;
}

}  // namespace

// ============================================================================
// Horizontal: on-grid clips
// ============================================================================

TEST_CASE("An on-grid clip moves a full grid step", "[clip_nudge]") {
    REQUIRE(horizontalDelta(horizontal(4.0, 1)) == Approx(1.0));
    REQUIRE(horizontalDelta(horizontal(4.0, -1)) == Approx(-1.0));
}

TEST_CASE("The grid step sets the nudge distance", "[clip_nudge]") {
    REQUIRE(horizontalDelta(horizontal(4.0, 1, 0.25)) == Approx(0.25));
    REQUIRE(horizontalDelta(horizontal(8.0, -1, 4.0)) == Approx(-4.0));
}

TEST_CASE("Floating-point residue on a grid line still moves a whole step", "[clip_nudge]") {
    // Repeated snapped moves leave anchors a hair off the line; treating that
    // as off-grid would nudge by an invisible fraction of a beat.
    REQUIRE(horizontalDelta(horizontal(4.0 - 1e-12, 1)) == Approx(1.0).margin(1e-9));
    REQUIRE(horizontalDelta(horizontal(4.0 + 1e-12, -1)) == Approx(-1.0).margin(1e-9));
}

// ============================================================================
// Horizontal: off-grid clips
// ============================================================================

TEST_CASE("An off-grid clip lands on the next grid line, not a full step out", "[clip_nudge]") {
    REQUIRE(horizontalDelta(horizontal(0.5, 1)) == Approx(0.5));      // 0.5 -> 1.0
    REQUIRE(horizontalDelta(horizontal(0.5, -1)) == Approx(-0.5));    // 0.5 -> 0.0
    REQUIRE(horizontalDelta(horizontal(4.75, 1)) == Approx(0.25));    // 4.75 -> 5.0
    REQUIRE(horizontalDelta(horizontal(4.75, -1)) == Approx(-0.75));  // 4.75 -> 4.0
}

TEST_CASE("With snap off, an off-grid clip keeps its offset", "[clip_nudge]") {
    REQUIRE(horizontalDelta(horizontal(0.5, 1, 1.0, false)) == Approx(1.0));
    REQUIRE(horizontalDelta(horizontal(4.75, -1, 1.0, false)) == Approx(-1.0));
}

// ============================================================================
// Horizontal: the start of the timeline
// ============================================================================

TEST_CASE("A clip near the start clamps flush against beat 0", "[clip_nudge]") {
    REQUIRE(horizontalDelta(horizontal(0.4, -1, 1.0, false)) == Approx(-0.4));
}

TEST_CASE("A clip already at beat 0 refuses to move earlier", "[clip_nudge]") {
    REQUIRE(horizontalDelta(horizontal(0.0, -1)) == Approx(0.0));
    REQUIRE(horizontalDelta(horizontal(0.0, -1, 1.0, false)) == Approx(0.0));
}

TEST_CASE("A degenerate grid refuses to move", "[clip_nudge]") {
    REQUIRE(horizontalDelta(horizontal(4.0, 1, 0.0)) == Approx(0.0));
    REQUIRE(horizontalDelta(horizontal(4.0, 0)) == Approx(0.0));
}

// ============================================================================
// Horizontal: the model is unit-agnostic
// ============================================================================

TEST_CASE("The same rules hold in the seconds domain", "[clip_nudge]") {
    // Seconds display mode feeds seconds and a second-based interval rather
    // than beats. At 90 BPM / 40 px per beat the drawn grid is 1 s while the
    // beat fraction is 2 beats (1.333 s), so the caller must pass the interval
    // the ruler actually shows — and get a seconds delta straight back.
    REQUIRE(horizontalDelta(horizontal(2.0, 1, 1.0)) == Approx(1.0));    // 2.0s -> 3.0s
    REQUIRE(horizontalDelta(horizontal(2.4, 1, 1.0)) == Approx(0.6));    // 2.4s -> 3.0s
    REQUIRE(horizontalDelta(horizontal(2.4, -1, 1.0)) == Approx(-0.4));  // 2.4s -> 2.0s
    REQUIRE(horizontalDelta(horizontal(1.0, 1, 0.5)) == Approx(0.5));    // 500 ms grid
}

// ============================================================================
// Vertical
// ============================================================================

TEST_CASE("A clip in the middle of the stack moves one track either way", "[clip_nudge]") {
    REQUIRE(verticalTrackDelta(vertical(2, 2, 5, 1)) == 1);
    REQUIRE(verticalTrackDelta(vertical(2, 2, 5, -1)) == -1);
}

TEST_CASE("The selection moves as a block or not at all", "[clip_nudge]") {
    // Spread over tracks 0..2 of 4: down is fine, up would clamp the topmost
    // clip and collapse the spread.
    REQUIRE(verticalTrackDelta(vertical(0, 2, 4, 1)) == 1);
    REQUIRE(verticalTrackDelta(vertical(0, 2, 4, -1)) == 0);
    REQUIRE(verticalTrackDelta(vertical(1, 3, 4, 1)) == 0);
}

TEST_CASE("Clips on the end tracks refuse to move off the stack", "[clip_nudge]") {
    REQUIRE(verticalTrackDelta(vertical(0, 0, 3, -1)) == 0);
    REQUIRE(verticalTrackDelta(vertical(2, 2, 3, 1)) == 0);
}

TEST_CASE("A single track leaves nowhere to go", "[clip_nudge]") {
    REQUIRE(verticalTrackDelta(vertical(0, 0, 1, 1)) == 0);
    REQUIRE(verticalTrackDelta(vertical(0, 0, 1, -1)) == 0);
}

TEST_CASE("Out-of-range input refuses to move", "[clip_nudge]") {
    REQUIRE(verticalTrackDelta(vertical(-1, 1, 3, 1)) == 0);
    REQUIRE(verticalTrackDelta(vertical(0, 3, 3, -1)) == 0);
    REQUIRE(verticalTrackDelta(vertical(0, 0, 0, 1)) == 0);
    REQUIRE(verticalTrackDelta(vertical(1, 1, 3, 0)) == 0);
}
