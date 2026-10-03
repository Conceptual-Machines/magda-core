#include <bit>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

#include "../../magda/daw/audio/analysis/TrackMeasurer.hpp"

using magda::daw::audio::kSilenceLufs;
using magda::daw::audio::TrackMeasurementSnapshot;
using magda::daw::audio::TrackMeasurer;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 512;
constexpr double kPi = 3.14159265358979323846;

// Feed `seconds` of a stereo sine (same signal both channels unless rGain set)
// through the measurer in kBlock-sized chunks.
void feedSine(TrackMeasurer& m, double freq, float ampL, float ampR, double seconds,
              double startPhase = 0.0) {
    const int total = static_cast<int>(seconds * kSr);
    std::vector<float> l(kBlock), r(kBlock);
    double phase = startPhase;
    const double inc = 2.0 * kPi * freq / kSr;
    int done = 0;
    while (done < total) {
        const int n = std::min(kBlock, total - done);
        for (int i = 0; i < n; ++i) {
            const float s = static_cast<float>(std::sin(phase));
            l[static_cast<size_t>(i)] = ampL * s;
            r[static_cast<size_t>(i)] = ampR * s;
            phase += inc;
        }
        const float* ch[2] = {l.data(), r.data()};
        m.process(ch, 2, n);
        done += n;
    }
}

}  // namespace

TEST_CASE("TrackMeasurer - silence reports floor values", "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, kBlock, false);
    feedSine(m, 1000.0, 0.0f, 0.0f, 1.0);
    const auto s = m.read();
    REQUIRE(s.momentaryLufs == Catch::Approx(kSilenceLufs));
    REQUIRE(s.shortTermLufs == Catch::Approx(kSilenceLufs));
    REQUIRE(s.integratedLufs == Catch::Approx(kSilenceLufs));
}

TEST_CASE("TrackMeasurer - full-scale 1kHz sine lands near 0 LUFS", "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, kBlock, false);
    feedSine(m, 1000.0, 1.0f, 1.0f, 4.0);
    const auto s = m.read();
    // Amplitude-1 stereo sine: stereo-summed mean square ~= 1.0 (0 dB) minus the
    // 0.691 offset plus a small K-weighting gain at 1 kHz -> a few tenths around 0.
    REQUIRE(s.momentaryLufs > -3.0f);
    REQUIRE(s.momentaryLufs < 2.0f);
    REQUIRE(s.shortTermLufs > -3.0f);
    REQUIRE(s.integratedLufs > -3.0f);
    REQUIRE(s.integratedLufs < 2.0f);
}

TEST_CASE("TrackMeasurer - halving amplitude drops loudness ~6 LU", "[measurer]") {
    TrackMeasurer full, half;
    full.prepare(kSr, kBlock, false);
    half.prepare(kSr, kBlock, false);
    feedSine(full, 1000.0, 1.0f, 1.0f, 4.0);
    feedSine(half, 1000.0, 0.5f, 0.5f, 4.0);
    const float df = full.read().integratedLufs - half.read().integratedLufs;
    REQUIRE(df == Catch::Approx(6.02f).margin(0.5f));
}

TEST_CASE("TrackMeasurer - gated integrated tracks steady short-term", "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, kBlock, false);
    feedSine(m, 1000.0, 0.5f, 0.5f, 5.0);
    const auto s = m.read();
    REQUIRE(s.integratedLufs == Catch::Approx(s.shortTermLufs).margin(1.0f));
}

TEST_CASE("TrackMeasurer - sample peak reflects amplitude", "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, kBlock, false);
    feedSine(m, 1000.0, 0.5f, 0.5f, 1.0);
    // 0.5 amplitude -> -6.02 dBFS peak.
    REQUIRE(m.read().samplePeakDb == Catch::Approx(-6.02f).margin(0.2f));
}

TEST_CASE("TrackMeasurer - live blocks larger than prepared scratch are still measured",
          "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, 64, true);
    m.setSpectrumCaptureEnabled(true);

    feedSine(m, 1000.0, 0.5f, 0.5f, 1.0);

    const auto s = m.read();
    REQUIRE(s.valid);
    REQUIRE(s.momentaryLufs > kSilenceLufs);
    REQUIRE(s.shortTermLufs > kSilenceLufs);
    REQUIRE(s.integratedLufs > kSilenceLufs);
    REQUIRE(s.samplePeakDb == Catch::Approx(-6.02f).margin(0.2f));
    REQUIRE(s.truePeakValid);
    REQUIRE(s.truePeakDb >= s.samplePeakDb - 0.05f);
    REQUIRE(m.getSpectrumRing().writePosition() == static_cast<size_t>(kSr));
}

TEST_CASE("TrackMeasurer - correlation: mono=+1, inverted=-1", "[measurer]") {
    TrackMeasurer mono, inv;
    mono.prepare(kSr, kBlock, false);
    inv.prepare(kSr, kBlock, false);
    feedSine(mono, 1000.0, 0.5f, 0.5f, 1.0);
    feedSine(inv, 1000.0, 0.5f, -0.5f, 1.0);
    REQUIRE(mono.read().correlation == Catch::Approx(1.0f).margin(0.05f));
    REQUIRE(inv.read().correlation == Catch::Approx(-1.0f).margin(0.05f));
}

TEST_CASE("TrackMeasurer - width: mono is ~0, decorrelated is larger", "[measurer]") {
    TrackMeasurer mono;
    mono.prepare(kSr, kBlock, false);
    feedSine(mono, 1000.0, 0.5f, 0.5f, 1.0);
    REQUIRE(mono.read().width == Catch::Approx(0.0f).margin(0.02f));

    // Out-of-phase content has side energy -> non-zero width.
    TrackMeasurer wide;
    wide.prepare(kSr, kBlock, false);
    feedSine(wide, 1000.0, 0.5f, -0.5f, 1.0);
    REQUIRE(wide.read().width > 0.5f);
}

TEST_CASE("TrackMeasurer - true peak is enabled-gated and >= sample peak", "[measurer]") {
    TrackMeasurer off;
    off.prepare(kSr, kBlock, false);
    feedSine(off, 1000.0, 0.9f, 0.9f, 1.0);
    REQUIRE_FALSE(off.read().truePeakValid);

    TrackMeasurer on;
    on.prepare(kSr, kBlock, true);
    // A high-frequency near-full-scale tone has inter-sample peaks above the
    // sampled maxima; true peak must not fall below sample peak.
    feedSine(on, 11000.0, 0.95f, 0.95f, 1.0);
    const auto s = on.read();
    REQUIRE(s.truePeakValid);
    REQUIRE(s.truePeakDb >= s.samplePeakDb - 0.05f);
}

TEST_CASE("TrackMeasurer - PLR/PSR are flagged invalid until both terms have signal",
          "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, kBlock, false);

    // Silence: no peak and no loudness, so neither ratio is meaningful.
    feedSine(m, 1000.0, 0.0f, 0.0f, 1.0);
    auto s = m.read();
    REQUIRE_FALSE(s.plrValid);
    REQUIRE_FALSE(s.psrValid);
    REQUIRE(s.plr == Catch::Approx(0.0f));

    feedSine(m, 1000.0, 0.5f, 0.5f, 4.0);
    s = m.read();
    REQUIRE(s.plrValid);
    REQUIRE(s.psrValid);
    REQUIRE(s.plr == Catch::Approx(s.samplePeakDb - s.integratedLufs).margin(0.01f));
    REQUIRE(s.psr == Catch::Approx(s.samplePeakDb - s.shortTermLufs).margin(0.01f));
}

TEST_CASE("TrackMeasurer - reset drops a held peak back to the current signal", "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, kBlock, false);

    feedSine(m, 1000.0, 0.9f, 0.9f, 2.0);
    const float loudPeak = m.read().samplePeakDb;
    REQUIRE(loudPeak == Catch::Approx(-0.92f).margin(0.2f));

    // A quieter signal cannot pull the peak hold (or the integrated loudness)
    // down on its own - that is what makes the reset necessary (issue #1967).
    feedSine(m, 1000.0, 0.1f, 0.1f, 2.0);
    REQUIRE(m.read().samplePeakDb == Catch::Approx(loudPeak).margin(0.01f));
    const float heldIntegrated = m.read().integratedLufs;

    m.reset();
    feedSine(m, 1000.0, 0.1f, 0.1f, 2.0);
    const auto s = m.read();
    REQUIRE(s.samplePeakDb == Catch::Approx(-20.0f).margin(0.2f));
    REQUIRE(s.samplePeakDb < loudPeak - 10.0f);
    REQUIRE(s.integratedLufs < heldIntegrated - 5.0f);
}

TEST_CASE("TrackMeasurer - reset clears state", "[measurer]") {
    TrackMeasurer m;
    m.prepare(kSr, kBlock, false);
    feedSine(m, 1000.0, 1.0f, 1.0f, 2.0);
    REQUIRE(m.read().valid);
    m.reset();
    const auto s = m.read();
    REQUIRE_FALSE(s.valid);
    REQUIRE(s.integratedLufs == Catch::Approx(kSilenceLufs));
    REQUIRE(s.samplePeakDb == Catch::Approx(magda::daw::audio::kSilenceDb));
}

namespace {

// Deterministic programme: two detuned sines plus a spiky tail, different on each channel.
std::vector<std::uint32_t> measureBits(double sr, int numSamples) {
    TrackMeasurer m;
    m.prepare(sr, kBlock, true);
    std::vector<float> l(kBlock), r(kBlock);
    std::uint32_t lcg = 12345u;
    int done = 0;
    while (done < numSamples) {
        const int n = std::min(kBlock, numSamples - done);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(done + i) / sr;
            lcg = lcg * 1664525u + 1013904223u;
            const float noise = (static_cast<float>(lcg >> 8) / 16777216.0f - 0.5f) * 0.2f;
            const float spike = ((done + i) % 997 == 0) ? 0.45f : 0.0f;
            l[static_cast<size_t>(i)] =
                0.4f * static_cast<float>(std::sin(2.0 * kPi * 997.0 * t)) + noise + spike;
            r[static_cast<size_t>(i)] =
                0.3f * static_cast<float>(std::sin(2.0 * kPi * 3111.0 * t + 0.3)) - noise + spike;
        }
        const float* ch[2] = {l.data(), r.data()};
        m.process(ch, 2, n);
        done += n;
    }
    const auto s = m.read();
    return {std::bit_cast<std::uint32_t>(s.momentaryLufs),
            std::bit_cast<std::uint32_t>(s.shortTermLufs),
            std::bit_cast<std::uint32_t>(s.integratedLufs),
            std::bit_cast<std::uint32_t>(s.samplePeakDb),
            std::bit_cast<std::uint32_t>(s.truePeakDb),
            std::bit_cast<std::uint32_t>(s.correlation),
            std::bit_cast<std::uint32_t>(s.width)};
}

}  // namespace

TEST_CASE("TrackMeasurer - readings are bit-stable across the DSP extraction", "[measurer]") {
    // Captured from the inline biquads and oversampler before they moved to the SDK:
    // momentary, short-term, integrated, sample peak, true peak, correlation, width.
    struct Golden {
        double sampleRate;
        std::uint32_t bits[7];
    };
    const Golden goldens[] = {
        {44100.0,
         {0xc0ea2962, 0xc0ea4261, 0xc0e9999a, 0xbf0fff8d, 0xbee5c98d, 0xbd5d035b, 0x3f06a903}},
        {48000.0,
         {0xc0eabab2, 0xc0ea21cd, 0xc0e9999a, 0xbef65f92, 0xbee3fd9b, 0xbd309adf, 0x3f0551b1}},
        {96000.0,
         {0xc0ea0466, 0xc0e9e282, 0xc0e9999a, 0xbeed094c, 0xbedab83c, 0xbd2f3c70, 0x3f054526}},
    };
    for (const auto& g : goldens) {
        const auto bits = measureBits(g.sampleRate, static_cast<int>(g.sampleRate * 5));
#if defined(__APPLE__) && defined(__aarch64__)
        // Captured on this platform; other compilers round the last bit differently.
        for (size_t i = 0; i < bits.size(); ++i) {
            INFO("sample rate " << g.sampleRate << " field " << i);
            CHECK(bits[i] == g.bits[i]);
        }
#else
        CHECK(bits.size() == std::size(g.bits));
#endif
    }
}
