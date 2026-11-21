// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/panels/CpuDeadlineMark.hpp"

using magda::daw::ui::transport::CpuDeadlineMark;

TEST_CASE("CpuDeadlineMark rises at once to a slower block", "[ui][cpu][3375]") {
    CpuDeadlineMark mark;
    mark.update(0.3f);
    mark.update(1.12f);
    CHECK(mark.value() == 1.12f);
}

TEST_CASE("CpuDeadlineMark holds an overrun, then falls to the current block", "[ui][cpu][3375]") {
    CpuDeadlineMark mark;
    mark.update(1.12f);
    for (int reading = 1; reading < CpuDeadlineMark::kHoldReadings; ++reading) {
        mark.update(0.3f);
        CHECK(mark.value() == 1.12f);
    }
    mark.update(0.3f);
    CHECK(mark.value() == 0.3f);
}

TEST_CASE("CpuDeadlineMark restarts its hold on each new high", "[ui][cpu][3375]") {
    CpuDeadlineMark mark;
    mark.update(0.9f);
    for (int reading = 1; reading < CpuDeadlineMark::kHoldReadings; ++reading)
        mark.update(0.2f);
    mark.update(1.05f);
    for (int reading = 1; reading < CpuDeadlineMark::kHoldReadings; ++reading) {
        mark.update(0.2f);
        CHECK(mark.value() == 1.05f);
    }
}
