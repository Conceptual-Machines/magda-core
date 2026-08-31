#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "../magda/daw/audio/analysis/AudioTapBuffer.hpp"
#include "../magda/daw/audio/analysis/BandSpectrum.hpp"

using magda::daw::audio::AudioTapBuffer;
using magda::daw::audio::computeMaskingBandsDb;
using magda::daw::audio::kNumMaskingBands;
using magda::daw::audio::maskingBandEdgeHz;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Index of the band that contains a given frequency.
int bandOf(double hz) {
    for (int b = 0; b < kNumMaskingBands; ++b)
        if (hz >= maskingBandEdgeHz(b) && hz < maskingBandEdgeHz(b + 1))
            return b;
    return -1;
}

std::array<float, kNumMaskingBands> bandsForSine(double freq, float amp) {
    AudioTapBuffer ring(4096);
    std::vector<float> buf(4096);
    for (int i = 0; i < 4096; ++i)
        buf[static_cast<size_t>(i)] =
            amp * static_cast<float>(std::sin(2.0 * kPi * freq * i / kSr));
    ring.write(buf.data(), 4096);
    std::array<float, kNumMaskingBands> out{};
    computeMaskingBandsDb(ring, kSr, out);
    return out;
}

}  // namespace

TEST_CASE("BandSpectrum - a tone lands in its own band", "[bandspectrum]") {
    const double freq = 1000.0;
    const int expected = bandOf(freq);
    REQUIRE(expected >= 0);

    const auto bands = bandsForSine(freq, 1.0f);
    int argmax = 0;
    for (int b = 1; b < kNumMaskingBands; ++b)
        if (bands[static_cast<size_t>(b)] > bands[static_cast<size_t>(argmax)])
            argmax = b;
    REQUIRE(argmax == expected);
}

TEST_CASE("BandSpectrum - full-scale tone reads near 0 dB", "[bandspectrum]") {
    const auto bands = bandsForSine(1000.0, 1.0f);
    const int b = bandOf(1000.0);
    REQUIRE(bands[static_cast<size_t>(b)] > -6.0f);
    REQUIRE(bands[static_cast<size_t>(b)] < 3.0f);
}

TEST_CASE("BandSpectrum - halving amplitude drops the band ~6 dB", "[bandspectrum]") {
    const int b = bandOf(1000.0);
    const float full = bandsForSine(1000.0, 1.0f)[static_cast<size_t>(b)];
    const float half = bandsForSine(1000.0, 0.5f)[static_cast<size_t>(b)];
    REQUIRE((full - half) == Catch::Approx(6.0f).margin(1.0f));
}

TEST_CASE("BandSpectrum - silence reads at the floor", "[bandspectrum]") {
    const auto bands = bandsForSine(1000.0, 0.0f);
    for (int b = 0; b < kNumMaskingBands; ++b)
        REQUIRE(bands[static_cast<size_t>(b)] < -100.0f);
}
