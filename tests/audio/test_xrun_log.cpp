// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/io/XrunLog.hpp"

/// @file Each xrun with when it happened and the callbacks before it (#3386, #3401).

using magda::XrunLog;

namespace {

XrunLog::Callback at(std::int64_t ms, bool started = false) {
    return {.atMs = ms, .renderMs = 1.0f, .lengthMs = 2.67f, .blocks = 1, .started = started};
}

}  // namespace

TEST_CASE("XrunLog sends nothing until the device's count rises", "[audio][xrun][3386]") {
    XrunLog log;
    log.check(5, 1000);
    log.record(at(1000), 1000.0);
    log.check(5, 1003);

    XrunLog::Event event;
    CHECK_FALSE(log.pop(event));
}

TEST_CASE("XrunLog stamps a rise and sends the callbacks before it, oldest first",
          "[audio][xrun][3386][3401]") {
    XrunLog log;
    log.check(0, 1000);
    log.record(at(1000, true), 1000.0);
    log.check(0, 1003);
    log.record(at(1003), 1003.0);
    log.check(2, 1006);

    XrunLog::Event event;
    REQUIRE(log.pop(event));
    CHECK(event.atMs == 1006);
    CHECK(event.count == 2);
    REQUIRE(event.numCallbacks == 2);
    CHECK(event.callbacks[0].atMs == 1000);
    CHECK(event.callbacks[0].sincePreviousMs == 0.0f);
    CHECK(event.callbacks[1].sincePreviousMs == 3.0f);
    CHECK(event.sinceStart == 1);
    CHECK(event.sinceJump == -1);
    CHECK_FALSE(log.pop(event));
}

TEST_CASE("XrunLog keeps the most recent callbacks only", "[audio][xrun][3401]") {
    XrunLog log;
    log.check(0, 0);
    for (int i = 0; i < 20; ++i)
        log.record(at(i * 3), i * 3.0);
    log.check(1, 60);

    XrunLog::Event event;
    REQUIRE(log.pop(event));
    REQUIRE(event.numCallbacks == XrunLog::kHistory);
    CHECK(event.callbacks.front().atMs == (20 - XrunLog::kHistory) * 3);
    CHECK(event.callbacks.back().atMs == 57);
}

TEST_CASE("XrunLog takes a lower count, or a new device's, as a new baseline",
          "[audio][xrun][3386]") {
    XrunLog log;
    log.check(10, 0);
    log.check(3, 3);
    log.check(3, 6);

    log.restart();
    log.check(9, 9);

    XrunLog::Event event;
    CHECK_FALSE(log.pop(event));

    log.check(10, 12);
    REQUIRE(log.pop(event));
    CHECK(event.count == 1);
    CHECK(event.numCallbacks == 0);
}

TEST_CASE("XrunLog counts what a full queue drops", "[audio][xrun][3386]") {
    XrunLog log;
    log.check(0, 0);
    for (int count = 1; count <= XrunLog::kCapacity + 3; ++count)
        log.check(count, count);
    CHECK(log.dropped() == 3);
}
