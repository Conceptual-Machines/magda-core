#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <magda/sdk/dsp/Random.hpp>
#include <numbers>
#include <vector>

#include "../../magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "devices/spectral/SpectralFx.hpp"
#include "devices/spectral/Stft.hpp"

using magda::devices::spectral::SpectralFx;
using magda::devices::spectral::Stft;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 100;  // not a divisor of the hop, so frames straddle blocks

std::vector<float> sine(double hz, float amplitude, int frames) {
    std::vector<float> out(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        out[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / kRate));
    return out;
}

/// Renders @p input as a stereo pair through @p device in kBlock blocks; returns the left channel.
std::vector<float> render(SpectralFx& device, const std::vector<float>& input) {
    std::vector<float> left = input, right = input;
    const int frames = static_cast<int>(input.size());
    for (int start = 0; start < frames; start += kBlock) {
        std::array<float*, 2> channels{left.data() + start, right.data() + start};
        magda::sdk::ProcessContext context;
        context.audio = magda::BufferView(channels.data(), 2, std::min(kBlock, frames - start));
        device.process(context);
    }
    return left;
}

void setReal(SpectralFx& device, int index, float real) {
    device.setParameterValue(index,
                             magda::sdk::realToNormalized(
                                 real, magda::sdk::domainOf(device.parameterDescriptor(index))));
}

/// Magnitude of @p signal at @p hz over [from, end), by correlation.
double levelAt(const std::vector<float>& signal, double hz, size_t from) {
    double re = 0.0, im = 0.0;
    for (size_t i = from; i < signal.size(); ++i) {
        const double w = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate;
        re += signal[i] * std::cos(w);
        im += signal[i] * std::sin(w);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(signal.size() - from);
}

double rms(const std::vector<float>& signal, size_t from) {
    double sum = 0.0;
    for (size_t i = from; i < signal.size(); ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return std::sqrt(sum / static_cast<double>(signal.size() - from));
}

}  // namespace

TEST_CASE("Stft reconstructs an untouched signal delayed by its latency", "[spectral]") {
    Stft stft;
    REQUIRE(stft.prepare(1024, 4, 1));
    REQUIRE(stft.latencySamples() == 1024);

    magda::sdk::Xoshiro256 random(7);
    std::vector<float> input(8192), wet(input.size()), dry(input.size());
    for (auto& x : input)
        x = 2.0f * random.nextFloat() - 1.0f;

    int frames = 0;
    stft.process(0, input.data(), wet.data(), dry.data(), static_cast<int>(input.size()),
                 [&frames](int, float*) { ++frames; });
    CHECK(frames == static_cast<int>(input.size()) / stft.hopSize());

    // The first latency's worth of output is the windows still filling.
    for (size_t i = 2048; i < input.size(); ++i) {
        INFO("sample " << i);
        REQUIRE(wet[i] == Catch::Approx(input[i - 1024]).margin(1.0e-4));
        REQUIRE(dry[i] == input[i - 1024]);
    }
}

TEST_CASE("Spectral FX is transparent at its defaults", "[spectral]") {
    SpectralFx device;
    device.prepare({kRate, kBlock});
    const int latency = device.latencySamples();
    REQUIRE(latency == 2048);

    const auto input = sine(440.0, 0.5f, 16384);
    const auto output = render(device, input);
    for (size_t i = 2 * static_cast<size_t>(latency); i < input.size(); ++i)
        REQUIRE(output[i] == Catch::Approx(input[i - static_cast<size_t>(latency)]).margin(1.0e-4));
}

TEST_CASE("Spectral FX mix at zero is the dry signal delayed", "[spectral]") {
    SpectralFx device;
    setReal(device, SpectralFx::kScrambleParamIndex, 100.0f);
    setReal(device, SpectralFx::kMixParamIndex, 0.0f);
    device.prepare({kRate, kBlock});

    const auto input = sine(440.0, 0.5f, 8192);
    const auto output = render(device, input);
    for (size_t i = 2048; i < input.size(); ++i)
        REQUIRE(output[i] == input[i - 2048]);
}

TEST_CASE("Spectral FX shift moves a tone by the set amount in Hz", "[spectral]") {
    SpectralFx device;
    // 317 Hz is not a whole number of bins (23.4 Hz at 48 kHz).
    setReal(device, SpectralFx::kShiftParamIndex, 317.0f);
    device.prepare({kRate, kBlock});

    const auto output = render(device, sine(1000.0, 0.5f, 48000));
    const size_t settled = 8192;
    CHECK(levelAt(output, 1317.0, settled) > 0.4);
    CHECK(levelAt(output, 1000.0, settled) < 0.01);
}

TEST_CASE("Spectral FX scramble smears a tone but keeps its level", "[spectral]") {
    SpectralFx device;
    setReal(device, SpectralFx::kScrambleParamIndex, 100.0f);
    device.prepare({kRate, kBlock});

    const auto input = sine(1000.0, 0.5f, 48000);
    const auto output = render(device, input);
    const size_t settled = 8192;
    const double ratio = rms(output, settled) / rms(input, settled);
    CHECK(ratio > 0.8);
    CHECK(ratio < 1.25);
    // Random phases break the tone's frame-to-frame coherence.
    CHECK(levelAt(output, 1000.0, settled) < 0.25);
}

namespace {

/// A 1 kHz tone of @p amplitude over uniform noise of @p noise peak.
std::vector<float> toneInNoise(float amplitude, float noise, std::uint64_t seed) {
    magda::sdk::Xoshiro256 random(seed);
    auto signal = sine(1000.0, amplitude, 48000);
    for (auto& x : signal)
        x += noise * (2.0f * random.nextFloat() - 1.0f);
    return signal;
}

/// RMS of everything but the 1 kHz tone, relative to the tone's amplitude.
double floorBelowTone(const std::vector<float>& signal) {
    const double tone = levelAt(signal, 1000.0, 8192);
    const double total = rms(signal, 8192);
    return std::sqrt(std::max(0.0, total * total - 0.5 * tone * tone)) / tone;
}

double floorAfter(int param, float value, const std::vector<float>& input) {
    SpectralFx device;
    setReal(device, param, value);
    device.prepare({kRate, kBlock});
    return floorBelowTone(render(device, input));
}

}  // namespace

TEST_CASE("Spectral FX gate removes bins below its threshold", "[spectral]") {
    const auto input = toneInNoise(0.5f, 0.01f, 3);
    const double open = floorAfter(SpectralFx::kGateParamIndex, SpectralFx::kGateOffDb, input);
    const double gated = floorAfter(SpectralFx::kGateParamIndex, -30.0f, input);
    // What survives is the noise under the tone's own window lobes.
    CHECK(gated < 0.35 * open);

    SpectralFx device;
    setReal(device, SpectralFx::kGateParamIndex, -30.0f);
    device.prepare({kRate, kBlock});
    CHECK(levelAt(render(device, input), 1000.0, 8192) == Catch::Approx(0.5).margin(0.02));
}

TEST_CASE("Spectral FX gate acts at the level of broadband material", "[spectral]") {
    // White noise at -20 dBFS RMS.
    magda::sdk::Xoshiro256 random(9);
    std::vector<float> input(48000);
    for (auto& x : input)
        x = 0.1f * std::sqrt(3.0f) * (2.0f * random.nextFloat() - 1.0f);

    const auto outDb = [&input](float gateDb) {
        SpectralFx device;
        setReal(device, SpectralFx::kGateParamIndex, gateDb);
        device.prepare({kRate, kBlock});
        return 20.0 * std::log10(rms(render(device, input), 8192) + 1.0e-12);
    };
    const double open = outDb(SpectralFx::kGateOffDb);
    CHECK(outDb(-40.0f) == Catch::Approx(open).margin(0.5));
    // Gating at the noise's own RMS drops the quieter 63% of its (Rayleigh) bins, a quarter of
    // its energy.
    CHECK(outDb(-20.0f) < open - 1.0);
    CHECK(outDb(-6.0f) < open - 30.0);
}

TEST_CASE("Spectral FX contrast trades the floor against the peaks", "[spectral]") {
    const auto input = toneInNoise(0.3f, 0.05f, 5);
    const double neutral = floorAfter(SpectralFx::kContrastParamIndex, 0.0f, input);
    CHECK(floorAfter(SpectralFx::kContrastParamIndex, 100.0f, input) < 0.5 * neutral);
    CHECK(floorAfter(SpectralFx::kContrastParamIndex, -100.0f, input) > 2.0 * neutral);
}

TEST_CASE("Spectral FX publishes a spectrogram column per frame", "[spectral]") {
    using magda::devices::spectral::SpectrogramTelemetry;
    SpectralFx device;
    device.prepare({kRate, kBlock});
    const auto* ring =
        dynamic_cast<const SpectrogramTelemetry*>(device.telemetry(SpectrogramTelemetry::kKey));
    REQUIRE(ring != nullptr);

    render(device, sine(1000.0, 0.5f, 48000));
    constexpr int bands = SpectrogramTelemetry::kColumnBands;
    CHECK(ring->writePosition() == static_cast<size_t>(48000 / 512 * bands));

    std::array<float, bands> column{};
    ring->readLatest(column.data(), bands);
    const auto loudest = std::distance(column.begin(), std::ranges::max_element(column));
    const double expected = bands * std::log(1000.0 / SpectrogramTelemetry::kMinHz) /
                            std::log(SpectrogramTelemetry::kMaxHz / SpectrogramTelemetry::kMinHz);
    CHECK(std::abs(static_cast<double>(loudest) - expected) <= 1.5);
    // A tone's energy sits in a few bins, so it reads well above its 0.5 amplitude's -6 dBFS.
    CHECK(column[static_cast<size_t>(loudest)] == Catch::Approx(20.2f).margin(2.0f));
    CHECK(std::ranges::min(column) >= SpectrogramTelemetry::kFloorDb);
}

TEST_CASE("Spectral FX is registered as a browsable spectral effect", "[spectral][device]") {
    const auto* spec = magda::daw::audio::findInternalPluginSpec(SpectralFx::xmlTypeName);
    REQUIRE(spec != nullptr);
    CHECK(juce::String(spec->browserCategory) == "Spectral");
    CHECK(spec->showInBrowser);
    CHECK_FALSE(spec->isInstrument);
    CHECK(spec->createDevice != nullptr);
    // Mix is the slot's side-strip knob.
    CHECK(spec->mixParamIndex == SpectralFx::kMixParamIndex);
}
