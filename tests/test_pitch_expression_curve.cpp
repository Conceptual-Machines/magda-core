// Tests for the MPE pitch glide curve (#2198): the shape a note's per-note
// pitch expression takes between two authored points, and the inverse a drag
// uses to bend it. Headless — plain points in, semitones out.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/PitchExpressionCurve.hpp"

using Catch::Approx;
using namespace magda;

namespace {

MidiPitchExpressionPoint pt(double beat, double semitones, double tension = 0.0) {
    MidiPitchExpressionPoint p;
    p.beat = beat;
    p.semitones = semitones;
    p.tension = tension;
    return p;
}

}  // namespace

// ============================================================================
// The straight line it has always been
// ============================================================================

TEST_CASE("An unbent glide is the linear interpolation it used to be", "[pitch_expression]") {
    const std::vector<MidiPitchExpressionPoint> points{pt(0.0, 0.0), pt(2.0, 4.0)};

    REQUIRE(evaluatePitchExpressionCurve(points, 0.0) == Approx(0.0));
    REQUIRE(evaluatePitchExpressionCurve(points, 0.5) == Approx(1.0));
    REQUIRE(evaluatePitchExpressionCurve(points, 1.0) == Approx(2.0));
    REQUIRE(evaluatePitchExpressionCurve(points, 1.5) == Approx(3.0));
    REQUIRE(evaluatePitchExpressionCurve(points, 2.0) == Approx(4.0));
}

TEST_CASE("A glide holds its end values rather than extrapolating", "[pitch_expression]") {
    // A note that starts before its first point sounds at that point's pitch,
    // and one that runs past its last stays where the glide left it.
    const std::vector<MidiPitchExpressionPoint> points{pt(1.0, -2.0), pt(2.0, 3.0)};

    REQUIRE(evaluatePitchExpressionCurve(points, 0.0) == Approx(-2.0));
    REQUIRE(evaluatePitchExpressionCurve(points, -5.0) == Approx(-2.0));
    REQUIRE(evaluatePitchExpressionCurve(points, 9.0) == Approx(3.0));
}

TEST_CASE("A glide with no points is no glide", "[pitch_expression]") {
    REQUIRE(evaluatePitchExpressionCurve({}, 0.0) == Approx(0.0));
    REQUIRE(evaluatePitchExpressionCurve({}, 4.0) == Approx(0.0));
    REQUIRE(evaluatePitchExpressionCurve({pt(1.0, 5.0)}, 0.0) == Approx(5.0));
}

// ============================================================================
// The bend
// ============================================================================

TEST_CASE("A bent segment still meets both of its endpoints", "[pitch_expression]") {
    // Whatever the shape does in between, it starts and ends where the user put
    // the points. A curve that misses its own endpoints would move the pitch of
    // a note the user never touched.
    for (const double tension : {-3.0, -1.5, -0.4, 0.4, 1.5, 3.0}) {
        const std::vector<MidiPitchExpressionPoint> points{pt(0.0, -1.0, tension), pt(2.0, 5.0)};
        REQUIRE(evaluatePitchExpressionCurve(points, 0.0) == Approx(-1.0));
        REQUIRE(evaluatePitchExpressionCurve(points, 2.0) == Approx(5.0));
    }
}

TEST_CASE("A bend leaves the straight line, in the direction of its sign", "[pitch_expression]") {
    const std::vector<MidiPitchExpressionPoint> straight{pt(0.0, 0.0), pt(2.0, 4.0)};
    const std::vector<MidiPitchExpressionPoint> positive{pt(0.0, 0.0, 1.5), pt(2.0, 4.0)};
    const std::vector<MidiPitchExpressionPoint> negative{pt(0.0, 0.0, -1.5), pt(2.0, 4.0)};

    const double mid = evaluatePitchExpressionCurve(straight, 1.0);
    REQUIRE(evaluatePitchExpressionCurve(positive, 1.0) < mid);
    REQUIRE(evaluatePitchExpressionCurve(negative, 1.0) > mid);

    // ...and opposite signs are mirror images about the straight line.
    REQUIRE(evaluatePitchExpressionCurve(positive, 1.0) - mid ==
            Approx(mid - evaluatePitchExpressionCurve(negative, 1.0)).margin(1.0e-5));
}

TEST_CASE("A bent segment is monotonic between its endpoints", "[pitch_expression]") {
    // A glide that doubles back would sound like a pitch wobble nobody drew.
    const std::vector<MidiPitchExpressionPoint> points{pt(0.0, 0.0, 2.5), pt(1.0, 7.0)};

    double previous = evaluatePitchExpressionCurve(points, 0.0);
    for (int step = 1; step <= 100; ++step) {
        const double value = evaluatePitchExpressionCurve(points, step / 100.0);
        REQUIRE(value >= previous - 1.0e-9);
        previous = value;
    }
}

TEST_CASE("A tension below the threshold is the straight line exactly", "[pitch_expression]") {
    // The renderer and the hit test both branch on this, so it has to be the
    // same threshold and the same answer either side of it.
    const std::vector<MidiPitchExpressionPoint> points{pt(0.0, 0.0, 0.0005), pt(2.0, 4.0)};
    REQUIRE(evaluatePitchExpressionCurve(points, 1.0) == Approx(2.0));
}

TEST_CASE("Only the segment's own left point shapes it", "[pitch_expression]") {
    // Three points, one bend. The far segment must be untouched: tension is
    // owned by the point on the left of the segment it shapes, and nothing else.
    const std::vector<MidiPitchExpressionPoint> points{pt(0.0, 0.0, 2.0), pt(1.0, 2.0),
                                                       pt(2.0, 4.0)};

    REQUIRE(evaluatePitchExpressionCurve(points, 0.5) < 1.0);           // bent
    REQUIRE(evaluatePitchExpressionCurve(points, 1.5) == Approx(3.0));  // straight
}

TEST_CASE("A flat segment stays flat however it is bent", "[pitch_expression]") {
    // The shape warps the travel between the endpoints, and there is none. The
    // editor refuses the gesture here rather than pretending otherwise.
    const std::vector<MidiPitchExpressionPoint> points{pt(0.0, 3.0, 2.0), pt(2.0, 3.0)};
    REQUIRE(evaluatePitchExpressionCurve(points, 1.0) == Approx(3.0));
}

// ============================================================================
// The inverse the drag uses
// ============================================================================

TEST_CASE("The tension for a point puts the curve through that point", "[pitch_expression]") {
    // What the drag promises: bend until the curve is under the cursor. Checked
    // by running the answer back through the evaluator.
    for (const double t : {0.25, 0.5, 0.75}) {
        for (const double target : {0.2, 0.35, 0.65, 0.8}) {
            const double tension = pitchExpressionTensionThrough(t, target);
            const std::vector<MidiPitchExpressionPoint> points{pt(0.0, 0.0, tension), pt(1.0, 1.0)};

            INFO("t " << t << " target " << target << " tension " << tension);
            REQUIRE(evaluatePitchExpressionCurve(points, t) == Approx(target).margin(1.0e-3));
        }
    }
}

TEST_CASE("Asking for the straight line gives no tension", "[pitch_expression]") {
    REQUIRE(pitchExpressionTensionThrough(0.5, 0.5) == Approx(0.0).margin(1.0e-9));
    REQUIRE(pitchExpressionTensionThrough(0.25, 0.25) == Approx(0.0).margin(1.0e-9));
}

TEST_CASE("The inverse stays in range however far the drag goes", "[pitch_expression]") {
    // A drag runs off the top and bottom of the note row, and past the segment's
    // own ends. None of that may produce a tension the evaluator cannot use.
    for (const double t : {-4.0, 0.0, 0.5, 1.0, 6.0}) {
        for (const double target : {-9.0, 0.0, 0.5, 1.0, 12.0}) {
            const double tension = pitchExpressionTensionThrough(t, target);
            INFO("t " << t << " target " << target);
            REQUIRE(std::isfinite(tension));
            REQUIRE(std::abs(tension) <= kMaxPitchExpressionTension);
        }
    }
}
