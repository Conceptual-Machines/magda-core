#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "core/DeviceState.hpp"
#include "core/ParameterUtils.hpp"
#include "magda/daw/audio/plugins/MagdaConvolutionPlugin.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"

// The native convolution device (#1980), driven as the native engine drives it
// (#2556): the impulse response survives a save and reload, the convolution
// convolves, and the parameter ranges normalise as they always have.

namespace {

namespace audio = magda::daw::audio;
namespace adapter = magda::daw::audio::engine_adapter;
namespace ds = magda::device_state;
using Convolution = audio::MagdaConvolutionPlugin;
using Catch::Approx;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 256;
constexpr int kDelaySamples = 8;

/// A mono IR that is silent except for a spike `kDelaySamples` in.
juce::MemoryBlock delayImpulseResponse() {
    juce::AudioBuffer<float> ir(1, kDelaySamples + 1);
    ir.clear();
    ir.setSample(0, kDelaySamples, 1.0f);
    juce::MemoryBlock data;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::MemoryOutputStream>(data, false);
    if (auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                         .withSampleRate(kSampleRate)
                                                         .withNumChannels(1)
                                                         .withBitsPerSample(24)))
        writer->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
    return data;
}

void prepare(audio::MagdaDevice& device, double sampleRate = kSampleRate) {
    device.prepare({.sampleRate = sampleRate, .maximumBlockSize = kBlockSize});
}

void process(audio::MagdaDevice& device, juce::AudioBuffer<float>& buffer) {
    audio::DeviceProcessContext context;
    context.audio = &buffer;
    context.numSamples = buffer.getNumSamples();
    device.process(context);
}

/// One block of a unit impulse through the device.
juce::AudioBuffer<float> renderImpulse(audio::MagdaDevice& device) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);
    process(device, buffer);
    return buffer;
}

int peakIndex(const juce::AudioBuffer<float>& buffer) {
    int index = 0;
    float peak = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (const auto magnitude = std::abs(buffer.getSample(0, i)); magnitude > peak) {
            peak = magnitude;
            index = i;
        }
    return index;
}

/// RMS of a steady 300 Hz tone, which divides both 44.1 and 48 kHz evenly (#2360).
float steadyStateSineRms(audio::MagdaDevice& device, double sampleRate) {
    constexpr double kToneHz = 300.0;
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    double phase = 0.0;
    const auto step = juce::MathConstants<double>::twoPi * kToneHz / sampleRate;
    const auto renderBlock = [&] {
        for (int i = 0; i < kBlockSize; ++i) {
            const auto sample = static_cast<float>(std::sin(phase));
            buffer.setSample(0, i, sample);
            buffer.setSample(1, i, sample);
            phase += step;
        }
        process(device, buffer);
    };
    for (int block = 0; block < 4; ++block)
        renderBlock();

    const int measureSamples = 20 * juce::roundToInt(sampleRate / kToneHz);
    double sumSquares = 0.0;
    for (int measured = 0; measured < measureSamples;) {
        renderBlock();
        const int take = std::min(kBlockSize, measureSamples - measured);
        for (int i = 0; i < take; ++i)
            sumSquares += buffer.getSample(0, i) * buffer.getSample(0, i);
        measured += take;
    }
    return static_cast<float>(std::sqrt(sumSquares / measureSamples));
}

}  // namespace

TEST_CASE("Convolution parameter ranges normalise exactly as the retired device's did",
          "[devices][convolution][2556]") {
    Convolution convolution;
    CHECK(convolution.parameterCount() == static_cast<int>(Convolution::kNumParams));

    // The retired device stored a MIDI note over a linear 10 Hz..20 kHz range. The
    // native one stores Hz, but a normalised position still lands on the same
    // frequency, or saved automation and macro links would shift on migration.
    const auto noteToFrequency = [](float note) {
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    };
    const float noteMin = 69.0f + 12.0f * std::log2(10.0f / 440.0f);
    const float noteMax = 69.0f + 12.0f * std::log2(20000.0f / 440.0f);

    const auto lowCut = convolution.parameterInfo(Convolution::kLowCut);
    for (const float normalised : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto retired = noteToFrequency(noteMin + normalised * (noteMax - noteMin));
        CHECK(magda::ParameterUtils::normalizedToReal(normalised, lowCut) ==
              Approx(retired).epsilon(0.001));
    }
    CHECK(magda::ParameterUtils::realToNormalized(110.0f, lowCut) ==
          Approx((45.0f - noteMin) / (noteMax - noteMin)).margin(0.001));

    const auto gain = convolution.parameterInfo(Convolution::kGain);
    CHECK(gain.minValue == Approx(-12.0f));
    CHECK(gain.maxValue == Approx(6.0f));
    CHECK(magda::ParameterUtils::normalizedToReal(0.5f, gain) == Approx(0.0f).margin(0.01));

    const auto q = convolution.parameterInfo(Convolution::kFilterQ);
    CHECK(q.minValue == Approx(0.1f));
    CHECK(q.maxValue == Approx(14.0f));
}

TEST_CASE("A loaded impulse response convolves the signal", "[devices][convolution][2556]") {
    const auto ir = delayImpulseResponse();
    Convolution convolution;
    REQUIRE(convolution.loadImpulseResponse(ir.getData(), ir.getSize()));
    convolution.setParameterValue(Convolution::kMix, 1.0f);
    prepare(convolution);

    const auto rendered = renderImpulse(convolution);
    CHECK(peakIndex(rendered) == kDelaySamples);
    CHECK(rendered.getMagnitude(0, kBlockSize) > 0.1f);
    CHECK(convolution.properties().tailLengthSeconds > 0.0);

    // A control still moving takes the sub-blocked path; the convolution survives it.
    convolution.setParameterValue(Convolution::kGain, 6.0f);
    const auto smoothed = renderImpulse(convolution);
    CHECK(peakIndex(smoothed) == kDelaySamples);
    CHECK(smoothed.getMagnitude(0, kBlockSize) > rendered.getMagnitude(0, kBlockSize));
}

TEST_CASE("Trim silence is not a parameter but still reaches the convolution",
          "[devices][convolution][2556]") {
    const auto ir = delayImpulseResponse();
    Convolution convolution;
    convolution.setTrimSilence(true);
    REQUIRE(convolution.loadImpulseResponse(ir.getData(), ir.getSize()));
    convolution.setParameterValue(Convolution::kMix, 1.0f);
    prepare(convolution);

    // With the leading silence gone, the same IR stops delaying anything.
    CHECK(peakIndex(renderImpulse(convolution)) == 0);
}

TEST_CASE("A saved impulse response loads from the model's document on the native engine",
          "[devices][convolution][native][2556]") {
    const auto ir = delayImpulseResponse();
    juce::MemoryBlock encoded;
    REQUIRE(Convolution::encodeImpulseResponse(ir.getData(), ir.getSize(), encoded));

    // What LoadImpulseResponseCommand writes into the model, and a project saves.
    ds::Doc doc;
    doc.deviceType = Convolution::xmlTypeName;
    doc.root.props.set(Convolution::StateIDs::irFileData, juce::var(encoded));
    doc.root.props.set(Convolution::StateIDs::name, "Test Space");
    doc.root.props.set(Convolution::StateIDs::normalise, false);
    magda::DeviceInfo model;
    model.pluginId = Convolution::xmlTypeName;
    model.pluginState = ds::encode(doc);

    auto engineDevice = adapter::createEngineDevice(model);
    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(engineDevice.get());
    REQUIRE(hosted != nullptr);
    auto* restored = dynamic_cast<Convolution*>(&hosted->device());
    REQUIRE(restored != nullptr);

    CHECK(restored->irName() == "Test Space");
    CHECK_FALSE(restored->normalise());
    CHECK_FALSE(restored->trimSilence());

    restored->setParameterValue(Convolution::kMix, 1.0f);
    prepare(*restored);
    CHECK(peakIndex(renderImpulse(*restored)) == kDelaySamples);
}

TEST_CASE("Restoring a document without an IR unloads the device", "[devices][convolution][2556]") {
    // Undoing the first IR load restores a document with no IR: the device must stop
    // convolving, drop the name, and stop reporting the blob (#2317 review).
    const auto ir = delayImpulseResponse();
    Convolution convolution;
    REQUIRE(convolution.loadImpulseResponse(ir.getData(), ir.getSize()));
    convolution.setIrName("Doomed");

    convolution.restoreState(juce::ValueTree("PLUGIN"));
    CHECK(convolution.irName().isEmpty());
    CHECK(convolution.properties().tailLengthSeconds == Approx(0.0).margin(1.0e-9));

    juce::ValueTree flushed("PLUGIN");
    convolution.flushState(flushed);
    CHECK_FALSE(flushed.hasProperty(Convolution::StateIDs::irFileData));

    convolution.setParameterValue(Convolution::kMix, 1.0f);
    prepare(convolution);
    CHECK(peakIndex(renderImpulse(convolution)) == 0);
}

TEST_CASE("A fully dry convolution passes the signal through", "[devices][convolution][2556]") {
    Convolution convolution;
    convolution.setParameterValue(Convolution::kMix, 0.0f);
    prepare(convolution);
    const auto rendered = renderImpulse(convolution);
    CHECK(rendered.getSample(0, 0) == Approx(1.0f).margin(0.001));
    CHECK(peakIndex(rendered) == 0);
}

TEST_CASE("No IR is a pass-through at any session rate", "[devices][convolution][2556]") {
    for (const double rate : {kSampleRate, 48000.0}) {
        INFO("sample rate " << rate);
        Convolution convolution;
        convolution.setParameterValue(Convolution::kMix, 1.0f);
        prepare(convolution, rate);
        CHECK(steadyStateSineRms(convolution, rate) == Approx(std::sqrt(0.5f)).margin(0.005));
    }
}
