// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "magda/daw/audio/plugins/mutable/MutableResampler.hpp"
#include "magda/daw/audio/plugins/mutable/MutableRingsPlugin.hpp"

// The output leg Elements (32 kHz) and Rings (48 kHz) cross to the host rate
// on (#2409). Clouds' crossings are pinned in test_mutable_clouds.cpp; these
// are the instrument side, read against the leg directly because an
// instrument has no input to put a test tone through.

namespace {

namespace audio = magda::daw::audio;
namespace mr = audio::mutable_resampling;

constexpr double kElementsRate = 32000.0;
constexpr double kRingsRate = 48000.0;
constexpr int kChunk = 128;

/// A tone at the internal rate through a leg, read at the host rate after a settling span.
std::vector<float> toneThroughLeg(double internalRate, double hostRate, double hz) {
    mr::OutputLeg leg;
    leg.prepare(internalRate, hostRate);

    long long k = 0;
    const auto pull = [&](float& l, float& r) {
        l = r = static_cast<float>(
            0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * hz * k++ / internalRate));
    };

    const int settle = static_cast<int>(0.05 * hostRate);
    const int length = static_cast<int>(0.25 * hostRate);
    std::vector<float> out(static_cast<size_t>(settle + length));
    for (int pos = 0; pos < static_cast<int>(out.size()); pos += kChunk)
        leg.render(out.data() + pos, nullptr, std::min(kChunk, static_cast<int>(out.size()) - pos),
                   pull);
    return {out.begin() + settle, out.end()};
}

/// Energy at `hz` over the whole signal (Goertzel).
double toneEnergy(const std::vector<float>& signal, double rate, double hz) {
    const double coeff = 2.0 * std::cos(2.0 * juce::MathConstants<double>::pi * hz / rate);
    double s1 = 0.0, s2 = 0.0;
    for (const float x : signal) {
        const double s = coeff * s1 - s2 + x;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

/// How far below a 1 kHz passband tone a `inHz` tone reads at `probeHz`, in dB.
double dbBelowReference(double internalRate, double hostRate, double inHz, double probeHz) {
    const double reference =
        toneEnergy(toneThroughLeg(internalRate, hostRate, 1000.0), hostRate, 1000.0);
    const double energy =
        toneEnergy(toneThroughLeg(internalRate, hostRate, inHz), hostRate, probeHz);
    return -10.0 * std::log10(energy / reference + 1.0e-30);
}

/// A note's delay through the leg, in host samples: the DSP starts a 4 kHz half-cycle on the
/// first sample it is asked for after host sample `at`, the way a note lands mid-block.
double noteDelaySamples(double internalRate, double hostRate, int at) {
    mr::OutputLeg leg;
    leg.prepare(internalRate, hostRate);

    constexpr double kWidthSeconds = 1.0 / 8000.0;
    const auto halfCycle = [](double t) {
        return (t >= 0.0 && t < kWidthSeconds)
                   ? 0.5 * std::sin(juce::MathConstants<double>::pi * t / kWidthSeconds)
                   : 0.0;
    };

    bool started = false;
    long long k = 0;
    const auto pull = [&](float& l, float& r) {
        const double t = started ? static_cast<double>(k++) / internalRate : -1.0;
        l = r = static_cast<float>(halfCycle(t));
    };

    std::vector<float> out(static_cast<size_t>(0.03 * hostRate));
    leg.render(out.data(), nullptr, at, pull);
    started = true;
    leg.render(out.data() + at, nullptr, static_cast<int>(out.size()) - at, pull);

    const int width = static_cast<int>(std::ceil(kWidthSeconds * hostRate)) + 1;
    double best = -1.0e30;
    int bestLag = 0;
    for (int lag = 0; lag < static_cast<int>(0.005 * hostRate); ++lag) {
        double acc = 0.0;
        for (int i = 0; i < width; ++i)
            acc += halfCycle(static_cast<double>(i) / hostRate) *
                   out[static_cast<size_t>(at + i + lag)];
        if (acc > best) {
            best = acc;
            bestLag = lag;
        }
    }
    return static_cast<double>(bestLag);
}

/// Where a note falls between DSP samples moves its delay by up to one; this is the mean over
/// sixteen positions.
double meanNoteDelaySamples(double internalRate, double hostRate) {
    const int at = static_cast<int>(std::lround(0.01 * hostRate));
    double sum = 0.0;
    for (int offset = 0; offset < 16; ++offset)
        sum += noteDelaySamples(internalRate, hostRate, at + offset);
    return sum / 16.0;
}

constexpr double kHostRates[] = {22050.0, 32000.0, 44100.0,  48000.0,
                                 88200.0, 96000.0, 176400.0, 192000.0};

}  // namespace

TEST_CASE("Mutable output leg band-limits the images it makes rising to the host rate",
          "[mutable][resampler]") {
    // At least what the cubic and Butterworth measured before #3328: 130.7 and 68.3 dB.
    const double elements = dbBelowReference(kElementsRate, 48000.0, 10000.0, 22000.0);
    const double rings = dbBelowReference(kRingsRate, 96000.0, 15000.0, 33000.0);
    INFO("32k->48k: 10k image at 22k " << elements << " dB down; 48k->96k: 15k image at 33k "
                                       << rings << " dB down");
    CHECK(elements > 130.0);
    CHECK(rings > 68.0);
}

TEST_CASE("Mutable output leg band-limits what would fold falling to the host rate",
          "[mutable][resampler]") {
    // Rings at a 44.1 kHz host is a decimation, so its top 2 kHz folds unless it goes first.
    // At least what the cubic and Butterworth measured before #3328: 105.9 and 59.1 dB.
    const double rings = dbBelowReference(kRingsRate, 44100.0, 23000.0, 21100.0);
    const double elements = dbBelowReference(kElementsRate, 22050.0, 13000.0, 9050.0);
    INFO("48k->44.1k: 23k folding to 21.1k " << rings << " dB down; 32k->22.05k: 13k folding to "
                                             << "9.05k " << elements << " dB down");
    CHECK(rings > 105.0);
    CHECK(elements > 59.0);
}

TEST_CASE("Mutable output leg keeps the passband", "[mutable][resampler]") {
    for (const auto [internalRate, hostRate] :
         {std::pair{kElementsRate, 44100.0}, std::pair{kElementsRate, 48000.0},
          std::pair{kRingsRate, 44100.0}, std::pair{kRingsRate, 96000.0}}) {
        for (const double hz : {1000.0, 5000.0, 10000.0}) {
            const double loss = dbBelowReference(internalRate, hostRate, hz, hz);
            INFO(internalRate << " -> " << hostRate << ": " << hz << " Hz down " << loss << " dB");
            CHECK(loss < 0.1);
        }
    }
}

TEST_CASE("Mutable output leg declares the delay it imposes", "[mutable][resampler][latency]") {
    // Every rate the application uses, the 22.05 kHz analysis render included,
    // either side of both internal rates. Checked against a whole host sample,
    // the resolution the correlation reads at.
    for (const double internalRate : {kElementsRate, kRingsRate}) {
        for (const double hostRate : kHostRates) {
            mr::OutputLeg leg;
            leg.prepare(internalRate, hostRate);
            const double declared = leg.latencySeconds() * hostRate;
            const double measured = meanNoteDelaySamples(internalRate, hostRate);
            INFO(internalRate << " -> " << hostRate << ": declared " << declared
                              << " samples, measured " << measured);
            CHECK(std::abs(measured - declared) <= 1.0);
        }
    }
}

TEST_CASE("Mutable output leg renders the same whatever the callback partition",
          "[mutable][resampler]") {
    // It pulls DSP samples on demand, so a partition decides only when the
    // pulls happen, never what they produce.
    const auto renderWith = [](const std::vector<int>& partitions) {
        mr::OutputLeg leg;
        leg.prepare(kRingsRate, 44100.0);
        long long k = 0;
        const auto pull = [&](float& l, float& r) {
            l = r = static_cast<float>(
                std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * k++ / kRingsRate));
        };
        std::vector<float> out(13230);
        size_t next = 0;
        for (int pos = 0; pos < static_cast<int>(out.size());) {
            const int n = std::min(partitions[next++ % partitions.size()],
                                   static_cast<int>(out.size()) - pos);
            leg.render(out.data() + pos, nullptr, n, pull);
            pos += n;
        }
        return out;
    };

    const auto reference = renderWith({kChunk});
    for (const auto& partition : std::vector<std::vector<int>>{{1}, {16}, {37, 1, 512, 3, 129}})
        CHECK(renderWith(partition) == reference);
}

TEST_CASE("Elements and Rings declare their output leg's delay", "[mutable][latency]") {
    for (const double rate : kHostRates) {
        audio::MutableElementsPlugin elements;
        audio::MutableRingsPlugin rings;
        elements.prepare({.sampleRate = rate, .maximumBlockSize = kChunk});
        rings.prepare({.sampleRate = rate, .maximumBlockSize = kChunk});

        mr::OutputLeg elementsLeg, ringsLeg;
        elementsLeg.prepare(kElementsRate, rate);
        ringsLeg.prepare(kRingsRate, rate);

        INFO("rate " << rate);
        CHECK(elements.latencySamples() == juce::roundToInt(elementsLeg.latencySeconds() * rate));
        CHECK(rings.latencySamples() == juce::roundToInt(ringsLeg.latencySeconds() * rate));
    }
}

TEST_CASE("Elements and Rings stay finite at the offline analysis rate", "[mutable][resampler]") {
    // OfflineMixAnalysis renders at 22.05 kHz, where an unclamped corner is an
    // unstable biquad (#2365 review).
    const auto render = [](audio::MagdaDevice& device, double rate) {
        device.prepare({.sampleRate = rate, .maximumBlockSize = kChunk});
        int nonFinite = 0;
        float peak = 0.0f;
        for (int block = 0; block < 64; ++block) {
            juce::AudioBuffer<float> buffer(2, kChunk);
            buffer.clear();
            magda::test::DeviceMidiBuffer midi;
            if (block == 0)
                midi.events.push_back({juce::MidiMessage::noteOn(1, 60, juce::uint8{100}), 0});
            magda::test::DeviceTestBlock contextBlock(buffer, kChunk);
            contextBlock.context.midiIn = &midi;
            contextBlock.context.isPlaying = true;
            device.process(contextBlock.context);
            // Both channels: Rings can leave either at exact silence for a given Structure.
            for (int channel = 0; channel < 2; ++channel) {
                for (int i = 0; i < kChunk; ++i) {
                    const float s = buffer.getSample(channel, i);
                    if (!std::isfinite(s))
                        ++nonFinite;
                    else
                        peak = std::max(peak, std::abs(s));
                }
            }
        }
        return std::pair{nonFinite, peak};
    };

    for (const double rate : {22050.0, 24000.0}) {
        audio::MutableElementsPlugin elements;
        audio::MutableRingsPlugin rings;
        const auto [elementsNonFinite, elementsPeak] = render(elements, rate);
        const auto [ringsNonFinite, ringsPeak] = render(rings, rate);
        INFO("rate " << rate << ": Elements peak " << elementsPeak << ", Rings peak " << ringsPeak);
        CHECK(elementsNonFinite == 0);
        CHECK(ringsNonFinite == 0);
        CHECK(elementsPeak > 0.0f);
        CHECK(ringsPeak > 0.0f);
    }
}
