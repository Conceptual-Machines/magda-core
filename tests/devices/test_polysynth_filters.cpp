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

// Poly Synth's two filters, their routing and poles (#2926).

using magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;
using PolySynth = MagdaPolySynthCompiledPlugin;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 48000.0;
constexpr int kSettleBlocks = 20;
constexpr int kMeasureBlocks = 20;

using Settings = std::vector<std::pair<int, float>>;

/// A held C4 saw through @p settings, after the envelopes and smoothing settle.
std::vector<float> render(const Settings& settings) {
    PolySynth synth;
    synth.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
    for (auto [slot, value] : settings)
        synth.getSlotParameter(slot).setValueFromHost(synth.displayValueToNativeValue(slot, value));

    std::vector<float> samples;
    for (int block = 0; block < kSettleBlocks + kMeasureBlocks; ++block) {
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
        if (block >= kSettleBlocks)
            samples.insert(samples.end(), audio.getReadPointer(0),
                           audio.getReadPointer(0) + kBlockSize);
    }
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

const Settings kFlat{{PolySynth::kResonanceSlot, 0.0f}, {PolySynth::kFilter2ResonanceSlot, 0.0f}};

Settings with(Settings settings, const Settings& more) {
    settings.insert(settings.end(), more.begin(), more.end());
    return settings;
}

}  // namespace

TEST_CASE("Filter 2 starts bypassed in series, so Filter 1 alone shapes the sound",
          "[polysynth][2926]") {
    const auto byDefault = render({});
    // Parallel with all the blend on Filter 1 is Filter 1 alone by construction.
    const auto filter1Only =
        render({{PolySynth::kFilterRoutingSlot, 1.0f}, {PolySynth::kFilterBlendSlot, 0.0f}});
    REQUIRE(rms(byDefault) > 0.01f);
    CHECK(maxDifference(byDefault, filter1Only) < 1.0e-4f);
}

TEST_CASE("Bypassed filters pass the oscillators through", "[polysynth][2926]") {
    const Settings lowCut = with(kFlat, {{PolySynth::kCutoffSlot, 200.0f}});
    const auto bothOff = render(with(lowCut, {{PolySynth::kFilter1EnableSlot, 0.0f}}));
    const auto filtered = render(lowCut);
    CHECK(rms(bothOff) > 2.0f * rms(filtered));

    // Parallel, all blend on a bypassed Filter 2: the dry mix.
    const auto parallelDry = render(
        with(lowCut, {{PolySynth::kFilterRoutingSlot, 1.0f}, {PolySynth::kFilterBlendSlot, 1.0f}}));
    CHECK(maxDifference(bothOff, parallelDry) < 1.0e-4f);
}

TEST_CASE("Serial filters stack; parallel ones add", "[polysynth][2926]") {
    // Filter 1 keeps the lows, Filter 2 the highs: in series little gets through both.
    const Settings split = with(kFlat, {{PolySynth::kCutoffSlot, 300.0f},
                                        {PolySynth::kFilter2EnableSlot, 1.0f},
                                        {PolySynth::kFilter2TypeSlot, 1.0f},
                                        {PolySynth::kFilter2CutoffSlot, 6000.0f}});
    const float serial = rms(render(split));
    const float parallel = rms(render(with(split, {{PolySynth::kFilterRoutingSlot, 1.0f}})));
    const float lowsOnly = rms(render(with(kFlat, {{PolySynth::kCutoffSlot, 300.0f}})));

    CHECK(serial < 0.1f * lowsOnly);
    CHECK(parallel > 0.3f * lowsOnly);
}

TEST_CASE("More poles cut harder past the cutoff, on both filters", "[polysynth][2926]") {
    for (auto [cutoff, poles, enable] :
         {std::tuple{PolySynth::kCutoffSlot, PolySynth::kFilterSlopeSlot, -1},
          std::tuple{PolySynth::kFilter2CutoffSlot, PolySynth::kFilter2SlopeSlot,
                     PolySynth::kFilter2EnableSlot}}) {
        CAPTURE(cutoff);
        Settings base = with(kFlat, {{cutoff, 600.0f}});
        if (enable >= 0)
            base = with(base, {{PolySynth::kFilter1EnableSlot, 0.0f}, {enable, 1.0f}});
        const float two = rms(render(with(base, {{poles, 0.0f}})));
        const float four = rms(render(with(base, {{poles, 1.0f}})));
        const float six = rms(render(with(base, {{poles, 2.0f}})));
        CHECK(four < two);
        CHECK(six < four);
        for (float level : {two, four, six})
            CHECK(std::isfinite(level));
    }
}

TEST_CASE("Full resonance on six poles stays finite and bounded", "[polysynth][2926]") {
    const auto samples = render({{PolySynth::kFilterSlopeSlot, 2.0f},
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
