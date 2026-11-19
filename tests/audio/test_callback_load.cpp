// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/io/CallbackLoad.hpp"

/// @file The CPU readout's load, averaged over whole windows of audio time.

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 128;
constexpr double kBlockMs = kBlock * 1000.0 / kRate;

}  // namespace

TEST_CASE("CallbackLoad publishes nothing before a whole window", "[audio][cpu]") {
    magda::CallbackLoad load;
    for (int i = 0; i < 100; ++i)
        load.record(kBlockMs * 0.5, kBlock, kRate);
    CHECK(load.lastWindow() == 0.0);
}

TEST_CASE("CallbackLoad reads a beat's spikes as their mean, at any phase", "[audio][cpu]") {
    // One block in eight at 80% and the rest at 10%: a drum hit and its tail.
    magda::CallbackLoad load;
    const double expected = (0.8 + 7 * 0.1) / 8.0;
    for (int block = 0; block < 4000; ++block) {
        load.record(kBlockMs * (block % 8 == 0 ? 0.8 : 0.1), kBlock, kRate);
        if (block > 400)
            CHECK(load.lastWindow() == Catch::Approx(expected).epsilon(0.02));
    }
}
