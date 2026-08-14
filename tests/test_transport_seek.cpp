#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    // The other thing scripts were rebuilding: in 7/8 a bar is 3.5 beats, and
    // a script that hard-codes four rewinds to somewhere that is not a bar
    // line. Asking the facade is the whole point of `seekBars` existing.
    MockTransportApi transport;
    transport.positionBeats = 7.0;
    transport.beatsPerBar = 3.5;

    transport.seekBars(-1);
    CHECK(transport.positionBeats == Approx(3.5));

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
