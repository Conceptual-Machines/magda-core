#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "transport/TransportClock.hpp"

using magda::engine::SamplePosition;
using magda::engine::TempoMap;
using magda::engine::TransportClock;
using magda::engine::TransportSnapshot;

namespace {

constexpr double kSampleRate = 44100.0;

/// 120 bpm: a beat is half a second, 22050 samples, and a bar is four of them.
constexpr double kSamplesPerBeat = kSampleRate / 2.0;

/// A position on the transport's count, which is what the clock answers in.
SamplePosition at(std::int64_t sample) {
    return SamplePosition{sample};
}

Catch::Approx approx(double value) {
    return Catch::Approx(value).margin(1e-9);
}

/// Playing from a beat, with everything else left alone.
TransportSnapshot playing(double fromBeat, std::uint64_t generation = 1) {
    TransportSnapshot snapshot;
    snapshot.request.generation = generation;
    snapshot.request.playing = true;
    snapshot.request.positionBeat = fromBeat;
    return snapshot;
}

TransportSnapshot stopped(double atBeat, std::uint64_t generation = 1) {
    TransportSnapshot snapshot;
    snapshot.request.generation = generation;
    snapshot.request.positionBeat = atBeat;
    return snapshot;
}

/// Stopping without naming a position: the play state changes and nothing
/// else does.
TransportSnapshot halt(std::uint64_t generation) {
    TransportSnapshot snapshot;
    snapshot.request.generation = generation;
    snapshot.request.locate = false;
    return snapshot;
}

/// Every segment of one callback, as a value the checks can read twice.
struct Rendered {
    std::vector<TransportClock::Segment> segments;

    int totalSamples() const {
        auto total = 0;
        for (const auto& segment : segments)
            total += segment.block.numSamples;
        return total;
    }
};

Rendered advance(TransportClock& clock, const TransportSnapshot& snapshot, int numSamples) {
    Rendered rendered;
    for (const auto& segment : clock.advance(snapshot, kSampleRate, numSamples))
        rendered.segments.push_back(segment);
    return rendered;
}

/// What every callback owes, whatever it did in the middle: the samples the
/// device asked for, each covered once, in order.
void requireCovers(const Rendered& rendered, int numSamples) {
    REQUIRE(rendered.totalSamples() == numSamples);

    auto expected = 0;
    for (const auto& segment : rendered.segments) {
        REQUIRE(segment.startSample == expected);
        expected += segment.block.numSamples;
    }
}

}  // namespace

TEST_CASE("A stopped transport still renders, and stands still", "[engine][transport][clock]") {
    TransportClock clock;
    const auto snapshot = stopped(8.0);

    const auto first = advance(clock, snapshot, 512);
    requireCovers(first, 512);
    REQUIRE(first.segments.size() == 1);

    CHECK(!first.segments[0].block.playing);
    CHECK(first.segments[0].block.beats.start == approx(8.0));
    CHECK(first.segments[0].block.beats.end == approx(8.0));
    CHECK(clock.positionBeats() == approx(8.0));

    // Standing still is not a jump. The block after it continues the one
    // before, which is what keeps a device's tail ringing out into silence.
    const auto second = advance(clock, snapshot, 512);
    CHECK(second.segments[0].block.continuous);
    CHECK(clock.positionBeats() == approx(8.0));
}

TEST_CASE("The cursor moves at the rate the tempo says", "[engine][transport][clock]") {
    TransportClock clock;
    const auto snapshot = playing(0.0);

    const auto block = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat));
    REQUIRE(block.segments.size() == 1);

    CHECK(block.segments[0].block.playing);
    CHECK(block.segments[0].block.beats.start == approx(0.0));
    CHECK(block.segments[0].block.beats.end == approx(1.0));
    CHECK(clock.positionBeats() == approx(1.0));

    // Starting is a jump; carrying on is not.
    CHECK(!block.segments[0].block.continuous);
    CHECK(advance(clock, snapshot, 512).segments[0].block.continuous);
}

TEST_CASE("Where the cursor lands does not depend on how the blocks were cut",
          "[engine][transport][clock]") {
    // The cursor is a function of the sample count rather than a sum of steps,
    // so this is exact equality rather than a tolerance: a clock that
    // accumulated per block would drift here and drift more the longer it ran.
    TransportClock whole, pieces;

    const auto snapshot = playing(0.0);
    for (auto i = 0; i < 100; ++i)
        advance(whole, snapshot, 512);

    for (auto i = 0; i < 100; ++i) {
        advance(pieces, snapshot, 128);
        advance(pieces, snapshot, 64);
        advance(pieces, snapshot, 256);
        advance(pieces, snapshot, 64);
    }

    CHECK(whole.positionBeats() == pieces.positionBeats());
}

TEST_CASE("A loop wrap cuts the callback rather than the timeline",
          "[engine][transport][clock][loop]") {
    TransportClock clock;
    auto snapshot = playing(0.0);
    snapshot.loop = {true, 0.0, 1.0};  // one beat: 22050 samples

    // Land 100 samples short of the loop end, then ask for a block that
    // crosses it.
    advance(clock, snapshot, static_cast<int>(kSamplesPerBeat) - 100);

    const auto crossing = advance(clock, snapshot, 512);
    requireCovers(crossing, 512);
    REQUIRE(crossing.segments.size() == 2);

    // Everything up to the loop end, then everything from the loop start. No
    // block straddles the wrap, so nothing downstream has to know it happened.
    CHECK(crossing.segments[0].block.numSamples == 100);
    CHECK(crossing.segments[0].block.beats.end == approx(1.0));
    CHECK(crossing.segments[0].block.continuous);

    CHECK(crossing.segments[1].startSample == 100);
    CHECK(crossing.segments[1].block.numSamples == 412);
    CHECK(crossing.segments[1].block.beats.start == approx(0.0));
    CHECK(!crossing.segments[1].block.continuous);

    CHECK(clock.loopWrapOverflows() == 0);
}

TEST_CASE("A loop is entered from before it and left where it ends",
          "[engine][transport][clock][loop]") {
    TransportClock clock;
    auto snapshot = playing(0.0);
    snapshot.loop = {true, 2.0, 4.0};

    SECTION("playing into a loop from before it is not a wrap") {
        // Playback starts where it was asked to, two beats before the loop,
        // and runs straight through the loop start: a loop is a place the
        // timeline returns to, not a pen it is put in.
        const auto entering = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 2.5));
        REQUIRE(entering.segments.size() == 1);
        CHECK(entering.segments[0].block.beats.start == approx(0.0));
        CHECK(entering.segments[0].block.beats.end == approx(2.5));

        // Carrying on from there wraps at the end of the loop, not at its
        // start.
        const auto wrapping = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 2));
        REQUIRE(wrapping.segments.size() == 2);
        CHECK(wrapping.segments[0].block.beats.end == approx(4.0));
        CHECK(wrapping.segments[1].block.beats.start == approx(2.0));
    }

    SECTION("a cursor put down past the loop plays on") {
        // The loop catches what reaches its end. It does not collect a
        // playhead that was put down somewhere else entirely, because that
        // would be ignoring where the user pointed.
        auto located = playing(9.0, 2);
        located.loop = snapshot.loop;

        const auto block = advance(clock, located, 512);
        CHECK(block.segments[0].block.beats.start == approx(9.0));
        CHECK(block.segments[0].block.beats.end > 9.0);
    }

    SECTION("a loop dragged behind the cursor leaves it playing") {
        advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 3));
        REQUIRE(clock.positionBeats() == approx(3.0));

        // Same generation: nothing was requested, the loop was dragged.
        auto moved = snapshot;
        moved.loop = {true, 0.0, 1.0};

        const auto block = advance(clock, moved, 512);
        CHECK(block.segments[0].block.beats.start == approx(3.0));
        CHECK(block.segments[0].block.continuous);
    }

    SECTION("a loop dragged around the cursor catches it at the new end") {
        advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 3));

        auto moved = snapshot;
        moved.loop = {true, 0.0, 3.5};

        const auto block = advance(clock, moved, static_cast<int>(kSamplesPerBeat));
        REQUIRE(block.segments.size() == 2);
        CHECK(block.segments[0].block.beats.end == approx(3.5));
        CHECK(block.segments[1].block.beats.start == approx(0.0));
        CHECK(!block.segments[1].block.continuous);
    }
}

TEST_CASE("Count-in rolls in before the play position", "[engine][transport][clock][countin]") {
    TransportClock clock;
    auto snapshot = playing(4.0);
    snapshot.request.countInBeats = 2.0;
    snapshot.loop = {true, 4.0, 8.0};

    SECTION("the cursor starts short of it and plays into it") {
        const auto opening = advance(clock, snapshot, 512);

        // Two beats before the play position, playing: material before the
        // start is heard rather than skipped, which is what makes a count-in
        // different from starting late.
        CHECK(opening.segments[0].block.beats.start == approx(2.0));
        CHECK(opening.segments[0].block.playing);
        CHECK(opening.segments[0].countingIn);
    }

    SECTION("the loop does not wrap a roll-in") {
        // A count-in that began before the loop end would otherwise wrap on
        // its way to the play position and count for ever without arriving.
        auto late = playing(9.0, 2);
        late.request.countInBeats = 4.0;
        late.loop = {true, 0.0, 6.0};

        advance(clock, late, static_cast<int>(kSamplesPerBeat * 3));
        CHECK(clock.positionBeats() == approx(8.0));
    }

    SECTION("the callback is cut where the count-in ends") {
        // Land 100 samples short of the play position.
        advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 2) - 100);

        const auto crossing = advance(clock, snapshot, 512);
        requireCovers(crossing, 512);
        REQUIRE(crossing.segments.size() == 2);

        CHECK(crossing.segments[0].countingIn);
        CHECK(crossing.segments[0].block.numSamples == 100);
        CHECK(!crossing.segments[1].countingIn);
        CHECK(crossing.segments[1].block.beats.start == approx(4.0));

        // Reaching the play position is not a jump: the cursor played there.
        CHECK(crossing.segments[1].block.continuous);
    }

    SECTION("the loop takes over once the count-in is done") {
        // Two beats of roll-in, the loop's four beats, and half a beat back
        // inside it: the loop was suppressed for the count-in and for nothing
        // else.
        const auto rendered = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 6.5));

        REQUIRE(rendered.segments.size() == 3);
        CHECK(clock.positionBeats() == approx(4.5));
    }
}

TEST_CASE("Editing the tempo moves the seconds, not the beat",
          "[engine][transport][clock][tempo]") {
    TransportClock clock;
    const auto snapshot = playing(0.0);

    advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 2));
    REQUIRE(clock.positionBeats() == approx(2.0));

    // Same request, half the tempo. The cursor is on beat 2 and stays there;
    // what changed is how long the next beat takes.
    auto slower = snapshot;
    slower.tempo = TempoMap({{0.0, 60.0, 0.0f}}, {});

    const auto block = advance(clock, slower, static_cast<int>(kSampleRate));
    CHECK(block.segments[0].block.beats.start == approx(2.0));
    CHECK(block.segments[0].block.beats.end == approx(3.0));

    // Not a jump. Nothing about the timeline broke: a beat is where it always
    // was, and only its arrival time moved.
    CHECK(block.segments[0].block.continuous);
}

TEST_CASE("A callback that crosses a tempo step is cut at it",
          "[engine][transport][clock][tempo]") {
    // 120 held to beat 2, then 60. Two changes at the one beat, which is how a
    // step is written rather than a ramp to it (TempoMap.hpp).
    TransportClock clock;
    auto snapshot = playing(0.0);
    snapshot.tempo = TempoMap({{0.0, 120.0, 0.0f}, {2.0, 120.0, 0.0f}, {2.0, 60.0, 0.0f}}, {});

    // A whole beat before the step, so it lands in the middle of the callback.
    advance(clock, snapshot, static_cast<int>(kSamplesPerBeat));

    const auto rendered = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 2));

    // Cut on the step, not around it: the callback is covered once, in order.
    requireCovers(rendered, static_cast<int>(kSamplesPerBeat * 2));
    REQUIRE(rendered.segments.size() == 2);

    CHECK(rendered.segments[0].block.beats.start == approx(1.0));
    CHECK(rendered.segments[0].block.beats.end == approx(2.0));
    CHECK(rendered.segments[0].block.numSamples == static_cast<int>(kSamplesPerBeat));

    // Half the tempo past it, so the same samples again are half the beats.
    CHECK(rendered.segments[1].block.beats.start == approx(2.0));
    CHECK(rendered.segments[1].block.beats.end == approx(2.5));

    // Not a jump. Audio flows straight through a tempo change, so an op that
    // reset here would invent a gap the transport never asked for.
    CHECK(rendered.segments[0].block.continuous);
    CHECK(rendered.segments[1].block.continuous);
}

TEST_CASE("A steep ramp leaves every block affine to within a sample",
          "[engine][transport][clock][tempo]") {
    // A ramp is baked into constant-tempo sections, and between two of them the
    // slope changes as surely as it does at a step. Over this one, a beat the
    // map puts at sample 256 of an uncut block lands at sample 93 on the
    // block's own line.
    //
    // That line is what a reader without a map has: the seconds face of a
    // shifted block, the monotonic offset a launcher works in, the crop
    // materialSubBlock takes. Placement itself goes through the map now
    // (RenderContext::eventForBeat), so what is asserted here is the property
    // the cut exists to provide rather than any consumer of it: inside a block,
    // the line and the map are the same answer to within the sample the block
    // is allowed. Whether anything still needs that is what decides whether the
    // cut stays (#2333).
    TransportClock clock;
    auto snapshot = playing(0.0);
    snapshot.tempo = TempoMap({{0.0, 20.0, 0.0f}, {1.0, 300.0, 0.0f}}, {});

    // Long enough to cross several sections: at 20 bpm a beat is three seconds,
    // and the ramp opens with its widest ones.
    const auto rendered = advance(clock, snapshot, static_cast<int>(kSampleRate * 2));
    requireCovers(rendered, static_cast<int>(kSampleRate * 2));

    REQUIRE(rendered.segments.size() > 1);

    for (const auto& segment : rendered.segments) {
        const auto& block = segment.block;
        REQUIRE(block.numSamples > 0);

        for (const auto through : {0.0, 0.25, 0.5, 0.75}) {
            const auto beat = block.beats.start + through * block.beats.length();

            // Where the map puts it, in samples of this block. The seconds axis
            // is exact by construction: a block runs at one second per sample
            // rate whatever the tempo does.
            const auto exact =
                (snapshot.tempo.beatToTime(beat) - block.seconds.start) * kSampleRate;

            // And where the block's own two ends put it, which is the line.
            const auto online =
                ((beat - block.beats.start) / block.beats.length()) * block.numSamples;

            CHECK(online == approx(exact).margin(1.0));
        }
    }

    // Crossing a tempo change is not a jump. Audio flows through one, so an op
    // that reset here would invent a gap the transport never asked for. The
    // first segment is the exception every callback has: starting is a jump.
    for (std::size_t i = 1; i < rendered.segments.size(); ++i)
        CHECK(rendered.segments[i].block.continuous);
}

TEST_CASE("One tempo throughout cuts nothing", "[engine][transport][clock][tempo]") {
    // The cut is bounded by what the map actually does: a project at one tempo
    // has one section, so a callback is one segment however long it is.
    TransportClock clock;
    const auto snapshot = playing(0.0);

    const auto rendered = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 8));

    requireCovers(rendered, static_cast<int>(kSamplesPerBeat * 8));
    CHECK(rendered.segments.size() == 1);
}

TEST_CASE("Monotonic samples count what the transport rolled",
          "[engine][transport][clock][samples]") {
    TransportClock clock;
    const auto snapshot = playing(0.0);

    CHECK(clock.monotonicSamples() == at(0));

    const auto first = advance(clock, snapshot, 512);
    CHECK(first.segments[0].block.monotonicSamples.start == at(0));
    CHECK(first.segments[0].block.monotonicSamples.end == at(512));
    CHECK(clock.monotonicSamples() == at(512));

    // Picks up where the last block left off, so two blocks are adjacent on
    // this axis whatever the timeline did between them.
    const auto second = advance(clock, snapshot, 512);
    CHECK(second.segments[0].block.monotonicSamples.start == at(512));
    CHECK(clock.monotonicSamples() == at(1024));
}

TEST_CASE("A stopped transport rolls no samples", "[engine][transport][clock][samples]") {
    // A stopped block still renders, so tails ring out, but nothing played:
    // the count stands still for the same reason the timeline does.
    TransportClock clock;

    advance(clock, playing(0.0), 512);
    REQUIRE(clock.monotonicSamples() == at(512));

    const auto rendered = advance(clock, halt(2), 512);

    CHECK(rendered.segments[0].block.monotonicSamples.start == at(512));
    CHECK(rendered.segments[0].block.monotonicSamples.end == at(512));
    CHECK(clock.monotonicSamples() == at(512));
}

TEST_CASE("Nothing that moves the cursor moves the sample count",
          "[engine][transport][clock][samples]") {
    // The point of the axis. A wrap takes the timeline back and a locate puts
    // it anywhere, and neither is a thing that unplays a sample.
    TransportClock clock;

    auto looping = playing(0.0);
    looping.loop = {true, 0.0, 1.0};

    // Several beats' worth at 120 bpm, so the loop wraps more than once.
    constexpr auto kSamples = static_cast<int>(kSamplesPerBeat * 3);

    const auto wrapped = advance(clock, looping, kSamples);
    requireCovers(wrapped, kSamples);

    // Cut into pieces by the wraps, and the pieces are adjacent: the count
    // carries across a boundary the timeline does not.
    for (std::size_t i = 1; i < wrapped.segments.size(); ++i)
        CHECK(wrapped.segments[i].block.monotonicSamples.start ==
              wrapped.segments[i - 1].block.monotonicSamples.end);

    CHECK(clock.monotonicSamples() == at(kSamples));

    // A locate backwards, which is where a beat-counting axis would go back.
    const auto located = advance(clock, playing(0.0, 2), 512);

    CHECK(located.segments[0].block.monotonicSamples.start == at(kSamples));
    CHECK(clock.monotonicSamples() == at(kSamples + 512));
}

TEST_CASE("A request is applied once", "[engine][transport][clock]") {
    TransportClock clock;
    const auto snapshot = playing(2.0);

    advance(clock, snapshot, 512);
    const auto second = advance(clock, snapshot, 512);

    // Republishing the same request does not drag the cursor back to where
    // playback started, which is what would happen if the position were read
    // every block rather than the generation.
    CHECK(second.segments[0].block.beats.start > 2.0);
    CHECK(second.segments[0].block.continuous);

    SECTION("a new one relocates and says so") {
        const auto located = advance(clock, playing(16.0, 2), 512);
        CHECK(located.segments[0].block.beats.start == approx(16.0));
        CHECK(!located.segments[0].block.continuous);
    }

    SECTION("stopping where the cursor already is is not a jump") {
        const auto at = clock.positionBeats();
        const auto halted = advance(clock, stopped(at, 3), 512);

        CHECK(!halted.segments[0].block.playing);
        CHECK(halted.segments[0].block.continuous);
    }
}

TEST_CASE("Stopping does not have to say where", "[engine][transport][clock]") {
    TransportClock clock;

    advance(clock, playing(0.0), 512);
    advance(clock, playing(0.0), 512);
    const auto at = clock.positionBeats();

    // The publisher cannot name where the cursor is: whatever it read from
    // positionBeats() is a callback out of date by the time the request lands.
    // Naming a stale position would step the cursor back and read as a jump,
    // so a stop says nothing about position at all.
    const auto halted = advance(clock, halt(2), 512);

    CHECK(!halted.segments[0].block.playing);
    CHECK(halted.segments[0].block.beats.start == approx(at));
    CHECK(clock.positionBeats() == approx(at));
    CHECK(halted.segments[0].block.continuous);

    SECTION("and playing again from there does not have to either") {
        auto resumed = halt(3);
        resumed.request.playing = true;

        const auto block = advance(clock, resumed, 512);
        CHECK(block.segments[0].block.beats.start == approx(at));

        // Starting is a jump wherever it starts from.
        CHECK(!block.segments[0].block.continuous);
    }
}

TEST_CASE("A change of sample rate keeps the position it was given", "[engine][transport][clock]") {
    TransportClock clock;
    const auto snapshot = playing(0.0);

    clock.advance(snapshot, kSampleRate, static_cast<int>(kSamplesPerBeat));
    REQUIRE(clock.positionBeats() == approx(1.0));

    // The anchor is a position in seconds, which survives the device changing
    // under it; the sample count taken at the old rate does not.
    clock.advance(snapshot, 48000.0, 24000);
    CHECK(clock.positionBeats() == approx(2.0));
}

TEST_CASE("A loop too short to render is reported, not silently dropped",
          "[engine][transport][clock][loop]") {
    TransportClock clock;
    auto snapshot = playing(0.0);

    // Around ten samples of loop, against a callback of five hundred: more
    // wraps than there are segments to render them as.
    snapshot.loop = {true, 0.0, 10.0 / kSamplesPerBeat};

    const auto block = advance(clock, snapshot, 512);

    // The callback is still covered exactly. What is lost is the wrapping, and
    // the count is what says so.
    requireCovers(block, 512);
    CHECK(clock.loopWrapOverflows() > 0);

    SECTION("including one whose end is not a sample away from its start") {
        // Short enough that the wrap lands where it started: the count of
        // samples to the loop end comes back zero however many times the
        // cursor is put back at the top. Without that zero reaching the
        // fallback, the callback renders straight through with nothing
        // counted, and the cursor ends past the end where no later callback
        // will wrap it.
        TransportClock tiny;
        auto degenerate = playing(0.0);
        degenerate.loop = {true, 0.0, 0.005 / kSamplesPerBeat};

        const auto first = advance(tiny, degenerate, 512);
        requireCovers(first, 512);
        CHECK(tiny.loopWrapOverflows() == 1);

        const auto second = advance(tiny, degenerate, 512);
        requireCovers(second, 512);
        CHECK(second.segments[0].block.beats.start == approx(0.0));
        CHECK(tiny.loopWrapOverflows() == 2);
    }

    SECTION("and it costs one callback, not every callback after it") {
        // Rendering the remainder straight through leaves the cursor past the
        // loop end, which is also where a cursor located beyond the loop sits.
        // Without putting it back, the next callback would read it as one that
        // was put there on purpose and never wrap again.
        const auto next = advance(clock, snapshot, 512);

        requireCovers(next, 512);
        CHECK(next.segments.size() > 1);
        CHECK(next.segments[0].block.beats.start == approx(0.0));
    }
}

TEST_CASE("The monotonic beat counts what was rolled through, not where the cursor is",
          "[engine][transport][clock][monotonic]") {
    // The third face of the instant (#2300). Beats say where the material is,
    // seconds say how much audio went by, and this says how much musical time
    // the transport has actually run through. A queued launch names a position
    // in this domain because it is the only one a loop cannot take back.
    TransportClock clock;

    SECTION("it advances with the timeline while nothing wraps") {
        const auto snapshot = playing(0.0);
        const auto block = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat) * 2);

        REQUIRE(block.segments.size() == 1);
        CHECK(block.segments[0].block.monotonicBeats.start == approx(0.0));
        CHECK(block.segments[0].block.monotonicBeats.end == approx(2.0));
        CHECK(clock.monotonicBeat() == approx(2.0));
    }

    SECTION("a loop wrap takes the timeline back and leaves this alone") {
        auto snapshot = playing(0.0);
        snapshot.loop = {true, 0.0, 2.0};

        // Five beats of playing through a two-beat loop. The cursor has been
        // round twice and is a beat into its third pass; this has counted
        // every beat of it. Five rather than six so the cursor lands inside
        // the loop rather than exactly on its end, which is where a wrap has
        // been decided but not yet taken.
        for (auto i = 0; i < 5; ++i)
            advance(clock, snapshot, static_cast<int>(kSamplesPerBeat));

        CHECK(clock.positionBeats() == approx(1.0));
        CHECK(clock.monotonicBeat() == approx(5.0));
    }

    SECTION("it never goes backwards across a wrap inside one callback") {
        auto snapshot = playing(0.0);
        snapshot.loop = {true, 0.0, 1.0};

        // A callback long enough to wrap: the timeline segments run 0->1 then
        // 0->..., and the monotonic ones have to run on end to end.
        const auto block = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat) * 2);
        REQUIRE(block.segments.size() >= 2);

        auto previous = -1.0;
        for (const auto& segment : block.segments) {
            CHECK(segment.block.monotonicBeats.start >= previous);
            CHECK(segment.block.monotonicBeats.end >= segment.block.monotonicBeats.start);
            previous = segment.block.monotonicBeats.end;
        }

        CHECK(clock.monotonicBeat() == approx(2.0));
    }

    SECTION("a locate moves the cursor and does not rewind this") {
        advance(clock, playing(8.0, 1), static_cast<int>(kSamplesPerBeat));
        CHECK(clock.monotonicBeat() == approx(1.0));

        // Back to the top of the timeline, which is a jump the cursor takes
        // and this does not: a launch queued for two beats from now must not
        // be satisfied by somebody seeking.
        advance(clock, playing(0.0, 2), static_cast<int>(kSamplesPerBeat));

        CHECK(clock.positionBeats() == approx(1.0));
        CHECK(clock.monotonicBeat() == approx(2.0));
    }

    SECTION("a stopped block does not advance it") {
        advance(clock, playing(0.0, 1), static_cast<int>(kSamplesPerBeat));
        const auto rolled = clock.monotonicBeat();

        const auto block = advance(clock, halt(2), 512);
        REQUIRE(block.segments.size() == 1);

        CHECK(block.segments[0].block.monotonicBeats.start == approx(rolled));
        CHECK(block.segments[0].block.monotonicBeats.end == approx(rolled));
        CHECK(clock.monotonicBeat() == approx(rolled));
    }

    SECTION("it does not depend on how the callbacks were cut") {
        TransportClock whole, pieces;
        const auto snapshot = playing(0.0);

        for (auto i = 0; i < 100; ++i)
            advance(whole, snapshot, 512);

        for (auto i = 0; i < 100; ++i) {
            advance(pieces, snapshot, 128);
            advance(pieces, snapshot, 64);
            advance(pieces, snapshot, 256);
            advance(pieces, snapshot, 64);
        }

        CHECK(whole.monotonicBeat() == approx(pieces.monotonicBeat()));
    }
}

TEST_CASE("The monotonic seconds count rendered time, not converted beats",
          "[engine][transport][clock][monotonic]") {
    // The fourth face (#2324), and the one a run measures its own length in.
    // The alternative was asking a tempo map how far apart two beats are, and a
    // map answers where a beat is.
    TransportClock clock;

    /// The seconds a whole number of samples lasts, which is what this counts.
    const auto secondsOf = [](int samples) { return samples / kSampleRate; };

    SECTION("it advances by the samples rendered") {
        const auto snapshot = playing(0.0);
        const auto block = advance(clock, snapshot, 512);

        REQUIRE(block.segments.size() == 1);
        CHECK(block.segments[0].block.monotonicSeconds.start == approx(0.0));
        CHECK(block.segments[0].block.monotonicSeconds.end == approx(secondsOf(512)));
        CHECK(clock.monotonicSeconds() == approx(secondsOf(512)));
    }

    SECTION("a tempo edit changes the beats and not this") {
        auto fast = playing(0.0);
        auto slow = playing(0.0);
        slow.tempo = TempoMap({{0.0, 60.0, 0.0f}}, {});

        advance(clock, fast, 4096);
        const auto afterFast = clock.monotonicSeconds();

        advance(clock, slow, 4096);

        // Half the musical time and the same wall-clock time: same samples.
        CHECK(afterFast == approx(secondsOf(4096)));
        CHECK(clock.monotonicSeconds() == approx(secondsOf(8192)));
    }

    SECTION("a loop wrap takes the timeline back and leaves this alone") {
        auto snapshot = playing(0.0);
        snapshot.loop = {true, 0.0, 2.0};

        for (auto i = 0; i < 5; ++i)
            advance(clock, snapshot, static_cast<int>(kSamplesPerBeat));

        CHECK(clock.positionBeats() == approx(1.0));
        CHECK(clock.monotonicSeconds() == approx(secondsOf(static_cast<int>(kSamplesPerBeat) * 5)));
    }

    SECTION("it never goes backwards across a wrap inside one callback") {
        auto snapshot = playing(0.0);
        snapshot.loop = {true, 0.0, 1.0};

        const auto block = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat) * 2);
        REQUIRE(block.segments.size() >= 2);

        auto previous = -1.0;
        auto rendered = 0;
        for (const auto& segment : block.segments) {
            CHECK(segment.block.monotonicSeconds.start >= previous);
            CHECK(segment.block.monotonicSeconds.end >= segment.block.monotonicSeconds.start);
            previous = segment.block.monotonicSeconds.end;
            rendered += segment.block.numSamples;
        }

        // Exactly the callback: the pieces share the samples out, none twice.
        CHECK(clock.monotonicSeconds() == approx(secondsOf(rendered)));
    }

    SECTION("a locate moves the cursor and does not rewind this") {
        advance(clock, playing(8.0, 1), static_cast<int>(kSamplesPerBeat));
        advance(clock, playing(0.0, 2), static_cast<int>(kSamplesPerBeat));

        CHECK(clock.positionBeats() == approx(1.0));
        CHECK(clock.monotonicSeconds() == approx(secondsOf(static_cast<int>(kSamplesPerBeat) * 2)));
    }

    SECTION("a count-in is time the transport rolled through") {
        auto snapshot = playing(0.0);
        snapshot.request.countInBeats = 2.0;

        advance(clock, snapshot, static_cast<int>(kSamplesPerBeat) * 3);

        // Counting in is playing: the clock runs through it.
        CHECK(clock.monotonicSeconds() == approx(secondsOf(static_cast<int>(kSamplesPerBeat) * 3)));
    }

    SECTION("a stopped block does not advance it") {
        advance(clock, playing(0.0, 1), static_cast<int>(kSamplesPerBeat));
        const auto rolled = clock.monotonicSeconds();

        const auto block = advance(clock, halt(2), 512);
        REQUIRE(block.segments.size() == 1);

        CHECK(block.segments[0].block.monotonicSeconds.start == approx(rolled));
        CHECK(block.segments[0].block.monotonicSeconds.end == approx(rolled));
        CHECK(clock.monotonicSeconds() == approx(rolled));
    }

    SECTION("it does not depend on how the callbacks were cut") {
        TransportClock whole, pieces;
        const auto snapshot = playing(0.0);

        for (auto i = 0; i < 100; ++i)
            advance(whole, snapshot, 512);

        for (auto i = 0; i < 100; ++i) {
            advance(pieces, snapshot, 128);
            advance(pieces, snapshot, 64);
            advance(pieces, snapshot, 256);
            advance(pieces, snapshot, 64);
        }

        CHECK(whole.monotonicSeconds() == approx(pieces.monotonicSeconds()));
    }
}

TEST_CASE("A block is one tempo, which is what a rate-synced modifier reads",
          "[engine][transport][clock][tempo][2336]") {
    // Why the section cut stays now that placement goes through the map
    // (#2333). A modifier synced to the tempo reads one bpm for the whole block
    // it renders (ModLfo's modTimingFor), and so does anything else that takes
    // a rate rather than a position: a block spanning a step would run at the
    // tempo it opened on for the rest of itself.
    TransportClock clock;
    auto snapshot = playing(0.0);
    snapshot.tempo = TempoMap({{0.0, 20.0, 0.0f}, {1.0, 300.0, 0.0f}}, {});

    const auto rendered = advance(clock, snapshot, static_cast<int>(kSampleRate * 2));
    requireCovers(rendered, static_cast<int>(kSampleRate * 2));

    REQUIRE(rendered.segments.size() > 1);

    for (const auto& segment : rendered.segments) {
        const auto& block = segment.block;

        // At the last sample as well as the first. The end beat itself is
        // excluded and can sit a fraction of a sample past the boundary, since
        // a cut lands on the first sample at or after it; what has to be one
        // tempo is every sample the block actually renders.
        const auto last =
            snapshot.tempo.timeToBeat(block.seconds.start + ((block.numSamples - 1) / kSampleRate));

        const auto opening = snapshot.tempo.bpmAt(block.beats.start);
        const auto closing = snapshot.tempo.bpmAt(last);

        CHECK(closing == approx(opening));
    }
}
