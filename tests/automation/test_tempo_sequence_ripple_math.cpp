#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../../magda/daw/engine/TempoSequenceRippleMath.hpp"

using magda::temporipple::Mode;
using magda::temporipple::rippleEvents;
using magda::temporipple::sameBeats;

namespace {
// Minimal event: the ripple math only needs a `beat`; `tag` rides along to
// prove payloads follow their beat (and that copies keep the source payload).
struct Ev {
    double beat;
    int tag = 0;
};

std::vector<double> beats(const std::vector<Ev>& v) {
    std::vector<double> b;
    for (const auto& e : v)
        b.push_back(e.beat);
    return b;
}

// The beat-0 anchor plus caller-supplied events, in the layout the sequences
// hand us (index 0 is always the anchor).
std::vector<Ev> seq(std::vector<Ev> events) {
    std::vector<Ev> v{{0.0, -1}};
    for (auto& e : events)
        v.push_back(e);
    return v;
}
}  // namespace

TEST_CASE("ripple Insert shifts events at/after the point, keeps earlier ones", "[tempo_ripple]") {
    auto in = seq({{4.0, 1}, {16.0, 2}});
    auto out = rippleEvents(in, Mode::Insert, 8.0, 12.0);  // +4 beats at beat 8
    REQUIRE(beats(out) == std::vector<double>{0.0, 4.0, 20.0});
    // The anchor never moves; the @4 event is before the point; @16 -> 20.
    REQUIRE(out[2].tag == 2);
}

TEST_CASE("ripple Insert never moves the beat-0 anchor even at point 0", "[tempo_ripple]") {
    auto in = seq({{8.0, 1}});
    auto out = rippleEvents(in, Mode::Insert, 0.0, 4.0);
    REQUIRE(beats(out) == std::vector<double>{0.0, 12.0});  // anchor stays, @8 -> 12
}

TEST_CASE("ripple Insert on an event exactly at the point moves it", "[tempo_ripple]") {
    auto in = seq({{8.0, 1}});
    auto out = rippleEvents(in, Mode::Insert, 8.0, 12.0);
    REQUIRE(beats(out) == std::vector<double>{0.0, 12.0});  // boundary event shifts with content
}

TEST_CASE("ripple Delete drops in-range events and pulls later ones left", "[tempo_ripple]") {
    auto in = seq({{10.0, 1}, {20.0, 2}});
    auto out = rippleEvents(in, Mode::Delete, 8.0, 12.0);   // -4 beats
    REQUIRE(beats(out) == std::vector<double>{0.0, 16.0});  // @10 dropped, @20 -> 16
    REQUIRE(out[1].tag == 2);
}

TEST_CASE("ripple Delete keeps the anchor and events before the range", "[tempo_ripple]") {
    auto in = seq({{2.0, 1}, {10.0, 2}, {20.0, 3}});
    auto out = rippleEvents(in, Mode::Delete, 8.0, 12.0);
    REQUIRE(beats(out) == std::vector<double>{0.0, 2.0, 16.0});  // @2 stays, @10 dropped, @20->16
}

TEST_CASE("ripple Delete of an event exactly at range end shifts it to the range start",
          "[tempo_ripple]") {
    auto in = seq({{12.0, 1}});
    auto out = rippleEvents(in, Mode::Delete, 8.0, 12.0);  // [8,12), @12 is at the end (not inside)
    REQUIRE(beats(out) == std::vector<double>{0.0, 8.0});
}

TEST_CASE("ripple Duplicate opens a gap and copies in-range events into it", "[tempo_ripple]") {
    auto in = seq({{12.0, 1}, {20.0, 2}});
    auto out = rippleEvents(in, Mode::Duplicate, 8.0, 16.0);  // dur 8
    // @12 stays, @20 -> 28, copy of @12 lands at 20.
    REQUIRE(beats(out) == std::vector<double>{0.0, 12.0, 20.0, 28.0});
    REQUIRE(out[2].tag == 1);  // the copy carries the source payload
    REQUIRE(out[3].tag == 2);
}

TEST_CASE("ripple Duplicate never copies the anchor", "[tempo_ripple]") {
    auto in = seq({});  // only the anchor
    auto out = rippleEvents(in, Mode::Duplicate, 0.0, 8.0);
    REQUIRE(beats(out) == std::vector<double>{0.0});  // nothing to shift or copy
}

TEST_CASE("ripple leaves a sequence with only the anchor untouched", "[tempo_ripple]") {
    auto in = seq({});
    REQUIRE(sameBeats(rippleEvents(in, Mode::Insert, 8.0, 12.0), in));
    REQUIRE(sameBeats(rippleEvents(in, Mode::Delete, 8.0, 12.0), in));
    REQUIRE(sameBeats(rippleEvents(in, Mode::Duplicate, 8.0, 16.0), in));
}

TEST_CASE("sameBeats detects membership and position changes", "[tempo_ripple]") {
    auto a = seq({{8.0, 1}});
    REQUIRE(sameBeats(a, a));
    REQUIRE_FALSE(sameBeats(a, seq({{12.0, 1}})));  // moved
    REQUIRE_FALSE(sameBeats(a, seq({})));           // dropped
}
