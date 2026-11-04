// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <magda/sdk/dsp/Fft.hpp>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "magda/daw/audio/plugins/mutable/MutableRingsPlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"

// Materia (Elements, 32 kHz) and Halo (Rings, 48 kHz) through the rate crossing they share with
// Nimbus (#2409): where a note lands, what images above the DSP's own Nyquist, and how long a
// note rings past its release against what each declares. test_mutable_clouds.cpp holds the
// same for the effect.

namespace {

namespace audio = magda::daw::audio;
using audio::MutableElementsPlugin;
using audio::MutableRingsPlugin;

template <typename Device> struct Port;

template <> struct Port<MutableElementsPlugin> {
    static constexpr double rate = 32000.0;
    /// A host rate above it, where everything between the two Nyquists is image.
    static constexpr double imagingRate = 48000.0;
};

template <> struct Port<MutableRingsPlugin> {
    static constexpr double rate = 48000.0;
    static constexpr double imagingRate = 96000.0;
};

template <typename Device> void set(Device& device, int slot, float displayValue) {
    device.setParameterValue(
        slot, magda::ParameterUtils::realToNormalized(displayValue, device.parameterInfo(slot)));
}

/// The mono sum of @p note struck at full velocity on the first sample after prepare, released at
/// @p releaseSeconds.
template <typename Device>
std::vector<float> renderNote(Device& device, double rate, int note, double releaseSeconds,
                              double totalSeconds, int blockSize = 256) {
    const int total = static_cast<int>(std::lround(totalSeconds * rate));
    const int release = static_cast<int>(std::lround(releaseSeconds * rate));
    std::vector<float> out;
    out.reserve(static_cast<size_t>(total));

    for (int pos = 0; pos < total; pos += blockSize) {
        const int n = std::min(blockSize, total - pos);
        juce::AudioBuffer<float> buffer(2, n);
        buffer.clear();

        magda::test::DeviceMidiBuffer midi(rate);
        if (pos == 0)
            midi.events.push_back({juce::MidiMessage::noteOn(1, note, juce::uint8{127}), 0});
        if (release >= pos && release < pos + n)
            midi.events.push_back(
                {juce::MidiMessage::noteOff(1, note).withTimeStamp((release - pos) / rate), 0});

        magda::test::DeviceTestBlock block(buffer, n);
        block.context.midiIn = &midi;
        block.context.isPlaying = true;
        device.process(block.context);

        for (int i = 0; i < n; ++i)
            out.push_back(0.5f * (buffer.getSample(0, i) + buffer.getSample(1, i)));
    }
    return out;
}

template <typename Device>
std::vector<float> renderDefaultNote(double rate, int note, double totalSeconds,
                                     float brightness = 0.5f) {
    Device device;
    device.prepare({.sampleRate = rate, .maximumBlockSize = 256});
    set(device, Device::kBrightness, brightness);
    return renderNote(device, rate, note, totalSeconds, totalSeconds);
}

/// Seconds from @p releaseSeconds to the last sample above -60 dBFS, rendering until 4 s have
/// stayed below it or @p maxSeconds have passed.
template <typename Device>
double tailAfter(Device& device, double rate, int note, double releaseSeconds, double maxSeconds) {
    constexpr int kBlockSize = 256;
    const auto release = static_cast<int>(std::lround(releaseSeconds * rate));
    const auto total = static_cast<int>(std::lround(maxSeconds * rate));
    const auto quiet = static_cast<int>(std::lround(4.0 * rate));
    int last = release;

    for (int pos = 0; pos < total && pos - last < quiet; pos += kBlockSize) {
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        buffer.clear();
        magda::test::DeviceMidiBuffer midi(rate);
        if (pos == 0)
            midi.events.push_back({juce::MidiMessage::noteOn(1, note, juce::uint8{127}), 0});
        if (release > 0 && release >= pos && release < pos + kBlockSize)
            midi.events.push_back(
                {juce::MidiMessage::noteOff(1, note).withTimeStamp((release - pos) / rate), 0});

        magda::test::DeviceTestBlock block(buffer, kBlockSize);
        block.context.midiIn = &midi;
        block.context.isPlaying = true;
        device.process(block.context);

        for (int i = 0; i < kBlockSize; ++i)
            if (pos + i >= release &&
                std::abs(0.5f * (buffer.getSample(0, i) + buffer.getSample(1, i))) > 0.001f)
                last = pos + i;
    }
    return static_cast<double>(last - release) / rate;
}

/// When the output first reaches a quarter of its peak, in seconds, interpolated between samples:
/// partway up the first rise, where a strike on the first sample still has samples either side.
double onsetSeconds(const std::vector<float>& x, double rate) {
    float peak = 0.0f;
    for (const float s : x)
        peak = std::max(peak, std::abs(s));
    const float threshold = peak * 0.25f;
    for (size_t i = 1; i < x.size(); ++i) {
        const float a = std::abs(x[i - 1]), b = std::abs(x[i]);
        if (a < threshold && b >= threshold)
            return (static_cast<double>(i - 1) + (threshold - a) / (b - a)) / rate;
    }
    return -1.0;
}

/// Mean power in [@p lo, @p hi] Hz, from averaged Blackman-Harris frames of @p frameSize: its
/// sidelobes sit below the floors read here. Frames that span the same seconds at two rates read
/// the same signal the same.
double bandPower(const std::vector<float>& x, double rate, double lo, double hi,
                 int frameSize = 8192) {
    magda::sdk::RealFft fft;
    REQUIRE(fft.prepare(frameSize));
    const auto size = static_cast<size_t>(frameSize);

    std::vector<float> window(size), frame(size), magnitude(size / 2 + 1);
    double windowEnergy = 0.0;
    for (size_t i = 0; i < size; ++i) {
        const double a = 2.0 * juce::MathConstants<double>::pi * static_cast<double>(i) /
                         static_cast<double>(size - 1);
        window[i] = static_cast<float>(0.35875 - 0.48829 * std::cos(a) +
                                       0.14128 * std::cos(2.0 * a) - 0.01168 * std::cos(3.0 * a));
        windowEnergy += static_cast<double>(window[i]) * window[i];
    }

    double band = 0.0;
    int frames = 0;
    for (size_t start = 0; start + size <= x.size(); start += size / 2, ++frames) {
        for (size_t i = 0; i < size; ++i)
            frame[i] = x[start + i] * window[i];
        fft.forwardMagnitude(frame.data(), magnitude.data());
        for (size_t k = 0; k <= size / 2; ++k) {
            const double hz = static_cast<double>(k) * rate / frameSize;
            if (hz >= lo && hz <= hi)
                band += static_cast<double>(magnitude[k]) * magnitude[k];
        }
    }
    return band / (windowEnergy * frameSize * std::max(frames, 1));
}

double db(double ratio) {
    return 10.0 * std::log10(ratio + 1.0e-30);
}

}  // namespace

TEMPLATE_TEST_CASE("A Mutable instrument declares the delay its rate crossing imposes",
                   "[mutable][resampler][latency]", MutableElementsPlugin, MutableRingsPlugin) {
    // Against the DSP at its own rate, where nothing resamples: a note struck on the first
    // sample lands in the first internal block at every rate, so the onsets differ by the
    // crossing alone.
    const double native = onsetSeconds(renderDefaultNote<TestType>(Port<TestType>::rate, 72, 0.1),
                                       Port<TestType>::rate);
    REQUIRE(native > 0.0);

    for (double rate : {22050.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        TestType device;
        device.prepare({.sampleRate = rate, .maximumBlockSize = 256});
        const int declared = device.latencySamples();
        const double measured =
            (onsetSeconds(renderDefaultNote<TestType>(rate, 72, 0.1), rate) - native) * rate;

        // The native onset is read off a grid that is coarser than the host's above it.
        const double tolerance = std::max(1.0, 0.5 * rate / Port<TestType>::rate);
        INFO("rate " << rate << ": declared " << declared << " samples, measured " << measured);
        CHECK(std::abs(measured - declared) <= tolerance);
    }
}

TEMPLATE_TEST_CASE("A Mutable instrument images nothing above its own Nyquist",
                   "[mutable][resampler]", MutableElementsPlugin, MutableRingsPlugin) {
    // Bright, high and struck, so the DSP fills its band to the top. Every partial the DSP is
    // flat to images past 0.5625 of its rate. The cubic left this 20 dB down for Materia and
    // 13 dB for Halo; the kernel takes both past 100.
    constexpr double rate = Port<TestType>::imagingRate;
    constexpr double internal = Port<TestType>::rate;

    for (int note : {72, 96}) {
        const auto x = renderDefaultNote<TestType>(rate, note, 0.5, 1.0f);
        const double image = db(bandPower(x, rate, 0.5625 * internal, rate / 2.0) /
                                bandPower(x, rate, 20.0, 0.44 * internal));
        INFO("note " << note << ": image band " << image << " dB re passband");
        CHECK(image < -90.0);
    }
}

TEMPLATE_TEST_CASE("A Mutable instrument's passband survives the crossing", "[mutable][resampler]",
                   MutableElementsPlugin, MutableRingsPlugin) {
    // The same internal signal at its own rate and at a multiple of it, read over frames that
    // span the same seconds: the band below the corner carries the same power through both.
    constexpr double internal = Port<TestType>::rate;
    constexpr int kFrame = 4096;
    const auto powerAt = [](int multiple) {
        const double rate = internal * multiple;
        TestType device;
        device.prepare({.sampleRate = rate, .maximumBlockSize = 256});
        auto x = renderDefaultNote<TestType>(rate, 84, 0.6, 1.0f);
        x.erase(x.begin(), x.begin() + device.latencySamples());
        return bandPower(x, rate, 20.0, 0.44 * internal, kFrame * multiple);
    };

    const double native = powerAt(1);
    REQUIRE(native > 0.0);
    for (int multiple : {2, 4}) {
        const double moved = db(powerAt(multiple) / native);
        INFO(multiple << "x the native rate: passband power moves " << moved << " dB");
        CHECK(std::abs(moved) < 0.05);
    }
}

TEST_CASE("Materia declares the tail it is actually running", "[mutable][elements][latency]") {
    // Released at 0.5 s and timed to the last sample above -60 dBFS, the criterion Nimbus' tail
    // is held to. The full domain is a sweep under [.materia-sweep].
    constexpr double rate = MutableElementsPlugin::kNativeRate;

    const auto measure = [](float damping, float space, int note, double windowSeconds) {
        MutableElementsPlugin device;
        device.prepare({.sampleRate = rate, .maximumBlockSize = 256});
        set(device, MutableElementsPlugin::kDamping, damping);
        set(device, MutableElementsPlugin::kSpace, space);
        const double declared = static_cast<double>(device.tailSamples()) / rate;
        return std::pair{declared, tailAfter(device, rate, note, 0.5, 0.5 + windowSeconds)};
    };

    SECTION("the default patch") {
        const auto [declared, measured] = measure(0.7f, 0.3f, 60, 4.0);
        INFO("declared " << declared << "s, still sounding at " << measured << "s");
        CHECK(declared >= measured);
    }

    SECTION("the space's reverb, past where the resonator has gone") {
        const auto [declared, measured] = measure(0.0f, 1.0f, 60, 7.0);
        INFO("declared " << declared << "s, still sounding at " << measured << "s");
        CHECK(declared >= measured);
        CHECK(measured > 3.0);  // past the constant this replaced
    }

    // The rest is what the declaration says, which needs no audio at all.
    SECTION("it rises with damping, brightness, space and level") {
        const auto at = &MutableElementsPlugin::tailSecondsFor;
        CHECK(at(0.9f, 0.5f, 0.3f, 0.0f) > at(0.5f, 0.5f, 0.3f, 0.0f));
        CHECK(at(0.7f, 1.0f, 0.3f, 0.0f) > at(0.7f, 0.5f, 0.3f, 0.0f));
        CHECK(at(0.5f, 0.5f, 1.0f, 0.0f) > at(0.5f, 0.5f, 0.3f, 0.0f));
        CHECK(at(0.5f, 0.5f, 0.3f, 12.0f) > at(0.5f, 0.5f, 0.3f, 0.0f));
    }

    SECTION("a bright, undamped note declares the ceiling") {
        CHECK(MutableElementsPlugin::tailSecondsFor(1.0f, 1.0f, 0.3f, 0.0f) ==
              MutableElementsPlugin::kMaxTailSeconds);
    }
}

TEST_CASE("Halo declares the tail it is actually running", "[mutable][rings][latency]") {
    // From the strum, Halo having no note-off. The full domain is under [.halo-sweep].
    constexpr double rate = MutableRingsPlugin::kNativeRate;

    const auto measure = [](int model, float damping, int note, double windowSeconds) {
        MutableRingsPlugin device;
        device.prepare({.sampleRate = rate, .maximumBlockSize = 256});
        set(device, MutableRingsPlugin::kModel, static_cast<float>(model));
        set(device, MutableRingsPlugin::kDamping, damping);
        const double declared = static_cast<double>(device.tailSamples()) / rate;
        return std::pair{declared, tailAfter(device, rate, note, 0.0, windowSeconds)};
    };

    SECTION("the default patch") {
        const auto [declared, measured] = measure(0, 0.5f, 48, 3.0);
        INFO("declared " << declared << "s, still sounding at " << measured << "s");
        CHECK(declared >= measured);
    }

    SECTION("the modal bank, damped less") {
        const auto [declared, measured] = measure(0, 0.7f, 48, 10.0);
        INFO("declared " << declared << "s, still sounding at " << measured << "s");
        CHECK(declared >= measured);
        CHECK(measured > 4.0);  // past the constant this replaced
    }

    SECTION("it rises with damping and brightness, and sustain declares the ceiling") {
        const auto at = &MutableRingsPlugin::tailSecondsFor;
        for (int model = 0; model < 6; ++model) {
            INFO("model " << model);
            CHECK(at(model, 0.7f, 0.5f, 0.0f) > at(model, 0.3f, 0.5f, 0.0f));
            CHECK(at(model, 0.7f, 1.0f, 0.0f) >= at(model, 0.7f, 0.5f, 0.0f));
            CHECK(at(model, 0.7f, 0.5f, 12.0f) > at(model, 0.7f, 0.5f, 0.0f));
            CHECK(at(model, 1.0f, 0.5f, 0.0f) == MutableRingsPlugin::kMaxTailSeconds);
        }
    }
}

//==============================================================================
// Sweeps, left out of a bare run by the leading dot: the domains behind the bounded cases above,
// hours of rendered audio between them.

TEST_CASE("Materia tail declaration covers every damping, brightness, space and note",
          "[.materia-sweep]") {
    constexpr double rate = MutableElementsPlugin::kNativeRate;

    for (float damping : {0.0f, 0.5f, 0.7f, 0.85f, 0.95f}) {
        for (float brightness : {0.0f, 0.5f, 0.75f, 1.0f}) {
            for (float space : {0.0f, 0.3f, 1.0f}) {
                // 93 and 96 are left out: they self-oscillate at every damping, which no tail
                // covers.
                for (int note : {24, 48, 60, 72, 84, 108}) {
                    MutableElementsPlugin device;
                    device.prepare({.sampleRate = rate, .maximumBlockSize = 256});
                    set(device, MutableElementsPlugin::kDamping, damping);
                    set(device, MutableElementsPlugin::kBrightness, brightness);
                    set(device, MutableElementsPlugin::kSpace, space);
                    const double declared = static_cast<double>(device.tailSamples()) / rate;
                    const double measured = tailAfter(device, rate, note, 0.5, 0.5 + 62.0);

                    INFO("damping " << damping << " brightness " << brightness << " space " << space
                                    << " note " << note << ": declared " << declared
                                    << "s, still sounding at " << measured << "s");
                    // A note that outlasts the ceiling is the case the ceiling exists for.
                    CHECK((declared >= measured ||
                           declared == MutableElementsPlugin::kMaxTailSeconds));
                }
            }
        }
    }
}

TEST_CASE("Halo tail declaration covers every model, damping, brightness and note",
          "[.halo-sweep]") {
    constexpr double rate = MutableRingsPlugin::kNativeRate;

    for (int model = 0; model < 6; ++model) {
        for (float damping : {0.0f, 0.3f, 0.5f, 0.7f, 0.8f, 0.85f, 0.9f}) {
            for (float brightness : {0.0f, 0.5f, 0.75f, 1.0f}) {
                for (int note : {24, 48, 72, 96, 120}) {
                    MutableRingsPlugin device;
                    device.prepare({.sampleRate = rate, .maximumBlockSize = 256});
                    set(device, MutableRingsPlugin::kModel, static_cast<float>(model));
                    set(device, MutableRingsPlugin::kDamping, damping);
                    set(device, MutableRingsPlugin::kBrightness, brightness);
                    const double declared = static_cast<double>(device.tailSamples()) / rate;
                    const double measured = tailAfter(device, rate, note, 0.0, 62.0);

                    INFO("model " << model << " damping " << damping << " brightness " << brightness
                                  << " note " << note << ": declared " << declared
                                  << "s, still sounding at " << measured << "s");
                    CHECK(
                        (declared >= measured || declared == MutableRingsPlugin::kMaxTailSeconds));
                }
            }
        }
    }
}
