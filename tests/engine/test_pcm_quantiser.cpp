#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "io/PcmQuantiser.hpp"

/**
 * @file test_pcm_quantiser.cpp
 * @brief What dither is for, measured rather than asserted (#2248).
 *
 * The case the omission is audible in: a sine one LSB tall. Rounded on its own
 * it becomes a three-level staircase, which is a square wave, which is a tone
 * nobody played. Dithered it becomes the sine it was, under a noise floor.
 *
 * Asserted on band energies with a fixed seed rather than on samples. What
 * makes dither right is the shape of what it leaves behind, and a test that
 * pinned individual samples would be pinning the generator.
 */

using magda::engine::DitherMode;
using magda::engine::PcmQuantiser;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kFftOrder = 13;
constexpr int kFftSize = 1 << static_cast<unsigned>(kFftOrder);

/// A tone at @p amplitude, which for these cases is about one 16-bit LSB.
constexpr double kToneHz = 1000.0;

/// -90 dBFS, which at 16 bits is a hair over one least significant bit. The
/// level the omission is audible at, and the level the issue names.
constexpr float kQuietAmplitude = 3.16e-5f;

juce::AudioBuffer<float> tone(int numSamples, float amplitude) {
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* samples = buffer.getWritePointer(0);

    for (auto at = 0; at < numSamples; ++at)
        samples[at] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * kToneHz *
                                                    static_cast<double>(at) / kSampleRate));

    return buffer;
}

/// The magnitude spectrum of @p buffer's first channel, Hann-windowed.
std::vector<float> spectrumOf(const juce::AudioBuffer<float>& buffer) {
    std::vector<float> data(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
    const auto* samples = buffer.getReadPointer(0);

    for (auto at = 0; at < kFftSize; ++at) {
        const auto window =
            0.5 - (0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(at) / kFftSize));
        data[static_cast<std::size_t>(at)] = samples[at] * static_cast<float>(window);
    }

    juce::dsp::FFT fft(kFftOrder);
    fft.performFrequencyOnlyForwardTransform(data.data());

    data.resize(static_cast<std::size_t>(kFftSize) / 2);
    return data;
}

int binFor(double hz) {
    return static_cast<int>(std::lround(hz * kFftSize / kSampleRate));
}

/// The energy in the bins around @p hz, which is where a tone lands once a
/// window has spread it over three of them.
float energyAt(const std::vector<float>& spectrum, double hz) {
    const auto centre = binFor(hz);
    auto sum = 0.0f;

    for (auto bin = centre - 2; bin <= centre + 2; ++bin)
        if (bin >= 0 && bin < static_cast<int>(spectrum.size()))
            sum +=
                spectrum[static_cast<std::size_t>(bin)] * spectrum[static_cast<std::size_t>(bin)];

    return sum;
}

/// Energy between @p lowHz and @p highHz, the fundamental's own bins excluded:
/// what these cases compare is the noise, and the tone is not it.
float energyBetween(const std::vector<float>& spectrum, double lowHz, double highHz) {
    const auto first = juce::jmax(1, binFor(lowHz));
    const auto last = juce::jmin(static_cast<int>(spectrum.size()) - 1, binFor(highHz));
    const auto toneCentre = binFor(kToneHz);

    auto sum = 0.0f;
    for (auto bin = first; bin <= last; ++bin) {
        if (std::abs(bin - toneCentre) <= 3)
            continue;

        sum += spectrum[static_cast<std::size_t>(bin)] * spectrum[static_cast<std::size_t>(bin)];
    }

    return sum;
}

/// The odd harmonics a three-level staircase is made of.
float harmonicEnergy(const std::vector<float>& spectrum) {
    return energyAt(spectrum, kToneHz * 3.0) + energyAt(spectrum, kToneHz * 5.0) +
           energyAt(spectrum, kToneHz * 7.0);
}

std::vector<float> quantisedSpectrum(DitherMode mode) {
    auto buffer = tone(kFftSize, kQuietAmplitude);
    PcmQuantiser quantiser(16, 1, mode);
    quantiser.process(buffer, kFftSize);
    return spectrumOf(buffer);
}

}  // namespace

TEST_CASE("Rounding a one-bit sine on its own makes harmonics of it",
          "[engine][io][dither][2248]") {
    const auto spectrum = quantisedSpectrum(DitherMode::none);

    // A tone that was never played. Three peaks carry about two thirds of
    // everything in the band that is not the fundamental, which is what a
    // three-level staircase looks like from the outside.
    const auto harmonics = harmonicEnergy(spectrum);
    const auto band = energyBetween(spectrum, 200.0, 20000.0);

    INFO("harmonics " << harmonics << " against a band of " << band);
    CHECK(harmonics > band * 0.4f);
}

TEST_CASE("TPDF dither leaves a floor instead of harmonics", "[engine][io][dither][2248]") {
    const auto plain = quantisedSpectrum(DitherMode::none);
    const auto dithered = quantisedSpectrum(DitherMode::tpdf);

    // The harmonics go, and what is left in their place is spread over the
    // whole band rather than standing at a frequency.
    INFO("undithered " << harmonicEnergy(plain) << ", dithered " << harmonicEnergy(dithered));
    CHECK(harmonicEnergy(dithered) < harmonicEnergy(plain) * 0.1f);
    CHECK(energyBetween(dithered, 200.0, 20000.0) > energyBetween(plain, 200.0, 20000.0));
}

TEST_CASE("The tone survives the dither that hides its harmonics", "[engine][io][dither][2248]") {
    const auto dithered = quantisedSpectrum(DitherMode::tpdf);

    // The point of dithering rather than rounding: the signal is still there,
    // below the level a single LSB could carry on its own.
    const auto atTone = energyAt(dithered, kToneHz);
    const auto elsewhere = energyAt(dithered, kToneHz * 3.0);

    INFO("tone " << atTone << " against " << elsewhere);
    CHECK(atTone > elsewhere * 10.0f);
}

TEST_CASE("Shaping moves the floor above the ear", "[engine][io][dither][2248]") {
    const auto flat = quantisedSpectrum(DitherMode::tpdf);
    const auto shaped = quantisedSpectrum(DitherMode::shaped);

    // The same noise, spent where there is less hearing to spend it on.
    const auto lowFlat = energyBetween(flat, 200.0, 10000.0);
    const auto lowShaped = energyBetween(shaped, 200.0, 10000.0);
    const auto highFlat = energyBetween(flat, 10000.0, 20000.0);
    const auto highShaped = energyBetween(shaped, 10000.0, 20000.0);

    INFO("below 10k: flat " << lowFlat << " shaped " << lowShaped);
    INFO("above 10k: flat " << highFlat << " shaped " << highShaped);

    CHECK(lowShaped < lowFlat);
    CHECK(highShaped > highFlat);
}

TEST_CASE("A render of one project produces one file", "[engine][io][dither][2248]") {
    auto first = tone(1024, 0.1f);
    auto second = tone(1024, 0.1f);

    PcmQuantiser one(16, 1, DitherMode::tpdf);
    PcmQuantiser two(16, 1, DitherMode::tpdf);
    one.process(first, 1024);
    two.process(second, 1024);

    for (auto at = 0; at < 1024; ++at) {
        INFO("sample " << at);
        REQUIRE(first.getSample(0, at) == second.getSample(0, at));
    }

    // And the same quantiser again, once it has been told the render restarted.
    auto third = tone(1024, 0.1f);
    one.reset();
    one.process(third, 1024);

    for (auto at = 0; at < 1024; ++at) {
        INFO("sample " << at);
        REQUIRE(third.getSample(0, at) == first.getSample(0, at));
    }
}

TEST_CASE("Every sample lands on the target's grid", "[engine][io][dither][2248]") {
    auto buffer = tone(1024, 0.5f);
    PcmQuantiser quantiser(16, 1, DitherMode::tpdf);
    quantiser.process(buffer, 1024);

    // What comes back is what the file will hold, so it is a whole number of
    // steps and it is inside the format.
    for (auto at = 0; at < 1024; ++at) {
        const auto sample = buffer.getSample(0, at);
        INFO("sample " << at << " = " << sample);
        REQUIRE(std::abs(sample) <= 1.0f);
        REQUIRE(std::abs(std::nearbyint(sample / quantiser.lsb()) - (sample / quantiser.lsb())) <
                0.01f);
    }
}

TEST_CASE("A full-scale sample is held rather than wrapped", "[engine][io][dither][2248]") {
    juce::AudioBuffer<float> buffer(1, 4);
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(0, 1, -1.0f);
    buffer.setSample(0, 2, 1.0f);
    buffer.setSample(0, 3, -1.0f);

    PcmQuantiser quantiser(16, 1, DitherMode::tpdf);
    quantiser.process(buffer, 4);

    // A dither step on top of full scale is over the top of the format. Held
    // there, because the alternative is a sign flip and that is a click.
    for (auto at = 0; at < 4; ++at) {
        INFO("sample " << at);
        CHECK(std::abs(buffer.getSample(0, at)) <= 1.0f);
        CHECK(std::abs(buffer.getSample(0, at)) > 0.99f);
    }
}

TEST_CASE("Twenty-four bits is a finer grid than sixteen", "[engine][io][dither][2248]") {
    const PcmQuantiser sixteen(16, 1, DitherMode::tpdf);
    const PcmQuantiser twentyFour(24, 1, DitherMode::tpdf);

    // 2^8 steps between them, which is why the issue calls dither at 24 bits
    // correct and free rather than audible.
    CHECK(sixteen.lsb() / twentyFour.lsb() == Catch::Approx(256.0f).epsilon(0.01));
}

TEST_CASE("Asking for no dither rounds and nothing else", "[engine][io][dither][2248]") {
    auto buffer = tone(256, 0.25f);
    auto expected = tone(256, 0.25f);

    PcmQuantiser quantiser(16, 1, DitherMode::none);
    quantiser.process(buffer, 256);

    const auto lsb = quantiser.lsb();
    for (auto at = 0; at < 256; ++at) {
        INFO("sample " << at);
        CHECK(buffer.getSample(0, at) ==
              Catch::Approx(std::nearbyint(expected.getSample(0, at) / lsb) * lsb).margin(1e-7));
    }
}
