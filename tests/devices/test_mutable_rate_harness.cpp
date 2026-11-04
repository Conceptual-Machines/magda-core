// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "magda/daw/audio/plugins/mutable/MutableRateHarness.hpp"

// The rate crossing the three Mutable ports share (#2409), driven without any DSP behind it, so
// what comes out is the crossing alone. A Gaussian pulse is band-limited far inside every
// passband here, so the output can be held against the pulse itself, delayed by what the harness
// declares.

namespace {

using magda::daw::audio::MutableRateHarness;
using Role = MutableRateHarness::Role;

constexpr double kPi = 3.14159265358979323846;

/// Elements' and Clouds' rate and Rings', with their DSP blocks.
struct Port {
    double rate;
    int block;
};
constexpr Port kElements{32000.0, 16};
constexpr Port kRings{48000.0, 24};
constexpr Port kClouds{32000.0, 32};

const double kHostRates[] = {22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0};

double gaussian(double t, double sigma) {
    return std::exp(-0.5 * (t / sigma) * (t / sigma));
}

/// @p hostFrames rendered from an instrument whose DSP produces @p signal(k) at internal frame k.
template <typename Signal>
std::vector<float> renderInstrument(Port port, double hostRate, int hostFrames, int partition,
                                    Signal&& signal) {
    MutableRateHarness harness(port.rate, port.block, Role::Instrument);
    harness.prepare(hostRate);
    int next = 0;
    std::vector<float> out(static_cast<size_t>(hostFrames));
    for (int pos = 0; pos < hostFrames; pos += partition) {
        const int n = std::min(partition, hostFrames - pos);
        harness.render(out.data() + pos, nullptr, n, [&](float* l, float* r, int frames) {
            for (int j = 0; j < frames; ++j, ++next)
                l[j] = r[j] = static_cast<float>(signal(next));
        });
    }
    return out;
}

/// @p input through an effect whose DSP passes its blocks through untouched.
std::vector<float> runEffect(Port port, double hostRate, std::vector<float> input,
                             const std::vector<int>& partitions, int* latency = nullptr) {
    MutableRateHarness harness(port.rate, port.block, Role::Effect);
    harness.prepare(hostRate);
    if (latency != nullptr)
        *latency = harness.latencySamples();
    const int total = static_cast<int>(input.size());
    size_t next = 0;
    for (int pos = 0; pos < total;) {
        const int n = std::min(partitions[next++ % partitions.size()], total - pos);
        harness.process(input.data() + pos, nullptr, n, [](float*, float*, int) {});
        pos += n;
    }
    return input;
}

/// Energy at @p hz over @p x from @p start, Goertzel under a Blackman-Harris window: a plain
/// one leaks a strong tone 70 dB down across the band, which is above the floors read here.
double toneEnergy(const std::vector<float>& x, double rate, double hz, int start) {
    const double coeff = 2.0 * std::cos(2.0 * kPi * hz / rate);
    const double span = static_cast<double>(x.size()) - start - 1.0;
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = static_cast<size_t>(start); i < x.size(); ++i) {
        const double a = 2.0 * kPi * (static_cast<double>(i) - start) / span;
        const double w = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2.0 * a) -
                         0.01168 * std::cos(3.0 * a);
        const double s = w * x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

double dbBelow(double energy, double reference) {
    return -10.0 * std::log10(energy / reference + 1.0e-30);
}

}  // namespace

TEST_CASE("A Mutable instrument lands a note where its latency says", "[mutable][resampler]") {
    for (const auto port : {kElements, kRings}) {
        for (double hostRate : kHostRates) {
            MutableRateHarness harness(port.rate, port.block, Role::Instrument);
            harness.prepare(hostRate);
            const int latency = harness.latencySamples();

            // A pulse 300 internal frames in, so it is well clear of the primed silence.
            constexpr double centre = 300.0, sigma = 3.0;
            const auto out = renderInstrument(port, hostRate, static_cast<int>(hostRate / 40), 37,
                                              [](int k) { return gaussian(k - centre, sigma); });

            double worst = 0.0;
            for (size_t j = 0; j < out.size(); ++j) {
                const double internalTime =
                    (static_cast<double>(j) - latency) * port.rate / hostRate;
                worst = std::max(worst, std::abs(out[j] - gaussian(internalTime - centre, sigma)));
            }
            INFO("internal " << port.rate << " host " << hostRate << ": latency " << latency
                             << ", worst error " << worst);
            CHECK(worst < 1.0e-4);
        }
    }
}

TEST_CASE("A Mutable instrument at its own rate passes its DSP through untouched",
          "[mutable][resampler]") {
    for (const auto port : {kElements, kRings}) {
        MutableRateHarness harness(port.rate, port.block, Role::Instrument);
        harness.prepare(port.rate);
        CHECK(harness.latencySamples() == 0);

        const auto signal = [](int k) { return std::sin(0.37 * k) * 0.5; };
        const auto out = renderInstrument(port, port.rate, 1000, 129, signal);
        for (int j = 0; j < 1000; ++j)
            REQUIRE(out[static_cast<size_t>(j)] == static_cast<float>(signal(j)));
    }
}

TEST_CASE("A Mutable effect's output is its input, delayed by its latency",
          "[mutable][resampler]") {
    for (double hostRate : kHostRates) {
        // The pulse has to sit inside the lower rate's passband, whichever side that is.
        const double sigma = 3.0 * hostRate / std::min(hostRate, kClouds.rate);
        constexpr int centre = 2000;
        std::vector<float> input(6000);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float>(gaussian(static_cast<double>(i) - centre, sigma));

        for (const auto& partition :
             std::vector<std::vector<int>>{{1}, {128}, {4096}, {37, 1, 512}}) {
            int latency = 0;
            const auto out = runEffect(kClouds, hostRate, input, partition, &latency);

            double worst = 0.0;
            for (size_t j = 0; j < out.size(); ++j) {
                const double t = static_cast<double>(j) - latency - centre;
                worst = std::max(worst, std::abs(out[j] - gaussian(t, sigma)));
            }
            INFO("host " << hostRate << " partition " << partition.front() << ": latency "
                         << latency << ", worst error " << worst);
            CHECK(worst < 1.0e-4);
        }
    }
}

TEST_CASE("A Mutable effect at its own rate delays by all but one frame of a block",
          "[mutable][resampler]") {
    std::vector<float> input(500);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(std::sin(0.11 * static_cast<double>(i)));

    int latency = 0;
    const auto out = runEffect(kClouds, kClouds.rate, input, {7}, &latency);
    REQUIRE(latency == kClouds.block - 1);
    for (size_t j = 0; j < out.size(); ++j)
        REQUIRE(out[j] == (j < static_cast<size_t>(latency) ? 0.0f : input[j - latency]));
}

TEST_CASE("A Mutable effect renders the same whatever the callback partition",
          "[mutable][resampler]") {
    // A starved pull would emit a zero without advancing and shift everything after it (#2365).
    for (double hostRate : {22050.0, 44100.0, 192000.0}) {
        std::vector<float> input(static_cast<size_t>(hostRate / 10));
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float>(std::sin(2.0 * kPi * 440.0 * i / hostRate));

        const auto reference = runEffect(kClouds, hostRate, input, {128});
        for (const auto& partition :
             std::vector<std::vector<int>>{{1}, {31}, {8192}, {37, 1, 512, 3, 129}}) {
            INFO("host " << hostRate << " partition " << partition.front());
            CHECK(runEffect(kClouds, hostRate, input, partition) == reference);
        }
    }
}

TEST_CASE("A Mutable instrument keeps its images out of the band above its Nyquist",
          "[mutable][resampler]") {
    // Read against the same tone in the passband. The corner is the lower Nyquist, so an image
    // of f lands at rate - f, and every f a DSP at 32 kHz is flat to lands past 18 kHz.
    for (const auto& [port, hostRate] :
         {std::pair{kElements, 48000.0}, std::pair{kElements, 44100.0},
          std::pair{kRings, 96000.0}}) {
        const auto toneAt = [&](double hz) {
            return renderInstrument(
                port, hostRate, static_cast<int>(hostRate / 4), 256,
                [&](int k) { return 0.5 * std::sin(2.0 * kPi * hz * k / port.rate); });
        };
        const int settle = static_cast<int>(hostRate / 100);

        for (double fraction : {0.15, 0.3, 0.4375}) {
            const double hz = fraction * port.rate;
            const auto out = toneAt(hz);
            const double level = toneEnergy(out, hostRate, hz, settle);
            const double reference =
                toneEnergy(toneAt(0.03 * port.rate), hostRate, 0.03 * port.rate, settle);
            const double image = dbBelow(toneEnergy(out, hostRate, port.rate - hz, settle), level);
            const double loss = dbBelow(level, reference);

            INFO("internal " << port.rate << " host " << hostRate << ": " << hz << " Hz loses "
                             << loss << " dB, its image at " << port.rate - hz << " Hz is " << image
                             << " dB down");
            CHECK(std::abs(loss) < 0.1);
            CHECK(image > 85.0);
        }
    }
}

TEST_CASE("A Mutable effect folds nothing into its passband coming down", "[mutable][resampler]") {
    // 48 kHz into Clouds' 32: a tone above 18 kHz folds to 32 kHz - f, inside the band.
    constexpr double hostRate = 48000.0;
    const auto throughAt = [](double hz) {
        std::vector<float> input(static_cast<size_t>(hostRate / 4));
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * hz * i / hostRate));
        return runEffect(kClouds, hostRate, input, {256});
    };
    const int settle = 1000;
    const double reference = toneEnergy(throughAt(1000.0), hostRate, 1000.0, settle);

    for (double hz : {18000.0, 20000.0, 23000.0}) {
        const double fold =
            dbBelow(toneEnergy(throughAt(hz), hostRate, 32000.0 - hz, settle), reference);
        INFO(hz << " Hz folds to " << 32000.0 - hz << " Hz, " << fold << " dB down");
        CHECK(fold > 85.0);
    }
}
