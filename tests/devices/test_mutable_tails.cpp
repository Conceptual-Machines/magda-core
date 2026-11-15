// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "magda/daw/audio/plugins/mutable/MutableRingsPlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"

// Elements and Rings declared 3 s and 4 s whatever their settings, chosen rather
// than measured. Their decay runs from a few milliseconds to past a minute
// across Damping alone: Elements at full Space rings 6.75 s and Rings' modal
// model at 0.7 rings 8.6 s, so a bounce trusting the constants cut them short
// (#2409). The declaration follows the settings now, and these hold it to what
// the devices actually do: still sounding above -60 dBFS, the criterion Nimbus
// uses.

namespace {

namespace audio = magda::daw::audio;
using audio::MutableElementsPlugin;
using audio::MutableRingsPlugin;

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

template <typename Device> void set(Device& device, int slot, float displayValue) {
    device.setParameterValue(
        slot, magda::ParameterUtils::realToNormalized(displayValue, device.parameterInfo(slot)));
}

template <typename Device> double declaredSeconds(const Device& device) {
    return static_cast<double>(device.tailSamples()) / kRate;
}

/// Seconds after the last MIDI event until either channel last exceeds -60 dBFS.
/// Saturates at the window, which is still a lower bound on what the declaration
/// has to cover.
template <typename Device>
double measuredTail(Device& device, int note, double holdSeconds, double windowSeconds) {
    device.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});
    const int offAt = static_cast<int>(holdSeconds * kRate);
    const int total = offAt + static_cast<int>(windowSeconds * kRate);
    int last = offAt;
    for (int pos = 0; pos < total; pos += kBlock) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        magda::test::DeviceMidiBuffer midi;
        if (pos == 0)
            midi.events.push_back({juce::MidiMessage::noteOn(1, note, juce::uint8{127}), 0});
        if (holdSeconds > 0.0 && offAt >= pos && offAt < pos + kBlock)
            midi.events.push_back(
                {juce::MidiMessage::noteOff(1, note).withTimeStamp((offAt - pos) / kRate), 0});
        magda::test::DeviceTestBlock contextBlock(buffer, kBlock);
        contextBlock.context.midiIn = &midi;
        contextBlock.context.isPlaying = true;
        device.process(contextBlock.context);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < kBlock; ++i)
                if (std::abs(buffer.getSample(channel, i)) > 0.001f)
                    last = std::max(last, pos + i);
    }
    return static_cast<double>(last - offAt) / kRate;
}

struct ElementsPatch {
    float damping = 0.7f;
    float space = 0.3f;
    bool blown = false;
    float levelDb = 0.0f;
    int note = 48;
};

/// Declared and measured tails after a quarter-second note, struck or blown.
std::pair<double, double> elementsTail(const ElementsPatch& patch, double windowSeconds) {
    MutableElementsPlugin device;
    device.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});
    set(device, MutableElementsPlugin::kDamping, patch.damping);
    set(device, MutableElementsPlugin::kSpace, patch.space);
    set(device, MutableElementsPlugin::kLevel, patch.levelDb);
    if (patch.blown) {
        set(device, MutableElementsPlugin::kStrike, 0.0f);
        set(device, MutableElementsPlugin::kBlow, 1.0f);
    }
    const double declared = declaredSeconds(device);
    return {declared, measuredTail(device, patch.note, 0.25, windowSeconds)};
}

/// Declared and measured tails after one strum.
std::pair<double, double> ringsTail(int model, float damping, float levelDb, int note,
                                    double windowSeconds) {
    MutableRingsPlugin device;
    device.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});
    set(device, MutableRingsPlugin::kModel, static_cast<float>(model));
    set(device, MutableRingsPlugin::kDamping, damping);
    set(device, MutableRingsPlugin::kLevel, levelDb);
    const double declared = declaredSeconds(device);
    return {declared, measuredTail(device, note, 0.0, windowSeconds)};
}

}  // namespace

TEST_CASE("Elements declares the tail it is actually running", "[mutable][elements][tail]") {
    // The named reproductions only; the full matrix is under [.mutable-sweep].
    SECTION("the default patch") {
        const auto [declared, measured] = elementsTail({}, 12.0);
        INFO("declared " << declared << "s, still sounding at " << measured << "s");
        CHECK(declared >= measured);
    }

    SECTION("Space's reverb at the top of its range") {
        const auto [declared, measured] = elementsTail({.damping = 0.3f, .space = 1.0f}, 15.0);
        INFO("declared " << declared << "s, still sounding at " << measured << "s");
        CHECK(declared >= measured);
        CHECK(measured > 6.0);  // the reverb this case exists to catch
    }

    SECTION("a blown high note at +12 dB, the closest the sweep came") {
        const auto [declared, measured] = elementsTail(
            {.damping = 0.8f, .space = 0.0f, .blown = true, .levelDb = 12.0f, .note = 84}, 25.0);
        INFO("declared " << declared << "s, still sounding at " << measured << "s");
        CHECK(declared >= measured);
    }
}

TEST_CASE("Elements' declared tail follows its settings", "[mutable][elements][tail]") {
    const auto declared = [](const ElementsPatch& patch) {
        MutableElementsPlugin device;
        device.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});
        set(device, MutableElementsPlugin::kDamping, patch.damping);
        set(device, MutableElementsPlugin::kSpace, patch.space);
        set(device, MutableElementsPlugin::kLevel, patch.levelDb);
        if (patch.blown)
            set(device, MutableElementsPlugin::kBlow, 1.0f);
        return declaredSeconds(device);
    };

    CHECK(declared({.damping = 0.8f}) > declared({.damping = 0.5f}));
    CHECK(declared({.space = 1.0f}) > declared({.space = 0.5f}));
    CHECK(declared({.blown = true}) > declared({}));
    CHECK(declared({.levelDb = 12.0f}) > declared({}));
    CHECK(declared({.levelDb = -12.0f}) == Catch::Approx(declared({})));

    // Full Damping rings for most of a minute; past the ceiling it counts as sustaining.
    CHECK(declared({.damping = 1.0f, .space = 1.0f, .blown = true, .levelDb = 12.0f}) ==
          Catch::Approx(MutableElementsPlugin::kMaxTailSeconds));
}

TEST_CASE("Rings declares the tail each model is actually running", "[mutable][rings][tail]") {
    // Damping 0.7 in every model: long enough to have a tail worth cutting, short
    // of the settings where the strings stop decaying.
    for (int model = 0; model < 6; ++model) {
        const auto [declared, measured] = ringsTail(model, 0.7f, 0.0f, 48, 25.0);
        INFO("model " << model << ": declared " << declared << "s, still sounding at " << measured
                      << "s");
        CHECK(declared >= measured);
    }
}

TEST_CASE("Rings' declared tail follows its settings", "[mutable][rings][tail]") {
    const auto declared = [](int model, float damping, float levelDb = 0.0f) {
        MutableRingsPlugin device;
        device.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});
        set(device, MutableRingsPlugin::kModel, static_cast<float>(model));
        set(device, MutableRingsPlugin::kDamping, damping);
        set(device, MutableRingsPlugin::kLevel, levelDb);
        return declaredSeconds(device);
    };

    for (int model = 0; model < 6; ++model) {
        INFO("model " << model);
        CHECK(declared(model, 0.6f) > declared(model, 0.3f));
        CHECK(declared(model, 0.6f, 12.0f) > declared(model, 0.6f));
    }

    // Where the upstream decay stops: the strings crossfade to infinite from 0.95
    // (sympathetic from 0.926, String+Verb from 0.776), and FM's envelope stops
    // closing past 0.9. Policy, checked without rendering.
    CHECK(declared(2, 0.97f) == Catch::Approx(MutableRingsPlugin::kMaxTailSeconds));
    CHECK(declared(1, 0.93f) == Catch::Approx(MutableRingsPlugin::kMaxTailSeconds));
    CHECK(declared(5, 0.8f) == Catch::Approx(MutableRingsPlugin::kMaxTailSeconds));
    CHECK(declared(3, 0.95f) == Catch::Approx(MutableRingsPlugin::kMaxTailSeconds));
}

//==============================================================================
// Sweeps. Tagged with a leading dot, so Catch2 leaves them out of a bare run and
// `magda_tests "[mutable-sweep]"` still picks them up. They follow every decay
// to silence at +12 dB and the extremes of the keyboard, which costs a few
// minutes of rendering.

TEST_CASE("Elements tail declaration covers damping, space, exciter and pitch",
          "[.mutable-sweep]") {
    for (const float damping : {0.3f, 0.6f, 0.8f, 0.9f}) {
        for (const float space : {0.0f, 0.85f, 1.0f}) {
            for (const bool blown : {false, true}) {
                for (const int note : {24, 84}) {
                    const auto [declared, measured] = elementsTail({.damping = damping,
                                                                    .space = space,
                                                                    .blown = blown,
                                                                    .levelDb = 12.0f,
                                                                    .note = note},
                                                                   60.0);
                    INFO("damping " << damping << " space " << space << " blown " << blown
                                    << " note " << note << ": declared " << declared
                                    << "s, still sounding at " << measured << "s");
                    CHECK(declared >= measured);
                }
            }
        }
    }
}

TEST_CASE("Rings tail declaration covers every model, damping and pitch", "[.mutable-sweep]") {
    for (int model = 0; model < 6; ++model) {
        for (const float damping : {0.0f, 0.3f, 0.5f, 0.7f, 0.8f, 0.9f}) {
            for (const int note : {24, 84}) {
                const auto [declared, measured] = ringsTail(model, damping, 12.0f, note, 60.0);
                INFO("model " << model << " damping " << damping << " note " << note
                              << ": declared " << declared << "s, still sounding at " << measured
                              << "s");
                CHECK(declared >= measured);
            }
        }
    }
}
