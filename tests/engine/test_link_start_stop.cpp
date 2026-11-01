// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/transport/LinkStartStop.hpp"

using magda::LinkStartStop;

namespace {

LinkStartStop active() {
    LinkStartStop sync;
    sync.setActive(true);
    return sync;
}

}  // namespace

TEST_CASE("With Start Stop Sync off, nothing crosses", "[link][transport]") {
    LinkStartStop sync;
    CHECK_FALSE(sync.transportChanged(true));
    CHECK_FALSE(sync.sessionChanged(false));
    CHECK_FALSE(sync.sessionChanged(true));
}

TEST_CASE("The transport's play and stop reach the session", "[link][transport]") {
    auto sync = active();
    CHECK(sync.transportChanged(true));
    CHECK_FALSE(sync.transportChanged(true));
    CHECK(sync.transportChanged(false));
}

TEST_CASE("A peer's play and stop reach the transport and are not sent back", "[link][transport]") {
    auto sync = active();

    REQUIRE(sync.sessionChanged(true));
    CHECK_FALSE(sync.transportChanged(true));

    REQUIRE(sync.sessionChanged(false));
    CHECK_FALSE(sync.transportChanged(false));
}

TEST_CASE("Link's echo of a change we sent is not followed", "[link][transport]") {
    auto sync = active();
    REQUIRE(sync.transportChanged(true));
    CHECK_FALSE(sync.sessionChanged(true));
}

TEST_CASE("Joining a playing session leaves the transport until Play, then Stop stops everyone",
          "[link][transport]") {
    auto sync = active();
    CHECK(sync.transportChanged(true));
    CHECK(sync.transportChanged(false));
}

TEST_CASE("Switching sync on while playing keeps playing, and Stop still goes out",
          "[link][transport]") {
    LinkStartStop sync;
    CHECK_FALSE(sync.transportChanged(true));
    sync.setActive(true);

    CHECK_FALSE(sync.sessionChanged(true));
    CHECK(sync.transportPlaying());
    CHECK(sync.transportChanged(false));
}

TEST_CASE("A peer's Stop stops a transport that was playing before sync came on",
          "[link][transport]") {
    LinkStartStop sync;
    sync.transportChanged(true);
    sync.setActive(true);
    CHECK(sync.sessionChanged(false));
}

TEST_CASE("A project load's stop stays home, and Play afterwards goes out", "[link][transport]") {
    auto sync = active();
    REQUIRE(sync.transportChanged(true));

    sync.transportStoppedForLoad();
    CHECK_FALSE(sync.transportPlaying());
    CHECK_FALSE(sync.transportChanged(false));

    CHECK(sync.transportChanged(true));
}

TEST_CASE("A peer's Play after a project load starts the transport", "[link][transport]") {
    auto sync = active();
    sync.sessionChanged(true);
    sync.transportChanged(true);

    sync.transportStoppedForLoad();
    CHECK_FALSE(sync.sessionChanged(false));
    CHECK(sync.sessionChanged(true));
}
