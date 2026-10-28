// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

#include "devices/faust/effects/Eq.hpp"

namespace {

using Eq = magda::devices::faust::Eq;
constexpr float kRate = 48000.0f;

/** @brief A band's cascade at @p hz, in dB. */
double responseDb(const Eq::Stages& stages, double hz) {
    const double w = 2.0 * std::numbers::pi * hz / kRate;
    double db = 0.0;
    for (int s = 0; s < stages.count; ++s) {
        const auto& c = stages.coeffs[static_cast<size_t>(s)];
        const double numR = c.b0 + c.b1 * std::cos(w) + c.b2 * std::cos(2 * w);
        const double numI = -(c.b1 * std::sin(w) + c.b2 * std::sin(2 * w));
        const double denR = 1.0 + c.a1 * std::cos(w) + c.a2 * std::cos(2 * w);
        const double denI = -(c.a1 * std::sin(w) + c.a2 * std::sin(2 * w));
        db += 10.0 * std::log10((numR * numR + numI * numI) / (denR * denR + denI * denI));
    }
    return db;
}

}  // namespace

TEST_CASE("EQ cuts fall at 12 and 24 dB per octave", "[eq][dsp]") {
    using T = Eq::BandType;
    const float q = 0.70711f;
    const auto hp12 = Eq::designBand(T::Highpass, 1000.0f, 0.0f, q, kRate);
    const auto hp24 = Eq::designBand(T::Highpass24, 1000.0f, 0.0f, q, kRate);
    const auto lp24 = Eq::designBand(T::Lowpass24, 1000.0f, 0.0f, q, kRate);
    REQUIRE(hp12.count == 1);
    REQUIRE(hp24.count == 2);

    // Two octaves past the corner.
    CHECK(responseDb(hp12, 250.0) == Catch::Approx(-24.0).margin(1.0));
    CHECK(responseDb(hp24, 250.0) == Catch::Approx(-48.0).margin(1.5));
    CHECK(responseDb(lp24, 4000.0) == Catch::Approx(-48.0).margin(1.5));

    // Q 0.707 is a 4-pole Butterworth: 3 dB down at the corner, flat in the passband.
    CHECK(responseDb(hp24, 1000.0) == Catch::Approx(-3.01).margin(0.2));
    CHECK(responseDb(hp24, 8000.0) == Catch::Approx(0.0).margin(0.1));
}
