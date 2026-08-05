#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/TempoUtils.hpp"
#include "transport/TempoMap.hpp"

using magda::engine::BarsAndBeats;
using magda::engine::TempoChange;
using magda::engine::TempoMap;
using magda::engine::TimeSignatureChange;

namespace {

Catch::Approx approx(double value) {
    return Catch::Approx(value).margin(1e-9);
}

/// Loose enough for a curve that is baked into sections rather than
/// integrated, tight enough that a wrong shape fails.
Catch::Approx roughly(double value) {
    return Catch::Approx(value).epsilon(0.01);
}

}  // namespace

TEST_CASE("A beat is a quarter note at the tempo it falls under", "[engine][transport][tempo]") {
    SECTION("nothing configured is 120 in 4/4") {
        const TempoMap map;

        CHECK(map.bpmAt(0.0) == approx(120.0));
        CHECK(map.beatToTime(4.0) == approx(2.0));
        CHECK(map.timeToBeat(2.0) == approx(4.0));
        CHECK(map.barsAndBeatsAt(4.0).numerator == 4);
    }

    SECTION("both directions agree wherever you ask") {
        const TempoMap map({{0.0, 90.0, 0.0f}, {16.0, 150.0, 0.0f}}, {});

        for (const auto beat : {0.0, 3.5, 15.999, 16.0, 40.0})
            CHECK(map.timeToBeat(map.beatToTime(beat)) == approx(beat));
    }

    SECTION("a single tempo is the whole timeline") {
        const TempoMap map({{0.0, 60.0, 0.0f}}, {});

        CHECK(map.beatToTime(8.0) == approx(8.0));
        CHECK(map.bpmAt(1000.0) == approx(60.0));
    }

    SECTION("consecutive changes travel between each other") {
        // The tempo track is a curve the user draws, so two points at
        // different tempi are a ramp from one to the other, the same way two
        // points on any other automation lane are. A step is written as two
        // points at the same beat, which is what the curve editor does when
        // they share an x position.
        const TempoMap map({{0.0, 120.0, 0.0f}, {8.0, 60.0, 0.0f}}, {});

        CHECK(map.bpmAt(0.0) == approx(120.0));
        CHECK(map.bpmAt(8.0) == approx(60.0));
        CHECK(map.bpmAt(4.0) == roughly(90.0));

        // Slower than eight beats at 120 (4 s), faster than eight at 60 (8 s),
        // and past the ramp the last tempo is held.
        CHECK(map.beatToTime(8.0) > 4.0);
        CHECK(map.beatToTime(8.0) < 8.0);
        CHECK(map.beatToTime(16.0) - map.beatToTime(8.0) == approx(8.0));
    }

    SECTION("what precedes the first change is that change, held") {
        // The tempo track starts at bar 3. The bars before it are not some
        // default: they are the tempo the project actually opens with.
        const TempoMap map({{8.0, 60.0, 0.0f}}, {});

        CHECK(map.bpmAt(0.0) == approx(60.0));
        CHECK(map.beatToTime(8.0) == approx(8.0));
    }

    SECTION("the timeline exists before beat zero") {
        // Where a count-in happens. Extrapolated from the first section rather
        // than clamped, so a roll-in of four beats is four beats long.
        const TempoMap map;

        CHECK(map.beatToTime(-4.0) == approx(-2.0));
        CHECK(map.timeToBeat(-2.0) == approx(-4.0));
        CHECK(map.barsAndBeatsAt(-4.0).bar == -1);
    }
}

TEST_CASE("Tempo ramps between changes", "[engine][transport][tempo]") {
    SECTION("a straight ramp arrives at both ends and takes the middle route") {
        const TempoMap map({{0.0, 60.0, 0.0f}, {8.0, 120.0, 0.0f}}, {});

        CHECK(map.bpmAt(0.0) == approx(60.0));
        CHECK(map.bpmAt(4.0) == roughly(90.0));

        // Faster than eight beats at 60 (8 s), slower than eight at 120 (4 s).
        const auto rampSeconds = map.beatToTime(8.0);
        CHECK(rampSeconds < 8.0);
        CHECK(rampSeconds > 4.0);

        // The integral of 60/bpm across the ramp is 8 ln 2, or 5.545 s. The
        // sections hold the tempo at their start rather than across their
        // middle, so the sum runs slightly long, by about one part in a
        // hundred at the steepest ramp the model allows. That is the
        // approximation the map documents, measured rather than assumed.
        CHECK(rampSeconds > 5.545);
        CHECK(rampSeconds < 5.545 * 1.02);
    }

    SECTION("tension moves where the tempo spends its time") {
        const TempoMap straight({{0.0, 60.0, 0.0f}, {8.0, 120.0, 0.0f}}, {});
        const TempoMap holdsOld({{0.0, 60.0, 0.75f}, {8.0, 120.0, 0.0f}}, {});
        const TempoMap arrivesEarly({{0.0, 60.0, -0.75f}, {8.0, 120.0, 0.0f}}, {});

        // Holding the slower tempo longer makes the same eight beats take
        // longer; arriving at the faster one sooner makes them take less.
        CHECK(holdsOld.beatToTime(8.0) > straight.beatToTime(8.0));
        CHECK(arrivesEarly.beatToTime(8.0) < straight.beatToTime(8.0));

        // Both still start and end at the tempi that were asked for.
        CHECK(holdsOld.bpmAt(0.0) == approx(60.0));
        CHECK(arrivesEarly.bpmAt(0.0) == approx(60.0));
        CHECK(holdsOld.bpmAt(8.0) == approx(120.0));
        CHECK(arrivesEarly.bpmAt(8.0) == approx(120.0));
    }

    SECTION("a step is two changes at the same beat") {
        const TempoMap map({{0.0, 60.0, 0.0f}, {8.0, 60.0, 0.0f}, {8.0, 120.0, 0.0f}}, {});

        // Nothing ramps into beat 8, because the change arriving there is the
        // tempo already playing.
        CHECK(map.bpmAt(4.0) == approx(60.0));
        CHECK(map.beatToTime(8.0) == approx(8.0));
        CHECK(map.bpmAt(8.0) == approx(120.0));
        CHECK(map.beatToTime(12.0) == approx(10.0));
    }
}

TEST_CASE("Bars come from the time signature, beats do not",
          "[engine][transport][tempo][signature]") {
    SECTION("4/4") {
        const TempoMap map;

        CHECK(map.barsAndBeatsAt(0.0).bar == 0);
        CHECK(map.barsAndBeatsAt(3.5).bar == 0);
        CHECK(map.barsAndBeatsAt(3.5).beat == approx(3.5));
        CHECK(map.barsAndBeatsAt(4.0).bar == 1);
        CHECK(map.barsAndBeatsAt(4.0).beat == approx(0.0));
    }

    SECTION("3/4 groups the same beats differently") {
        const TempoMap map({}, {{0.0, 3, 4}});

        CHECK(map.barsAndBeatsAt(3.0).bar == 1);
        CHECK(map.beatToTime(3.0) == approx(1.5));  // still quarter notes at 120
    }

    SECTION("6/8 counts eighths inside a bar of three quarter notes") {
        const TempoMap map({}, {{0.0, 6, 8}});

        CHECK(map.barsAndBeatsAt(1.5).bar == 0);
        CHECK(map.barsAndBeatsAt(1.5).beat == approx(3.0));
        CHECK(map.barsAndBeatsAt(3.0).bar == 1);

        // The beat did not change length: a bar of 6/8 is three quarter notes,
        // which at 120 is a second and a half whatever it is written as.
        CHECK(map.beatToTime(3.0) == approx(1.5));
    }

    SECTION("a signature change starts a bar wherever it lands") {
        // Two bars of 4/4, then 3/4 from a beat that is not a bar line.
        const TempoMap map({}, {{0.0, 4, 4}, {9.0, 3, 4}});

        CHECK(map.barsAndBeatsAt(8.0).bar == 2);
        CHECK(map.barsAndBeatsAt(9.0).bar == 3);
        CHECK(map.barsAndBeatsAt(9.0).beat == approx(0.0));
        CHECK(map.barsAndBeatsAt(12.0).bar == 4);
    }
}

TEST_CASE("The metronome grid follows the signature", "[engine][transport][tempo][signature]") {
    SECTION("a beat sitting on a tick is that tick") {
        const TempoMap map;

        const auto tick = map.tickAtOrAfter(0.0);
        CHECK(tick.beat == approx(0.0));
        CHECK(tick.nextBeat == approx(1.0));
        CHECK(tick.startsBar);

        CHECK(map.tickAtOrAfter(0.25).beat == approx(1.0));
        CHECK(!map.tickAtOrAfter(1.0).startsBar);
        CHECK(map.tickAtOrAfter(4.0).startsBar);
    }

    SECTION("6/8 ticks on eighths and accents every sixth") {
        const TempoMap map({}, {{0.0, 6, 8}});

        auto tick = map.tickAtOrAfter(0.0);
        CHECK(tick.beat == approx(0.0));
        CHECK(tick.nextBeat == approx(0.5));
        CHECK(tick.startsBar);

        auto accents = 0;
        auto ticks = 0;
        for (tick = map.tickAtOrAfter(0.0); tick.beat < 3.0;
             tick = map.tickAtOrAfter(tick.nextBeat)) {
            ++ticks;
            accents += tick.startsBar ? 1 : 0;
        }

        CHECK(ticks == 6);
        CHECK(accents == 1);
    }

    SECTION("a signature change cuts the grid short") {
        const TempoMap map({}, {{0.0, 4, 4}, {3.5, 4, 4}});

        // The tick after beat 3 would be beat 4, but the signature change at
        // 3.5 is a bar line and takes its place.
        const auto tick = map.tickAtOrAfter(3.0);
        CHECK(tick.beat == approx(3.0));
        CHECK(tick.nextBeat == approx(3.5));
        CHECK(map.tickAtOrAfter(3.5).startsBar);
    }

    SECTION("ticks run backwards from the start of the timeline") {
        const TempoMap map;

        CHECK(map.tickAtOrAfter(-2.0).beat == approx(-2.0));
        CHECK(map.tickAtOrAfter(-1.5).beat == approx(-1.0));
        CHECK(map.tickAtOrAfter(-4.0).startsBar);
    }
}

TEST_CASE("A tempo map's fingerprint is what it places beats by", "[engine][transport][tempo]") {
    const TempoMap plain;
    const TempoMap same({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
    const TempoMap faster({{0.0, 140.0, 0.0f}}, {{0.0, 4, 4}});
    const TempoMap regrouped({{0.0, 120.0, 0.0f}}, {{0.0, 3, 4}});

    CHECK(plain == same);
    CHECK(!(plain == faster));
    CHECK(!(plain == regrouped));
    CHECK(plain.fingerprint() != faster.fingerprint());
}

TEST_CASE("A tempo track is taken as it is meant, not as it is written",
          "[engine][transport][tempo]") {
    SECTION("changes may arrive in any order") {
        const TempoMap ordered({{0.0, 120.0, 0.0f}, {8.0, 60.0, 0.0f}}, {});
        const TempoMap shuffled({{8.0, 60.0, 0.0f}, {0.0, 120.0, 0.0f}}, {});

        CHECK(ordered == shuffled);
        CHECK(shuffled.beatToTime(16.0) == approx(ordered.beatToTime(16.0)));
        CHECK(shuffled.bpmAt(8.0) == approx(60.0));
    }

    SECTION("a tempo outside what the model allows is clamped, not honoured") {
        const TempoMap map({{0.0, 1.0e9, 0.0f}}, {});

        CHECK(map.bpmAt(0.0) == approx(magda::MAX_VALID_BPM));
    }
}
