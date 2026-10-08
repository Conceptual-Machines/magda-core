#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "exec/BandSplitter.hpp"

// The multiband rack's crossover: bands sum back flat, and each band keeps its own range.

using magda::Crossover;
using magda::CrossoverSlope;
using magda::engine::BandSplitter;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Split {
    std::vector<std::vector<float>> bands;  // per band, mono
    std::vector<float> input;
};

// RMS of each band and of their sum over the last half of a long sine, past the transient.
Split run(BandSplitter& splitter, double hz, int bandCount, int blocks = 64) {
    Split split;
    split.bands.assign(static_cast<std::size_t>(bandCount), {});
    std::vector<float> in(kBlock);
    std::vector<std::vector<float>> out(static_cast<std::size_t>(bandCount),
                                        std::vector<float>(kBlock));
    long n = 0;
    for (int block = 0; block < blocks; ++block) {
        for (auto& sample : in)
            sample = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * n++ / kRate));
        const float* inputs[] = {in.data()};
        std::vector<float*> outputs;
        for (auto& band : out)
            outputs.push_back(band.data());
        splitter.process(inputs, outputs, kBlock);
        if (block >= blocks / 2) {
            split.input.insert(split.input.end(), in.begin(), in.end());
            for (std::size_t b = 0; b < out.size(); ++b)
                split.bands[b].insert(split.bands[b].end(), out[b].begin(), out[b].end());
        }
    }
    return split;
}

double rms(const std::vector<float>& signal) {
    double sum = 0.0;
    for (float x : signal)
        sum += static_cast<double>(x) * x;
    return std::sqrt(sum / static_cast<double>(signal.size()));
}

double sumRms(const Split& split) {
    std::vector<float> sum(split.input.size(), 0.0f);
    for (const auto& band : split.bands)
        for (std::size_t i = 0; i < sum.size(); ++i)
            sum[i] += band[i];
    return rms(sum);
}

}  // namespace

TEST_CASE("BandSplitter bands sum back to the input's level at every slope", "[band_splitter]") {
    for (auto slope : {CrossoverSlope::Db12, CrossoverSlope::Db24, CrossoverSlope::Db48}) {
        const std::vector<Crossover> crossovers{{180.0f, slope}, {3200.0f, slope}};
        for (double hz : {40.0, 180.0, 700.0, 3200.0, 9000.0}) {
            BandSplitter splitter;
            splitter.prepare(kRate, kBlock);
            splitter.setCrossovers(crossovers);
            const auto split = run(splitter, hz, 3);
            INFO("slope " << magda::slopeDbPerOctave(slope) << " at " << hz << " Hz");
            CHECK(sumRms(split) == Catch::Approx(rms(split.input)).epsilon(0.01));
        }
    }
}

TEST_CASE("BandSplitter puts a tone in the band that holds it", "[band_splitter]") {
    BandSplitter splitter;
    splitter.prepare(kRate, kBlock);
    splitter.setCrossovers(std::vector<Crossover>{{180.0f}, {3200.0f}});

    const auto low = run(splitter, 40.0, 3);
    CHECK(rms(low.bands[0]) > 0.95 * rms(low.input));
    CHECK(rms(low.bands[2]) < 0.001 * rms(low.input));

    splitter.reset();
    const auto high = run(splitter, 12000.0, 3);
    CHECK(rms(high.bands[2]) > 0.95 * rms(high.input));
    CHECK(rms(high.bands[0]) < 0.001 * rms(high.input));
}

TEST_CASE("BandSplitter at a crossover splits the tone 6 dB down each way", "[band_splitter]") {
    BandSplitter splitter;
    splitter.prepare(kRate, kBlock);
    splitter.setCrossovers(std::vector<Crossover>{{1000.0f}});
    const auto split = run(splitter, 1000.0, 2);
    // Linkwitz-Riley is -6 dB per side at the crossover, in phase.
    CHECK(rms(split.bands[0]) == Catch::Approx(0.5 * rms(split.input)).epsilon(0.02));
    CHECK(rms(split.bands[1]) == Catch::Approx(0.5 * rms(split.input)).epsilon(0.02));
}

TEST_CASE("BandSplitter unconfigured passes the input through the lowest band", "[band_splitter]") {
    BandSplitter splitter;
    splitter.prepare(kRate, kBlock);
    const auto split = run(splitter, 440.0, 3, 4);
    CHECK(rms(split.bands[0]) == Catch::Approx(rms(split.input)));
    CHECK(rms(split.bands[1]) == 0.0);
}
