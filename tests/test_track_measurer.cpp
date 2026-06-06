#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "../magda/daw/audio/analysis/TrackMeasurer.hpp"

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
