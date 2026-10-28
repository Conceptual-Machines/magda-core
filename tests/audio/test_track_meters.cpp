// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/TrackMeters.hpp"

using namespace magda;

TEST_CASE("A meter read drains the ring to its loudest reading", "[meters]") {
    // One reading popped per frame let a reader that slipped a frame stay that
    // far behind for good, and the ring holds eight.
    MeteringBuffer ring;
    ring.pushLevels(3, {.peakL = 0.2f, .peakR = 0.9f});
    ring.pushLevels(3, {.peakL = 0.7f, .peakR = 0.1f});
    ring.pushLevels(3, {.peakL = 0.4f, .peakR = 0.3f});

    MeterData peak;
    REQUIRE(drainPeak(ring, 3, peak));
    CHECK(peak.peakL == 0.7f);
    CHECK(peak.peakR == 0.9f);

    // Nothing is left for the next frame to show late.
    CHECK_FALSE(drainPeak(ring, 3, peak));
}
