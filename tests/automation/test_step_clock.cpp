#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "magda/daw/audio/sequencer/StepClock.hpp"

using StepClock = magda::daw::audio::sequencer::StepClock;
using Approx = Catch::Approx;

// ============================================================================
// Rate conversion
// ============================================================================

TEST_CASE("StepClock rateToBeats returns correct subdivisions", "[stepclock][rate]") {
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Quarter) == 1.0);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Eighth) == 0.5);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Sixteenth) == 0.25);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::ThirtySecond) == 0.125);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedQuarter) == 1.5);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedEighth) == 0.75);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedSixteenth) == 0.375);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletQuarter) == Approx(2.0 / 3.0));
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletEighth) == Approx(1.0 / 3.0));
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletSixteenth) == Approx(0.5 / 3.0));
}

TEST_CASE("StepClock dotted rates are 1.5x their base", "[stepclock][rate]") {
    double quarter = StepClock::rateToBeats(StepClock::Rate::Quarter);
    double dottedQuarter = StepClock::rateToBeats(StepClock::Rate::DottedQuarter);
    REQUIRE(dottedQuarter == Approx(quarter * 1.5));

    double eighth = StepClock::rateToBeats(StepClock::Rate::Eighth);
    double dottedEighth = StepClock::rateToBeats(StepClock::Rate::DottedEighth);
    REQUIRE(dottedEighth == Approx(eighth * 1.5));

    double sixteenth = StepClock::rateToBeats(StepClock::Rate::Sixteenth);
    double dottedSixteenth = StepClock::rateToBeats(StepClock::Rate::DottedSixteenth);
    REQUIRE(dottedSixteenth == Approx(sixteenth * 1.5));
}

TEST_CASE("StepClock triplet rates are 2/3 of their base", "[stepclock][rate]") {
    double quarter = StepClock::rateToBeats(StepClock::Rate::Quarter);
    double tripletQuarter = StepClock::rateToBeats(StepClock::Rate::TripletQuarter);
    REQUIRE(tripletQuarter == Approx(quarter * 2.0 / 3.0));

    double eighth = StepClock::rateToBeats(StepClock::Rate::Eighth);
    double tripletEighth = StepClock::rateToBeats(StepClock::Rate::TripletEighth);
    REQUIRE(tripletEighth == Approx(eighth * 2.0 / 3.0));
}

// ============================================================================
// Ramp curve — bezier mode (smooth)
// ============================================================================

TEST_CASE("StepClock applyRampCurve identity when depth is zero", "[stepclock][ramp]") {
    // With depth=0, curve should be identity (t → t) regardless of skew
    for (float skew : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        for (double t = 0.0; t <= 1.0; t += 0.1) {
            REQUIRE(StepClock::applyRampCurve(t, 0.0f, skew) == Approx(t).margin(0.002));
        }
    }
}

TEST_CASE("StepClock applyRampCurve endpoints are fixed", "[stepclock][ramp]") {
    // Curve always maps 0→0 and 1→1
    for (float depth : {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f}) {
        for (float skew : {-0.8f, 0.0f, 0.8f}) {
            REQUIRE(StepClock::applyRampCurve(0.0, depth, skew) == Approx(0.0).margin(0.001));
            REQUIRE(StepClock::applyRampCurve(1.0, depth, skew) == Approx(1.0).margin(0.001));
        }
    }
}

TEST_CASE("StepClock applyRampCurve is monotonically increasing", "[stepclock][ramp]") {
    for (float depth : {-0.8f, -0.3f, 0.3f, 0.8f}) {
        for (float skew : {-0.5f, 0.0f, 0.5f}) {
            double prev = -1.0;
            for (int i = 0; i <= 100; ++i) {
                double t = static_cast<double>(i) / 100.0;
                double val = StepClock::applyRampCurve(t, depth, skew);
                REQUIRE(val >= prev - 1e-10);
                prev = val;
            }
        }
    }
}

TEST_CASE("StepClock applyRampCurve positive depth bows above diagonal", "[stepclock][ramp]") {
    // At t=0.5, positive depth should produce value > 0.5 (front-loaded)
    double mid = StepClock::applyRampCurve(0.5, 0.5f, 0.0f);
    REQUIRE(mid > 0.5);
}

TEST_CASE("StepClock applyRampCurve negative depth bows below diagonal", "[stepclock][ramp]") {
    // At t=0.5, negative depth should produce value < 0.5 (back-loaded)
    double mid = StepClock::applyRampCurve(0.5, -0.5f, 0.0f);
    REQUIRE(mid < 0.5);
}

TEST_CASE("StepClock applyRampCurve output stays in [0, 1]", "[stepclock][ramp]") {
    for (float depth : {-0.99f, -0.5f, 0.0f, 0.5f, 0.99f}) {
        for (float skew : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            for (int i = 0; i <= 100; ++i) {
                double t = static_cast<double>(i) / 100.0;
                double val = StepClock::applyRampCurve(t, depth, skew);
                REQUIRE(val >= -0.01);
                REQUIRE(val <= 1.01);
            }
        }
    }
}

// ============================================================================
// Ramp curve — hard angle (piecewise linear)
// ============================================================================

TEST_CASE("StepClock hard angle endpoints are fixed", "[stepclock][hardangle]") {
    for (float depth : {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f}) {
        for (float skew : {-0.8f, 0.0f, 0.8f}) {
            REQUIRE(StepClock::applyRampCurve(0.0, depth, skew, true) == Approx(0.0).margin(0.001));
            REQUIRE(StepClock::applyRampCurve(1.0, depth, skew, true) == Approx(1.0).margin(0.001));
        }
    }
}

TEST_CASE("StepClock hard angle identity when depth is zero", "[stepclock][hardangle]") {
    for (float skew : {-1.0f, 0.0f, 1.0f}) {
        for (double t = 0.0; t <= 1.0; t += 0.1) {
            REQUIRE(StepClock::applyRampCurve(t, 0.0f, skew, true) == Approx(t).margin(0.002));
        }
    }
}

TEST_CASE("StepClock hard angle is monotonically increasing", "[stepclock][hardangle]") {
    for (float depth : {-0.8f, -0.3f, 0.3f, 0.8f}) {
        for (float skew : {-0.5f, 0.0f, 0.5f}) {
            double prev = -1.0;
            for (int i = 0; i <= 100; ++i) {
                double t = static_cast<double>(i) / 100.0;
                double val = StepClock::applyRampCurve(t, depth, skew, true);
                REQUIRE(val >= prev - 1e-10);
                prev = val;
            }
        }
    }
}

TEST_CASE("StepClock hard angle passes through control point", "[stepclock][hardangle]") {
    float depth = 0.4f;
    float skew = 0.2f;
    // Control point x = 0.5 + skew * 0.49 = 0.598
    // Control point y = s + depth = 0.598 + 0.4 = 0.998
    double s = 0.5 + static_cast<double>(skew) * 0.49;
    double expectedY = s + static_cast<double>(depth);
    double val = StepClock::applyRampCurve(s, depth, skew, true);
    REQUIRE(val == Approx(expectedY).margin(0.001));
}

TEST_CASE("StepClock hard angle is piecewise linear", "[stepclock][hardangle]") {
    // Two line segments should have constant slope within each segment
    float depth = 0.3f;
    float skew = 0.0f;
    double s = 0.5 + static_cast<double>(skew) * 0.49;

    // Check linearity in first segment (0 to s)
    double y_quarter = StepClock::applyRampCurve(s * 0.25, depth, skew, true);
    double y_half = StepClock::applyRampCurve(s * 0.5, depth, skew, true);
    double y_three_quarter = StepClock::applyRampCurve(s * 0.75, depth, skew, true);
    // Midpoint should be average of quarter and three-quarter
    REQUIRE(y_half == Approx((y_quarter + y_three_quarter) / 2.0).margin(0.001));
}

TEST_CASE("StepClock hard angle vs bezier differ at midpoints", "[stepclock][hardangle]") {
    // Hard angle and smooth bezier should give different results for non-zero depth
    float depth = 0.5f;
    float skew = 0.0f;
    double t = 0.25;
    double bezier = StepClock::applyRampCurve(t, depth, skew, false);
    double linear = StepClock::applyRampCurve(t, depth, skew, true);
    REQUIRE(bezier != Approx(linear).margin(0.01));
}

// ============================================================================
// Ramp curve — symmetry and skew
// ============================================================================

TEST_CASE("StepClock applyRampCurve with zero depth and zero skew is symmetric about midpoint",
          "[stepclock][ramp]") {
    // Symmetry f(0.5-dt) + f(0.5+dt) == 1.0 only holds when depth=0 (identity curve).
    // With depth != 0 the control point moves off the diagonal, breaking symmetry.
    float depth = 0.0f;
    for (double dt = 0.05; dt < 0.5; dt += 0.05) {
        double lo = StepClock::applyRampCurve(0.5 - dt, depth, 0.0f);
        double hi = StepClock::applyRampCurve(0.5 + dt, depth, 0.0f);
        REQUIRE(lo + hi == Approx(1.0).margin(0.01));
    }
}

TEST_CASE("StepClock applyRampCurve skew shifts the inflection point", "[stepclock][ramp]") {
    // Positive skew shifts the control point right → left half is flatter (lower values).
    // At t=0.3, negative skew (control point shifted left) bends the curve up earlier,
    // giving higher values than positive skew.
    float depth = 0.3f;
    double valPosSkew = StepClock::applyRampCurve(0.3, depth, 0.5f);
    double valNegSkew = StepClock::applyRampCurve(0.3, depth, -0.5f);
    REQUIRE(valNegSkew > valPosSkew);
}

TEST_CASE("StepClock applyRampCurveWithCycles matches single-cycle curve",
          "[stepclock][ramp][cycles]") {
    for (double t : {0.0, 0.1, 0.25, 0.5, 0.9, 1.0}) {
        REQUIRE(StepClock::applyRampCurveWithCycles(t, 0.4f, -0.2f, 1, false) ==
                Approx(StepClock::applyRampCurve(t, 0.4f, -0.2f, false)).margin(0.000001));
    }
}

TEST_CASE("StepClock applyRampCurveWithCycles preserves repeated segment boundaries",
          "[stepclock][ramp][cycles]") {
    REQUIRE(StepClock::applyRampCurveWithCycles(0.0, 0.5f, 0.0f, 4, true) ==
            Approx(0.0).margin(0.000001));
    REQUIRE(StepClock::applyRampCurveWithCycles(0.25, 0.5f, 0.0f, 4, true) ==
            Approx(0.25).margin(0.000001));
    REQUIRE(StepClock::applyRampCurveWithCycles(0.5, 0.5f, 0.0f, 4, true) ==
            Approx(0.5).margin(0.000001));
    REQUIRE(StepClock::applyRampCurveWithCycles(0.75, 0.5f, 0.0f, 4, true) ==
            Approx(0.75).margin(0.000001));
    REQUIRE(StepClock::applyRampCurveWithCycles(1.0, 0.5f, 0.0f, 4, true) ==
            Approx(1.0).margin(0.000001));
}

// ============================================================================
// processBlock — reachable directly since the clock took a BlockTiming (#2313)
// ============================================================================

namespace {

/// Drive the clock over a run of equal blocks and collect every step event.
/// 120 bpm and 512-sample blocks at 48 kHz: one block is 512/48000 s, which at
/// 2 beats/sec is 512/24000 beats.
struct ClockRunner {
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlockSamples = 512;
    static constexpr double kBeatsPerBlock = kBlockSamples / 24000.0;

    StepClock clock;
    int blockSamples = kBlockSamples;
    double beat = 0.0;
    std::vector<StepClock::StepEvent> events;

    explicit ClockRunner(int samplesPerBlock = kBlockSamples) : blockSamples(samplesPerBlock) {
        clock.setSampleRate(kSampleRate);
    }

    double beatsPerBlock() const {
        return blockSamples / 24000.0;
    }

    /// Blocks needed to cover @p beats of timeline at this block size.
    int blocksFor(double beats) const {
        return static_cast<int>(beats / beatsPerBlock()) + 1;
    }

    void run(int blocks, StepClock::Rate rate, StepClock::Direction dir, int numSteps,
             float swing = 0.0f, bool playing = true) {
        StepClock::StepEvent buffer[16];
        for (int i = 0; i < blocks; ++i) {
            const StepClock::BlockTiming timing{.startBeat = beat,
                                                .endBeat = beat + beatsPerBlock(),
                                                .isPlaying = playing,
                                                .numSamples = blockSamples};
            int n = clock.processBlock(timing, rate, dir, swing, numSteps, buffer, 16);
            for (int e = 0; e < n; ++e)
                events.push_back(buffer[e]);
            beat += beatsPerBlock();
        }
    }

    std::vector<int> stepIndices() const {
        std::vector<int> out;
        out.reserve(events.size());
        for (const auto& e : events)
            out.push_back(e.stepIndex);
        return out;
    }
};

}  // namespace

TEST_CASE("StepClock emits nothing while the transport is stopped", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(64, StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8, 0.0f,
               /*playing=*/false);
    REQUIRE(runner.events.empty());
    REQUIRE_FALSE(runner.clock.isRunning());
}

TEST_CASE("StepClock steps forward at the requested rate", "[stepclock][process]") {
    ClockRunner runner;
    // One bar of 4/4 at 1/16 = 16 steps = 4 beats.
    runner.run(runner.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8);

    REQUIRE(runner.events.size() >= 16);
    // Consecutive steps sit one sixteenth (0.25 beats) apart.
    for (size_t i = 1; i < runner.events.size(); ++i) {
        REQUIRE(runner.events[i].beatPosition - runner.events[i - 1].beatPosition ==
                Approx(0.25).margin(1e-9));
    }
    // Forward over 8 steps wraps 0..7, 0..7.
    auto indices = runner.stepIndices();
    for (size_t i = 0; i < indices.size(); ++i)
        REQUIRE(indices[i] == static_cast<int>(i % 8));
}

TEST_CASE("StepClock reverse direction walks the pattern backwards", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(runner.blocksFor(2.0), StepClock::Rate::Sixteenth, StepClock::Direction::Reverse, 4);

    auto indices = runner.stepIndices();
    REQUIRE(indices.size() >= 8);
    // First event lands on step 0, then walks down and wraps: 0, 3, 2, 1, 0, ...
    const int expected[] = {0, 3, 2, 1};
    for (size_t i = 0; i < indices.size(); ++i)
        REQUIRE(indices[i] == expected[i % 4]);
}

TEST_CASE("StepClock ping-pong turns around at both ends", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(runner.blocksFor(2.0), StepClock::Rate::Sixteenth, StepClock::Direction::PingPong,
               4);

    auto indices = runner.stepIndices();
    REQUIRE(indices.size() >= 6);
    // 0, 1, 2, 3, 2, 1, 0, 1, ...
    const int expected[] = {0, 1, 2, 3, 2, 1};
    for (size_t i = 0; i < std::min<size_t>(indices.size(), 6); ++i)
        REQUIRE(indices[i] == expected[i]);
}

TEST_CASE("StepClock events carry an in-block time inside the block", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(runner.blocksFor(2.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8);

    const double blockSecs = ClockRunner::kBlockSamples / ClockRunner::kSampleRate;
    REQUIRE_FALSE(runner.events.empty());
    for (const auto& e : runner.events) {
        REQUIRE(e.timeInBlock >= 0.0);
        REQUIRE(e.timeInBlock < blockSecs);
    }
}

TEST_CASE("StepClock swing pushes a tick later on the grid, never earlier",
          "[stepclock][process]") {
    // Whatever swing survives the emit guard below, it only ever delays: an
    // even tick sits exactly on the sixteenth grid and an odd one after it.
    ClockRunner swung{4096};
    swung.run(swung.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8,
              0.25f);

    REQUIRE(swung.events.size() >= 4);
    bool sawDelayed = false;
    for (const auto& e : swung.events) {
        const double offGrid = e.beatPosition - std::floor(e.beatPosition / 0.25) * 0.25;
        REQUIRE(offGrid >= -1e-9);
        REQUIRE(offGrid < 0.25);
        if (offGrid > 1e-6)
            sawDelayed = true;
    }
    REQUIRE(sawDelayed);
}

TEST_CASE("StepClock plays a swung tick late instead of dropping it", "[stepclock][process]") {
    // Regression for the defect #2313 surfaced: the emit guard used to accept a
    // swung position only inside the block that scheduled it, while the step
    // cursor advanced regardless, so a tick swung past the block end was never
    // played. Swing offsets by up to half a step - several blocks at a typical
    // buffer size - so from roughly 8% upward every odd tick vanished and the
    // pattern played half its notes. A swung tick is now held until the block
    // that contains it.
    ClockRunner straight;
    straight.run(straight.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward,
                 8, 0.0f);

    ClockRunner swung;
    swung.run(swung.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8,
              0.5f);

    REQUIRE(straight.events.size() >= 16);
    REQUIRE(swung.events.size() == straight.events.size());
    // Same steps, same order - only their timing moved.
    REQUIRE(swung.stepIndices() == straight.stepIndices());
    for (size_t i = 0; i < swung.events.size(); ++i) {
        if (i % 2 == 0)
            REQUIRE(swung.events[i].beatPosition ==
                    Approx(straight.events[i].beatPosition).margin(1e-9));
        else
            REQUIRE(swung.events[i].beatPosition > straight.events[i].beatPosition);
    }
}

TEST_CASE("StepClock keeps a held-over tick inside the block that plays it",
          "[stepclock][process]") {
    // The carried tick reports a position and in-block offset belonging to the
    // block that emits it, not the one that scheduled it.
    ClockRunner swung;
    const double blockSecs = ClockRunner::kBlockSamples / ClockRunner::kSampleRate;
    swung.run(swung.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8,
              0.5f);

    REQUIRE_FALSE(swung.events.empty());
    for (const auto& e : swung.events) {
        REQUIRE(e.timeInBlock >= 0.0);
        REQUIRE(e.timeInBlock < blockSecs);
    }
}

TEST_CASE("StepClock survives an arrangement loop wrapping the beat", "[stepclock][process]") {
    // Beats jump backwards at a loop boundary; the clock's monotonic offset
    // must keep stepping rather than re-anchoring and dropping steps.
    StepClock clock;
    clock.setSampleRate(ClockRunner::kSampleRate);
    StepClock::StepEvent buffer[16];

    int total = 0;
    double beat = 0.0;
    for (int i = 0; i < 400; ++i) {
        // Four bars of 4/4, then wrap to zero.
        if (beat >= 16.0)
            beat = 0.0;
        const StepClock::BlockTiming timing{.startBeat = beat,
                                            .endBeat = beat + ClockRunner::kBeatsPerBlock,
                                            .isPlaying = true,
                                            .numSamples = ClockRunner::kBlockSamples};
        total += clock.processBlock(timing, StepClock::Rate::Sixteenth,
                                    StepClock::Direction::Forward, 0.0f, 8, buffer, 16);
        beat += ClockRunner::kBeatsPerBlock;
    }

    // 400 blocks is 400 * 512/24000 beats = ~8.53 beats of stepping per pass;
    // at a sixteenth per step that is ~34 steps whatever the wrap did.
    REQUIRE(total > 30);
}

TEST_CASE("StepClock stops and resets when the transport stops", "[stepclock][process]") {
    ClockRunner runner;
    const int blocks = runner.blocksFor(1.0);
    runner.run(blocks, StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8);
    REQUIRE_FALSE(runner.events.empty());

    const size_t whilePlaying = runner.events.size();
    runner.run(blocks, StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8, 0.0f,
               /*playing=*/false);
    REQUIRE(runner.events.size() == whilePlaying);
    REQUIRE(runner.clock.getCurrentStep() == 0);
}

// ============================================================================
// The pending queue — steps held for the block that actually contains them
// (#2313), and never lost on the way there (#2335)
// ============================================================================

namespace {

/// Drive the clock over blocks of an arbitrary musical length, with quantize.
/// Quantize snaps a tick toward a coarse grid, which can push it a long way
/// past the block that scheduled it - much further than swing's half a step -
/// so it is how a test builds up several pending steps at once.
struct QuantizedRunner {
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlockSamples = 512;

    StepClock clock;
    double beat = 0.0;
    std::vector<StepClock::StepEvent> events;

    QuantizedRunner() {
        clock.setSampleRate(kSampleRate);
    }

    /// One block spanning @p beats, taking at most @p maxEvents events out.
    int block(double beats, int numSteps, int quantizeSub, int maxEvents) {
        StepClock::StepEvent buffer[32];
        const StepClock::BlockTiming timing{.startBeat = beat,
                                            .endBeat = beat + beats,
                                            .isPlaying = true,
                                            .numSamples = kBlockSamples};
        const int n = clock.processBlock(timing, StepClock::Rate::Sixteenth,
                                         StepClock::Direction::Forward, 0.0f, numSteps, buffer,
                                         maxEvents, 0.0f, 0.0f, 1, false, 1.0f, quantizeSub);
        for (int e = 0; e < n; ++e)
            events.push_back(buffer[e]);
        beat += beats;
        return n;
    }
};

}  // namespace

TEST_CASE("StepClock holds a due step the caller has no room for", "[stepclock][process]") {
    // Regression for #2335. A pending step whose beat fell inside the block was
    // emitted when there was room and DROPPED when there was not: the loop's
    // "keep it pending" branch only covered steps still in the future, so a
    // full event buffer took the step out of the pattern for good. It now waits
    // for a block with room, as a step past the block end always did.
    //
    // Four sixteenths a cycle is one beat, and a quantize grid of one division
    // per cycle snaps everything to a whole beat: ticks at 0.5 and 0.75 both
    // land on beat 1, past a block that ends there, so two steps go pending.
    QuantizedRunner runner;
    runner.block(/*beats=*/1.0, /*numSteps=*/4, /*quantizeSub=*/1, /*maxEvents=*/32);

    // Room for one of the two, so step 2 plays and step 3 has to wait.
    REQUIRE(runner.block(1.0, 4, 1, /*maxEvents=*/1) == 1);
    REQUIRE(runner.events.back().stepIndex == 2);
    const size_t afterSqueeze = runner.events.size();

    // Step 3 is still owed, and is the first thing the next block plays.
    // Before the fix it was gone and this block opened on a fresh tick.
    REQUIRE(runner.block(1.0, 4, 1, /*maxEvents=*/32) >= 1);
    REQUIRE(runner.events.size() > afterSqueeze);
    REQUIRE(runner.events[afterSqueeze].stepIndex == 3);
}

TEST_CASE("StepClock holds every step a coarse quantize pushes past the block",
          "[stepclock][process]") {
    // Regression for #2335. The pending queue was eight entries deep, sized for
    // swing - which can only push a tick within half a step of the block end,
    // so at most one at a time. Quantize snaps toward a grid that may be far
    // coarser than the rate, and a big offline block can push a whole half of
    // the pattern past its end at once; the ninth onwards were dropped with no
    // branch to hold them. The queue is now one entry per step.
    //
    // 32 sixteenths is an eight-beat cycle, and one grid division per cycle
    // snaps each tick to the nearer of beat 0 and beat 8: steps 0-15 fire at
    // the block's start, steps 16-31 all land on its end.
    QuantizedRunner runner;
    REQUIRE(runner.block(/*beats=*/8.0, /*numSteps=*/32, /*quantizeSub=*/1, /*maxEvents=*/32) ==
            16);

    const size_t afterFirst = runner.events.size();
    runner.block(/*beats=*/0.5, /*numSteps=*/32, /*quantizeSub=*/1, /*maxEvents=*/32);

    std::vector<int> carried;
    for (size_t i = afterFirst; i < runner.events.size(); ++i)
        carried.push_back(runner.events[i].stepIndex);

    // All sixteen, not the eight the old queue had room for.
    for (int step = 16; step < 32; ++step)
        REQUIRE(std::find(carried.begin(), carried.end(), step) != carried.end());
}

TEST_CASE("StepClock brings its position back inside a pattern that shrank",
          "[stepclock][process]") {
    // Regression for #2335. The cursor was emitted unwrapped, so shortening a
    // pattern under a running clock kept playing the steps the user had just
    // hidden - and in ping-pong the turnaround then walked down through the
    // rest of them rather than back into the pattern.
    for (auto direction : {StepClock::Direction::Forward, StepClock::Direction::Reverse,
                           StepClock::Direction::PingPong}) {
        ClockRunner runner;
        // Four beats of sixteenths is sixteen steps, so the cursor is well past
        // the eight the pattern is about to become.
        runner.run(runner.blocksFor(4.0), StepClock::Rate::Sixteenth, direction, 32);
        const size_t beforeShrink = runner.events.size();
        REQUIRE(beforeShrink > 8);

        runner.run(runner.blocksFor(4.0), StepClock::Rate::Sixteenth, direction, 8);
        REQUIRE(runner.events.size() > beforeShrink);
        for (size_t i = beforeShrink; i < runner.events.size(); ++i)
            REQUIRE(runner.events[i].stepIndex < 8);
    }
}
