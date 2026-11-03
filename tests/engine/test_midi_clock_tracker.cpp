// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/audio/transport/MidiClockTracker.hpp"

using magda::MidiClockTracker;
using Kind = MidiClockTracker::Event::Kind;

namespace {

/// 120 bpm, 24 clocks a beat.
constexpr double kClockSeconds = 0.5 / 24.0;

/// Some time after the counter started, as a real one would read.
constexpr double kOrigin = 1000.0;

/** @brief A clock at 120 bpm, sent one clock at a time. */
struct Master {
    MidiClockTracker tracker;
    int sent = 0;

    double timeOf(int clock) const {
        return kOrigin + (clock * kClockSeconds);
    }
    void send(int clocks) {
        for (auto i = 0; i < clocks; ++i)
            tracker.clock(timeOf(sent++));
    }
    /// When the next clock will arrive.
    double next() const {
        return timeOf(sent);
    }
};

Catch::Approx beats(double value) {
    return Catch::Approx(value).margin(1e-6);
}

}  // namespace

TEST_CASE("A whole beat of clocks gives the tempo", "[midi_clock][3190]") {
    Master master;
    master.send(MidiClockTracker::kWindow - 1);
    CHECK(!master.tracker.secondsPerClock(master.next()).has_value());

    master.send(1);
    const auto seconds = master.tracker.secondsPerClock(master.next());
    REQUIRE(seconds.has_value());
    CHECK(*seconds == Catch::Approx(kClockSeconds).epsilon(1e-9));
}

TEST_CASE("A clock stamped to the millisecond still reads its tempo closely",
          "[midi_clock][3190]") {
    MidiClockTracker tracker;
    for (auto clock = 0; clock < 96; ++clock)
        tracker.clock(std::floor((kOrigin + (clock * kClockSeconds)) * 1000.0) / 1000.0);

    const auto seconds = tracker.secondsPerClock(kOrigin + (96 * kClockSeconds));
    REQUIRE(seconds.has_value());
    const auto bpm = 60.0 / (*seconds * MidiClockTracker::kClocksPerBeat);
    CHECK(bpm == Catch::Approx(120.0).margin(0.25));
}

TEST_CASE("Start plays from the top, counted from the next clock", "[midi_clock][3190]") {
    Master master;
    master.send(24);
    CHECK(!master.tracker.beatAt(master.next()).has_value());

    master.tracker.start();
    const auto event = master.tracker.takeEvent();
    REQUIRE(event.has_value());
    CHECK(event->kind == Kind::play);
    CHECK(event->beat == 0.0);
    CHECK(!master.tracker.takeEvent().has_value());

    // Nothing to say until the clock that is beat zero arrives.
    CHECK(!master.tracker.beatAt(master.next()).has_value());

    const auto downbeat = master.next();
    master.send(1);
    CHECK(*master.tracker.beatAt(downbeat) == beats(0.0));
    CHECK(*master.tracker.beatAt(downbeat + (kClockSeconds / 2.0)) == beats(1.0 / 48.0));

    master.send(24);
    CHECK(*master.tracker.beatAt(downbeat + 0.5) == beats(1.0));
}

TEST_CASE("Stop then Continue plays on from where the clock stopped", "[midi_clock][3190]") {
    Master master;
    master.send(24);
    master.tracker.start();
    master.send(48);
    master.tracker.stop();
    CHECK(!master.tracker.beatAt(master.next()).has_value());

    master.tracker.resume();
    master.tracker.takeEvent();
    const auto stopped = master.tracker.takeEvent();
    REQUIRE(stopped.has_value());
    CHECK(stopped->kind == Kind::stop);

    const auto resumed = master.tracker.takeEvent();
    REQUIRE(resumed.has_value());
    CHECK(resumed->kind == Kind::play);
    CHECK(resumed->beat == beats(2.0));

    const auto at = master.next();
    master.send(1);
    CHECK(*master.tracker.beatAt(at) == beats(2.0));
}

TEST_CASE("Song Position locates a stopped clock, and Continue plays from it",
          "[midi_clock][3190]") {
    Master master;
    master.send(24);
    master.tracker.songPosition(36);

    const auto located = master.tracker.takeEvent();
    REQUIRE(located.has_value());
    CHECK(located->kind == Kind::locate);
    CHECK(located->beat == beats(9.0));

    master.tracker.resume();
    const auto resumed = master.tracker.takeEvent();
    REQUIRE(resumed.has_value());
    CHECK(resumed->beat == beats(9.0));

    const auto at = master.next();
    master.send(1);
    CHECK(*master.tracker.beatAt(at) == beats(9.0));
}

TEST_CASE("Song Position while running moves the count without locating", "[midi_clock][3190]") {
    Master master;
    master.send(24);
    master.tracker.start();
    master.send(30);
    master.tracker.takeEvent();

    master.tracker.songPosition(16);
    CHECK(!master.tracker.takeEvent().has_value());

    const auto at = master.next();
    master.send(1);
    CHECK(*master.tracker.beatAt(at) == beats(4.0));
}

TEST_CASE("A clock that goes quiet is lost, and measured again when it returns",
          "[midi_clock][3190]") {
    Master master;
    master.send(24);
    master.tracker.start();
    master.send(24);

    const auto quiet = master.next() + MidiClockTracker::kLostSeconds;
    CHECK(!master.tracker.beatAt(quiet).has_value());
    CHECK(!master.tracker.secondsPerClock(quiet).has_value());

    // Back at half the tempo: the old clocks are not fitted beside the new ones.
    for (auto clock = 0; clock < MidiClockTracker::kWindow; ++clock)
        master.tracker.clock(quiet + (clock * kClockSeconds * 2.0));
    const auto seconds = master.tracker.secondsPerClock(quiet + 1.0);
    REQUIRE(seconds.has_value());
    CHECK(*seconds == Catch::Approx(kClockSeconds * 2.0).epsilon(1e-9));
}
