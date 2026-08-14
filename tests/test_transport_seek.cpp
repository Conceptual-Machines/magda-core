#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "MockMagdaApi.hpp"

/**
 * @file test_transport_seek.cpp
 * @brief Relative transport seeking (#1987).
 *
 * `seekBeats` and `seekBars` are concrete on `TransportApi` rather than
 * virtual, so this exercises the implementation every surface shares: the Lua
 * bindings, the remote API's `transport.seekRelative`, and the OSC namespace's
 * `/magda/transport/seek` all reach the code under test here.
 *
 * What an implementation supplies is `beatsAtBarOffset` — how long a bar is,
 * which is the project's business. The mock answers with a fixed meter so the
 * arithmetic is checkable; the live one walks the tempo sequence.
 */

using Catch::Approx;
using magda::test::MockTransportApi;

TEST_CASE("Seeking by beats moves relative to where the playhead is", "[transport][seek][api]") {
    MockTransportApi transport;
    transport.positionBeats = 8.0;

    transport.seekBeats(4.0);
    CHECK(transport.positionBeats == Approx(12.0));

    transport.seekBeats(-1.5);
    CHECK(transport.positionBeats == Approx(10.5));

    // Zero is a legal ask and a no-op, not a special case for callers to guard.
    transport.seekBeats(0.0);
    CHECK(transport.positionBeats == Approx(10.5));
}

TEST_CASE("Seeking never lands before the start of the timeline", "[transport][seek][api]") {
    // The clamp every script was writing by hand. A rewind button held down at
    // the top of a project has to stop at zero rather than run negative, and
    // that is not a decision each of three surfaces should be making.
    MockTransportApi transport;
    transport.positionBeats = 2.0;

    transport.seekBeats(-10.0);
    CHECK(transport.positionBeats == Approx(0.0));

    transport.seekBeats(-10.0);
    CHECK(transport.positionBeats == Approx(0.0));
}

TEST_CASE("Seeking by bars asks the project how long a bar is", "[transport][seek][api]") {
    MockTransportApi transport;
    transport.positionBeats = 0.0;
    transport.beatsPerBar = 4.0;

    transport.seekBars(2);
    CHECK(transport.positionBeats == Approx(8.0));

    transport.seekBars(-1);
    CHECK(transport.positionBeats == Approx(4.0));
}

TEST_CASE("Seeking by bars follows the meter rather than assuming four", "[transport][seek][api]") {
    // The other thing scripts were rebuilding. A script that hard-codes four
    // rewinds to somewhere that is not a bar line the moment the project is
    // not in four, so the length comes from the project. What a bar is worth
    // in a given meter is the project's answer rather than this test's - here
    // it is simply not four.
    MockTransportApi transport;
    transport.positionBeats = 14.0;
    transport.beatsPerBar = 7.0;

    transport.seekBars(-1);
    CHECK(transport.positionBeats == Approx(7.0));

    transport.seekBars(-1);
    CHECK(transport.positionBeats == Approx(0.0));
}

TEST_CASE("Seeking back by bars clamps at zero too", "[transport][seek][api]") {
    MockTransportApi transport;
    transport.positionBeats = 4.0;
    transport.beatsPerBar = 4.0;

    transport.seekBars(-8);
    CHECK(transport.positionBeats == Approx(0.0));
}

TEST_CASE("A zero bar offset leaves the playhead alone", "[transport][seek][api]") {
    // Mid-bar on purpose: a no-op has to be a no-op rather than a quiet snap
    // to the nearest bar line, which is a different feature and not this one.
    MockTransportApi transport;
    transport.positionBeats = 5.25;
    transport.beatsPerBar = 4.0;

    transport.seekBars(0);
    CHECK(transport.positionBeats == Approx(5.25));
}

TEST_CASE("An out-of-range bar offset is bounded before it is narrowed", "[transport][seek][api]") {
    // Every surface reads this number from outside MAGDA — a Lua integer, a
    // JSON number, an OSC float — so the count arrives 64-bit and is bounded
    // here rather than narrowed at three different edges. Past the bound the
    // answer is one end of the project, which is what a clamp gives.
    MockTransportApi transport;
    transport.positionBeats = 16.0;
    transport.beatsPerBar = 4.0;

    transport.seekBars(std::numeric_limits<long long>::max());
    CHECK(transport.lastBarOffset == magda::TransportApi::kMaxBarOffset);
    CHECK(transport.positionBeats == Approx(16.0 + (4.0 * magda::TransportApi::kMaxBarOffset)));

    transport.positionBeats = 16.0;
    transport.seekBars(std::numeric_limits<long long>::min());
    CHECK(transport.lastBarOffset == -magda::TransportApi::kMaxBarOffset);

    // Bounded on the way in, and still clamped at zero on the way out.
    CHECK(transport.positionBeats == Approx(0.0));
}
