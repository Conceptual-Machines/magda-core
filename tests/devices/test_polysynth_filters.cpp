// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

// Poly Synth's two filters, their routing and poles (#2926), at every rate and
// switched mid-note (#3129).

using magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;
using PolySynth = MagdaPolySynthCompiledPlugin;

namespace {

constexpr int kBlockSize = 256;
// The export rates, which include every project rate.
constexpr double kSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};
constexpr double kSettleSeconds = 0.1;
constexpr double kMeasureSeconds = 0.1;

using Settings = std::vector<std::pair<int, float>>;

/// @p slot moves to @p value at the first block boundary from @p seconds.
struct Change {
    double seconds;
    int slot;
    float value;
};

int blocksIn(double rate, double seconds) {
    return static_cast<int>(std::ceil(seconds * rate / kBlockSize));
}

/// @p seconds of a C4 held from the start, through @p settings and then @p changes.
std::vector<float> renderHeld(double rate, const Settings& settings, double seconds,
                              const std::vector<Change>& changes = {}) {
    PolySynth synth;
    synth.prepare({.sampleRate = rate, .maximumBlockSize = kBlockSize});
    auto set = [&synth](int slot, float value) {
        synth.getSlotParameter(slot).setValueFromHost(synth.displayValueToNativeValue(slot, value));
    };
    for (auto [slot, value] : settings)
        set(slot, value);

    std::vector<float> samples;
    for (int block = 0; block < blocksIn(rate, seconds); ++block) {
        for (const auto& change : changes)
            if (blocksIn(rate, change.seconds) == block)
                set(change.slot, change.value);
        juce::AudioBuffer<float> audio(2, kBlockSize);
        audio.clear();
        magda::test::DeviceMidiBuffer midi, out;
        if (block == 0)
            midi.events.push_back({juce::MidiMessage::noteOn(1, 60, 1.0f), 0});
        magda::test::DeviceTestBlock contextBlock(audio, kBlockSize);
        contextBlock.context.midiIn = &midi;
        contextBlock.context.midiOut = &out;
        contextBlock.context.isPlaying = true;
        synth.process(contextBlock.context);
        samples.insert(samples.end(), audio.getReadPointer(0),
                       audio.getReadPointer(0) + kBlockSize);
    }
    return samples;
}

/// A held C4 saw through @p settings, after the envelopes and smoothing settle.
std::vector<float> render(double rate, const Settings& settings) {
    auto samples = renderHeld(rate, settings, kSettleSeconds + kMeasureSeconds);
    samples.erase(samples.begin(), samples.begin() + blocksIn(rate, kSettleSeconds) * kBlockSize);
    return samples;
}

float rms(const std::vector<float>& samples) {
    double sum = 0.0;
    for (float s : samples)
        sum += static_cast<double>(s) * s;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
}

float maxDifference(const std::vector<float>& a, const std::vector<float>& b) {
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

/// The largest sample-to-sample step in [@p from, @p to), @p from > 0.
float largestStep(const std::vector<float>& samples, size_t from, size_t to) {
    float worst = 0.0f;
    for (size_t i = from; i < to; ++i)
        worst = std::max(worst, std::abs(samples[i] - samples[i - 1]));
    return worst;
}

const Settings kFlat{{PolySynth::kResonanceSlot, 0.0f}, {PolySynth::kFilter2ResonanceSlot, 0.0f}};

Settings with(Settings settings, const Settings& more) {
    settings.insert(settings.end(), more.begin(), more.end());
    return settings;
}

}  // namespace

TEST_CASE("Filter 2 starts bypassed in series, so Filter 1 alone shapes the sound",
          "[polysynth][2926][3129]") {
    for (double rate : kSampleRates) {
        CAPTURE(rate);
        const auto byDefault = render(rate, {});
        // Parallel with all the blend on Filter 1 is Filter 1 alone by construction.
        const auto filter1Only = render(
            rate, {{PolySynth::kFilterRoutingSlot, 1.0f}, {PolySynth::kFilterBlendSlot, 0.0f}});
        REQUIRE(rms(byDefault) > 0.01f);
        CHECK(maxDifference(byDefault, filter1Only) < 1.0e-4f);
    }
}

TEST_CASE("Bypassed filters pass the oscillators through", "[polysynth][2926][3129]") {
    for (double rate : kSampleRates) {
        CAPTURE(rate);
        const Settings lowCut = with(kFlat, {{PolySynth::kCutoffSlot, 200.0f}});
        const auto bothOff = render(rate, with(lowCut, {{PolySynth::kFilter1EnableSlot, 0.0f}}));
        const auto filtered = render(rate, lowCut);
        CHECK(rms(bothOff) > 2.0f * rms(filtered));

        // Parallel, all blend on a bypassed Filter 2: the dry mix.
        const auto parallelDry = render(rate, with(lowCut, {{PolySynth::kFilterRoutingSlot, 1.0f},
                                                            {PolySynth::kFilterBlendSlot, 1.0f}}));
        CHECK(maxDifference(bothOff, parallelDry) < 1.0e-4f);
    }
}

TEST_CASE("Serial filters stack; parallel ones add", "[polysynth][2926][3129]") {
    // Filter 1 keeps the lows, Filter 2 the highs: in series little gets through both.
    const Settings split = with(kFlat, {{PolySynth::kCutoffSlot, 300.0f},
                                        {PolySynth::kFilter2EnableSlot, 1.0f},
                                        {PolySynth::kFilter2TypeSlot, 1.0f},
                                        {PolySynth::kFilter2CutoffSlot, 6000.0f}});
    for (double rate : kSampleRates) {
        CAPTURE(rate);
        const float serial = rms(render(rate, split));
        const float parallel =
            rms(render(rate, with(split, {{PolySynth::kFilterRoutingSlot, 1.0f}})));
        const float lowsOnly = rms(render(rate, with(kFlat, {{PolySynth::kCutoffSlot, 300.0f}})));

        CHECK(serial < 0.1f * lowsOnly);
        CHECK(parallel > 0.3f * lowsOnly);
    }
}

TEST_CASE("More poles cut harder past the cutoff, on both filters", "[polysynth][2926][3129]") {
    for (double rate : kSampleRates) {
        for (auto [cutoff, poles, enable] :
             {std::tuple{PolySynth::kCutoffSlot, PolySynth::kFilterSlopeSlot, -1},
              std::tuple{PolySynth::kFilter2CutoffSlot, PolySynth::kFilter2SlopeSlot,
                         PolySynth::kFilter2EnableSlot}}) {
            CAPTURE(rate, cutoff);
            Settings base = with(kFlat, {{cutoff, 600.0f}});
            if (enable >= 0)
                base = with(base, {{PolySynth::kFilter1EnableSlot, 0.0f}, {enable, 1.0f}});
            const float two = rms(render(rate, with(base, {{poles, 0.0f}})));
            const float four = rms(render(rate, with(base, {{poles, 1.0f}})));
            const float six = rms(render(rate, with(base, {{poles, 2.0f}})));
            CHECK(four < two);
            CHECK(six < four);
            for (float level : {two, four, six})
                CHECK(std::isfinite(level));
        }
    }
}

TEST_CASE("Full resonance on six poles stays finite and bounded", "[polysynth][2926][3129]") {
    for (double rate : kSampleRates) {
        CAPTURE(rate);
        const auto samples = render(rate, {{PolySynth::kFilterSlopeSlot, 2.0f},
                                           {PolySynth::kResonanceSlot, 0.95f},
                                           {PolySynth::kCutoffSlot, 18000.0f},
                                           {PolySynth::kFilter2EnableSlot, 1.0f},
                                           {PolySynth::kFilter2SlopeSlot, 2.0f},
                                           {PolySynth::kFilter2ResonanceSlot, 0.95f},
                                           {PolySynth::kFilter2CutoffSlot, 50.0f}});
        for (float s : samples) {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) <= 1.0f);
        }
    }
}

TEST_CASE("Switching filter type or poles mid-note doesn't click, on both filters",
          "[polysynth][3129]") {
    // A sine moves little from one sample to the next, so a step stands out. Each
    // value holds long enough to settle; its last 50 ms is the settled reference.
    constexpr double kHold = 0.15;
    constexpr double kSettled = 0.05;
    const Settings sine{{PolySynth::kOscBaseSlot, 0.0f},
                        {PolySynth::kAmpSustainSlot, 1.0f},
                        {PolySynth::kCutoffSlot, 400.0f},
                        {PolySynth::kFilter2CutoffSlot, 400.0f}};
    // Every pair of types, and every ordered pair of pole counts.
    const std::vector<float> types{0.0f, 1.0f, 2.0f, 3.0f, 0.0f, 2.0f, 1.0f, 3.0f};
    const std::vector<float> poles{0.0f, 1.0f, 2.0f, 0.0f, 2.0f, 1.0f, 0.0f};

    struct Sweep {
        Settings settings;
        int slot;
        const std::vector<float>& values;
    };
    std::vector<Sweep> sweeps;
    for (auto [type, slope, filter2] :
         {std::tuple{PolySynth::kFilterTypeSlot, PolySynth::kFilterSlopeSlot, false},
          std::tuple{PolySynth::kFilter2TypeSlot, PolySynth::kFilter2SlopeSlot, true}}) {
        const Settings filter = filter2 ? with(sine, {{PolySynth::kFilter1EnableSlot, 0.0f},
                                                      {PolySynth::kFilter2EnableSlot, 1.0f}})
                                        : sine;
        sweeps.push_back({filter, type, types});
        for (int t = 0; t < 4; ++t)
            sweeps.push_back({with(filter, {{type, static_cast<float>(t)}}), slope, poles});
    }

    for (double rate : kSampleRates) {
        for (const auto& sweep : sweeps) {
            std::vector<Change> changes;
            for (size_t i = 1; i < sweep.values.size(); ++i)
                changes.push_back({static_cast<double>(i) * kHold, sweep.slot, sweep.values[i]});
            const auto samples =
                renderHeld(rate, with(sweep.settings, {{sweep.slot, sweep.values[0]}}),
                           kHold * static_cast<double>(sweep.values.size()), changes);

            const auto settled = static_cast<size_t>(kSettled * rate);
            auto sampleAt = [rate](size_t hold) {
                return static_cast<size_t>(blocksIn(rate, static_cast<double>(hold) * kHold)) *
                       kBlockSize;
            };
            for (size_t i = 1; i < sweep.values.size(); ++i) {
                const size_t from = sampleAt(i);
                const size_t to = sampleAt(i + 1);
                const float before = largestStep(samples, from - settled, from);
                const float after = largestStep(samples, to - settled, to);
                CAPTURE(rate, sweep.slot, sweep.values[i - 1], sweep.values[i], before, after);
                REQUIRE(after > 0.0f);
                CHECK(largestStep(samples, from, to) < 2.0f * std::max(before, after));
            }
        }
    }
}
