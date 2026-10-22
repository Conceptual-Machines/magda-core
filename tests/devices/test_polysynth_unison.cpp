#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

// Poly Synth's unison (#2924).

using PolySynth = magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 48000.0;

using Settings = std::vector<std::pair<int, float>>;

struct Stereo {
    std::vector<float> left, right;
};

struct Event {
    int block;
    juce::MidiMessage message;
};

/// @p blocks of the synth at @p settings playing @p events.
Stereo render(const Settings& settings, const std::vector<Event>& events, int blocks) {
    PolySynth synth;
    synth.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
    for (auto [slot, value] : settings)
        synth.getSlotParameter(slot).setValueFromHost(synth.displayValueToNativeValue(slot, value));

    Stereo out;
    for (int block = 0; block < blocks; ++block) {
        juce::AudioBuffer<float> audio(2, kBlockSize);
        audio.clear();
        magda::test::DeviceMidiBuffer midi, midiOut;
        for (const auto& event : events)
            if (event.block == block)
                midi.events.push_back({event.message, 0});
        magda::test::DeviceTestBlock contextBlock(audio, kBlockSize);
        contextBlock.context.midiIn = &midi;
        contextBlock.context.midiOut = &midiOut;
        contextBlock.context.isPlaying = true;
        synth.process(contextBlock.context);
        out.left.insert(out.left.end(), audio.getReadPointer(0),
                        audio.getReadPointer(0) + kBlockSize);
        out.right.insert(out.right.end(), audio.getReadPointer(1),
                         audio.getReadPointer(1) + kBlockSize);
    }
    return out;
}

/// A held C4, the second half measured.
Stereo heldNote(const Settings& settings) {
    auto out = render(settings, {{0, juce::MidiMessage::noteOn(1, 60, 1.0f)}}, 40);
    out.left.erase(out.left.begin(), out.left.begin() + 20 * kBlockSize);
    out.right.erase(out.right.begin(), out.right.begin() + 20 * kBlockSize);
    return out;
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

}  // namespace

TEST_CASE("One loud unison voice soft-clips at full scale", "[polysynth][2924]") {
    const auto out = heldNote({{PolySynth::kUnisonVoicesSlot, 3.0f},
                               {PolySynth::kUnisonSpreadSlot, 1.0f},
                               {PolySynth::kOscBaseSlot + 1, 6.0f},
                               {PolySynth::kOscBaseSlot + 5, 6.0f}});
    for (const auto* channel : {&out.left, &out.right})
        for (float s : *channel)
            REQUIRE(std::abs(s) <= 1.0f);
}

TEST_CASE("Unison without spread stays centred", "[polysynth][2924]") {
    for (float voices : {1.0f, 2.0f, 3.0f}) {
        CAPTURE(voices);
        const auto out = heldNote(
            {{PolySynth::kUnisonVoicesSlot, voices}, {PolySynth::kUnisonSpreadSlot, 0.0f}});
        REQUIRE(rms(out.left) > 0.01f);
        CHECK(maxDifference(out.left, out.right) < 1.0e-6f);
    }
}

TEST_CASE("Spread pulls the unison copies apart", "[polysynth][2924]") {
    const auto out =
        heldNote({{PolySynth::kUnisonVoicesSlot, 2.0f}, {PolySynth::kUnisonSpreadSlot, 1.0f}});
    CHECK(maxDifference(out.left, out.right) > 0.05f);
}

TEST_CASE("Detune changes the stack, which keeps its level", "[polysynth][2924]") {
    const auto single = heldNote({});
    const auto tight = heldNote({{PolySynth::kUnisonVoicesSlot, 3.0f},
                                 {PolySynth::kUnisonDetuneSlot, 0.0f},
                                 {PolySynth::kUnisonSpreadSlot, 0.0f}});
    const auto wide = heldNote({{PolySynth::kUnisonVoicesSlot, 3.0f},
                                {PolySynth::kUnisonDetuneSlot, 40.0f},
                                {PolySynth::kUnisonSpreadSlot, 0.0f}});
    CHECK(maxDifference(tight.left, wide.left) > 0.05f);
    // Summed at 1/sqrt(n): eight detuned copies land near one copy's level.
    CHECK(rms(wide.left) > 0.5f * rms(single.left));
    CHECK(rms(wide.left) < 2.0f * rms(single.left));
}

TEST_CASE("Sixteen notes of eight-voice unison stay bounded and let go", "[polysynth][2924]") {
    std::vector<Event> events;
    for (int note = 0; note < PolySynth::kNumVoices; ++note) {
        events.push_back({0, juce::MidiMessage::noteOn(1, 36 + note * 3, 1.0f)});
        events.push_back({30, juce::MidiMessage::noteOff(1, 36 + note * 3)});
    }
    const auto out = render({{PolySynth::kUnisonVoicesSlot, 3.0f},
                             {PolySynth::kUnisonDetuneSlot, 100.0f},
                             {PolySynth::kUnisonSpreadSlot, 1.0f},
                             {PolySynth::kOscBaseSlot + 1, 6.0f},
                             {PolySynth::kAmpReleaseSlot, 20.0f}},
                            events, 80);
    // Each voice soft-clips at 1; the allocator sums them.
    for (const auto* channel : {&out.left, &out.right})
        for (float s : *channel) {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) <= static_cast<float>(PolySynth::kNumVoices));
        }

    const std::vector<float> tail(out.left.end() - kBlockSize, out.left.end());
    CHECK(rms(tail) < 1.0e-4f);
}

TEST_CASE("Mono unison retriggers on one voice and lets go", "[polysynth][2924]") {
    const auto out = render({{PolySynth::kVoiceModeSlot, 1.0f},
                             {PolySynth::kUnisonVoicesSlot, 3.0f},
                             {PolySynth::kUnisonSpreadSlot, 1.0f},
                             {PolySynth::kAmpReleaseSlot, 20.0f}},
                            {{0, juce::MidiMessage::noteOn(1, 60, 1.0f)},
                             {5, juce::MidiMessage::noteOn(1, 64, 1.0f)},
                             {10, juce::MidiMessage::noteOff(1, 64)},
                             {12, juce::MidiMessage::noteOff(1, 60)}},
                            60);
    const std::vector<float> held(out.right.begin() + 6 * kBlockSize,
                                  out.right.begin() + 10 * kBlockSize);
    CHECK(rms(held) > 0.01f);
    const std::vector<float> tail(out.right.end() - kBlockSize, out.right.end());
    CHECK(rms(tail) < 1.0e-4f);
}
