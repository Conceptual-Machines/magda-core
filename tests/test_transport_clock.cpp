#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "transport/TransportClock.hpp"

using magda::engine::TempoMap;
using magda::engine::TransportClock;
using magda::engine::TransportSnapshot;

namespace {

constexpr double kSampleRate = 44100.0;

/// 120 bpm: a beat is half a second, 22050 samples, and a bar is four of them.
constexpr double kSamplesPerBeat = kSampleRate / 2.0;

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
    CHECK(first.segments[0].block.startBeat == approx(8.0));
    CHECK(first.segments[0].block.endBeat == approx(8.0));
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
    CHECK(block.segments[0].block.startBeat == approx(0.0));
    CHECK(block.segments[0].block.endBeat == approx(1.0));
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
    CHECK(crossing.segments[0].block.endBeat == approx(1.0));
    CHECK(crossing.segments[0].block.continuous);

    CHECK(crossing.segments[1].startSample == 100);
    CHECK(crossing.segments[1].block.numSamples == 412);
    CHECK(crossing.segments[1].block.startBeat == approx(0.0));
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
        CHECK(entering.segments[0].block.startBeat == approx(0.0));
        CHECK(entering.segments[0].block.endBeat == approx(2.5));

        // Carrying on from there wraps at the end of the loop, not at its
        // start.
        const auto wrapping = advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 2));
        REQUIRE(wrapping.segments.size() == 2);
        CHECK(wrapping.segments[0].block.endBeat == approx(4.0));
        CHECK(wrapping.segments[1].block.startBeat == approx(2.0));
    }

    SECTION("a cursor put down past the loop plays on") {
        // The loop catches what reaches its end. It does not collect a
        // playhead that was put down somewhere else entirely, because that
        // would be ignoring where the user pointed.
        auto located = playing(9.0, 2);
        located.loop = snapshot.loop;

        const auto block = advance(clock, located, 512);
        CHECK(block.segments[0].block.startBeat == approx(9.0));
        CHECK(block.segments[0].block.endBeat > 9.0);
    }

    SECTION("a loop dragged behind the cursor leaves it playing") {
        advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 3));
        REQUIRE(clock.positionBeats() == approx(3.0));

        // Same generation: nothing was requested, the loop was dragged.
        auto moved = snapshot;
        moved.loop = {true, 0.0, 1.0};

        const auto block = advance(clock, moved, 512);
        CHECK(block.segments[0].block.startBeat == approx(3.0));
        CHECK(block.segments[0].block.continuous);
    }

    SECTION("a loop dragged around the cursor catches it at the new end") {
        advance(clock, snapshot, static_cast<int>(kSamplesPerBeat * 3));

        auto moved = snapshot;
        moved.loop = {true, 0.0, 3.5};

        const auto block = advance(clock, moved, static_cast<int>(kSamplesPerBeat));
        REQUIRE(block.segments.size() == 2);
        CHECK(block.segments[0].block.endBeat == approx(3.5));
        CHECK(block.segments[1].block.startBeat == approx(0.0));
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
        CHECK(opening.segments[0].block.startBeat == approx(2.0));
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
        CHECK(crossing.segments[1].block.startBeat == approx(4.0));

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
    CHECK(block.segments[0].block.startBeat == approx(2.0));
    CHECK(block.segments[0].block.endBeat == approx(3.0));

    // Not a jump. Nothing about the timeline broke: a beat is where it always
    // was, and only its arrival time moved.
    CHECK(block.segments[0].block.continuous);
}

TEST_CASE("A request is applied once", "[engine][transport][clock]") {
    TransportClock clock;
    const auto snapshot = playing(2.0);

    advance(clock, snapshot, 512);
    const auto second = advance(clock, snapshot, 512);

    // Republishing the same request does not drag the cursor back to where
    // playback started, which is what would happen if the position were read
    // every block rather than the generation.
    CHECK(second.segments[0].block.startBeat > 2.0);
    CHECK(second.segments[0].block.continuous);

    SECTION("a new one relocates and says so") {
        const auto located = advance(clock, playing(16.0, 2), 512);
        CHECK(located.segments[0].block.startBeat == approx(16.0));
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
    CHECK(halted.segments[0].block.startBeat == approx(at));
    CHECK(clock.positionBeats() == approx(at));
    CHECK(halted.segments[0].block.continuous);

    SECTION("and playing again from there does not have to either") {
        auto resumed = halt(3);
        resumed.request.playing = true;

        const auto block = advance(clock, resumed, 512);
        CHECK(block.segments[0].block.startBeat == approx(at));

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

    SECTION("and it costs one callback, not every callback after it") {
        // Rendering the remainder straight through leaves the cursor past the
        // loop end, which is also where a cursor located beyond the loop sits.
        // Without putting it back, the next callback would read it as one that
        // was put there on purpose and never wrap again.
        const auto next = advance(clock, snapshot, 512);

        requireCovers(next, 512);
        CHECK(next.segments.size() > 1);
        CHECK(next.segments[0].block.startBeat == approx(0.0));
    }
}
