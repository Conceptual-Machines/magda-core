// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "../../magda/daw/engine/host/RenderLoadShares.hpp"
#include "exec/RenderLoad.hpp"

// Each track's and device's share of the audio period, as the CPU view lists them (#3357).

using namespace magda;
using magda::engine::OpKind;
using magda::engine::PlanOp;
using magda::engine::RenderLoad;

namespace {

constexpr double kRate = 48000.0;

PlanOp opOf(OpKind kind, TrackId track, DeviceId device = INVALID_DEVICE_ID) {
    PlanOp op;
    op.kind = kind;
    op.key.trackId = track;
    op.key.deviceId = device;
    return op;
}

}  // namespace

TEST_CASE("RenderLoadShares turns two reads into each track's share", "[engine][cpu][3357]") {
    using std::chrono::microseconds;
    RenderLoad load;
    RenderLoadShares shares;

    // 10 ms of audio: a device taking 2 ms at the callback, the track's fader 1 ms, and a
    // second track 3 ms on the ahead thread.
    load.addBudget(480, kRate);
    load.addOp(opOf(OpKind::Device, 1, 7), microseconds(2000));
    load.addOp(opOf(OpKind::Fader, 1, 7), microseconds(1000));
    {
        const RenderLoad::AheadScope ahead;
        load.addOp(opOf(OpKind::Device, 2, 8), microseconds(3000));
    }
    REQUIRE(shares.update(load));

    CHECK(shares.device(1, ChainSegment::Fx, 7).callback == Catch::Approx(0.2));
    CHECK(shares.track(1).callback == Catch::Approx(0.3));
    CHECK(shares.track(1).ahead == 0.0);
    CHECK(shares.track(2).ahead == Catch::Approx(0.3));
    CHECK(shares.track(2).callback == 0.0);
    CHECK(shares.all().callback == Catch::Approx(0.3));
    CHECK(shares.all().ahead == Catch::Approx(0.3));

    // The next read counts only what came after the first.
    load.addBudget(480, kRate);
    load.addOp(opOf(OpKind::Device, 1, 7), microseconds(500));
    REQUIRE(shares.update(load));
    CHECK(shares.track(1).callback == Catch::Approx(0.05));
    CHECK(shares.track(2).total() == 0.0);

    // No audio time, no new shares.
    CHECK_FALSE(shares.update(load));
    CHECK(shares.track(1).callback == Catch::Approx(0.05));
}

TEST_CASE("An ahead scope inside another leaves the thread rendering ahead",
          "[engine][cpu][3361]") {
    using std::chrono::microseconds;
    RenderLoad load;
    RenderLoadShares shares;
    load.addBudget(480, kRate);
    {
        const RenderLoad::AheadScope thread;
        { const RenderLoad::AheadScope op; }
        load.addOp(opOf(OpKind::Device, 2, 8), microseconds(1000));
    }
    load.addOp(opOf(OpKind::Device, 1, 7), microseconds(1000));
    REQUIRE(shares.update(load));
    CHECK(shares.track(2).ahead == Catch::Approx(0.1));
    CHECK(shares.track(1).callback == Catch::Approx(0.1));
}

TEST_CASE("RenderLoad files the master and devices on other chains apart", "[engine][cpu][3357]") {
    RenderLoad load;
    auto post = opOf(OpKind::Device, MASTER_TRACK_ID, 3);
    post.key.segment = ChainSegment::PostFx;
    load.addOp(post, std::chrono::microseconds(10));
    load.addOp(opOf(OpKind::Device, MASTER_TRACK_ID, 3), std::chrono::microseconds(20));

    int seen = 0;
    load.forEach([&](const RenderLoad::Entry& entry) {
        ++seen;
        CHECK(entry.track == MASTER_TRACK_ID);
        CHECK(entry.device == 3);
        CHECK(entry.nanos[0] == (entry.segment == ChainSegment::PostFx ? 10000U : 20000U));
    });
    CHECK(seen == 2);
}
