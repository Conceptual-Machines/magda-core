#include <algorithm>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "magda/daw/audio/plugins/mutable/MutableRingsPlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"

// The Mutable instruments add their signal to the buffer they are handed rather than
// replace it (#2370), driven as the native engine drives them (#2556). A pre-filled
// buffer stands in for an audio clip ahead on the chain.

namespace {

namespace audio = magda::daw::audio;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;
constexpr int kNumBlocks = 8;
constexpr float kExistingSignal = 0.5f;

template <typename Device> void prepare(Device& device) {
    device.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
}

template <typename Device> void setLevelDb(Device& device, float db) {
    device.setParameterValue(Device::kLevel, magda::ParameterUtils::realToNormalized(
                                                 db, device.parameterInfo(Device::kLevel)));
}

void fillWithExistingSignal(juce::AudioBuffer<float>& buffer) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        std::fill_n(buffer.getWritePointer(ch), buffer.getNumSamples(), kExistingSignal);
}

void process(audio::MagdaDevice& device, juce::AudioBuffer<float>& buffer,
             magda::test::DeviceMidiBuffer& midi) {
    magda::test::DeviceMidiBuffer out;
    magda::test::DeviceTestBlock contextBlock(buffer);
    auto& context = contextBlock.context;
    context.midiIn = &midi;
    context.midiOut = &out;
    context.isPlaying = true;
    device.process(context);
}

struct RenderResult {
    std::vector<float> left, right;
};

/// `kNumBlocks` blocks with a note struck in the first, every sample per channel in order.
RenderResult renderNote(audio::MagdaDevice& device, bool preFillExisting) {
    RenderResult result;
    for (int block = 0; block < kNumBlocks; ++block) {
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        buffer.clear();
        if (preFillExisting)
            fillWithExistingSignal(buffer);

        magda::test::DeviceMidiBuffer midi;
        if (block == 0)
            midi.events.push_back(
                {juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0});
        process(device, buffer, midi);

        for (int i = 0; i < kBlockSize; ++i) {
            result.left.push_back(buffer.getSample(0, i));
            result.right.push_back(buffer.getSample(1, i));
        }
    }
    return result;
}

float peakAbs(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (const float s : samples)
        peak = std::max(peak, std::abs(s));
    return peak;
}

}  // namespace

TEMPLATE_TEST_CASE("A silent block adds to, and does not replace, the buffer",
                   "[devices][mutable][2556]", audio::MutableElementsPlugin,
                   audio::MutableRingsPlugin) {
    TestType device;
    prepare(device);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    fillWithExistingSignal(buffer);
    magda::test::DeviceMidiBuffer midi;
    process(device, buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            CHECK(std::abs(buffer.getSample(ch, i) - kExistingSignal) <= 0.05f);
}

TEMPLATE_TEST_CASE("A struck Mutable note reaches the buffer", "[devices][mutable][2556]",
                   audio::MutableElementsPlugin, audio::MutableRingsPlugin) {
    TestType device;
    prepare(device);

    // Rings can leave one of its outputs silent for a patch, so either channel will do.
    const auto dry = renderNote(device, false);
    CHECK(std::max(peakAbs(dry.left), peakAbs(dry.right)) > 0.01f);
}

TEMPLATE_TEST_CASE("A struck note adds onto, rather than replaces, an existing signal",
                   "[devices][mutable][2556]", audio::MutableElementsPlugin,
                   audio::MutableRingsPlugin) {
    TestType dryDevice;
    TestType wetDevice;
    prepare(dryDevice);
    prepare(wetDevice);

    const auto dry = renderNote(dryDevice, false);
    const auto wet = renderNote(wetDevice, true);

    for (size_t i = 0; i < dry.left.size(); ++i) {
        CHECK(std::abs(wet.left[i] - (dry.left[i] + kExistingSignal)) <= 0.01f);
        CHECK(std::abs(wet.right[i] - (dry.right[i] + kExistingSignal)) <= 0.01f);
    }
}

TEMPLATE_TEST_CASE("Level scales the device's own signal, not the existing buffer",
                   "[devices][mutable][2556]", audio::MutableElementsPlugin,
                   audio::MutableRingsPlugin) {
    TestType device;
    prepare(device);
    setLevelDb(device, -48.0f);

    const auto wet = renderNote(device, true);
    for (size_t i = 0; i < wet.left.size(); ++i) {
        CHECK(std::abs(wet.left[i] - kExistingSignal) <= 0.02f);
        CHECK(std::abs(wet.right[i] - kExistingSignal) <= 0.02f);
    }
}
