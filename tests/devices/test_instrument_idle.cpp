#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaFMCompiledPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

// An instrument with nothing sounding and nothing arriving skips its render (#2786). The skip
// has to be exact: an event the instrument ignores forces the full render, so a timeline played
// with one on every quiet block must come out bit for bit the same as one played without.

namespace {

constexpr int kBlockSize = 128;
constexpr double kSampleRate = 44100.0;

struct Note {
    int block;
    juce::MidiMessage message;
};

/// Renders @p blocks blocks of @p device, playing @p notes, with active sensing on every block
/// that has no note when @p forceRender is set.
std::vector<float> render(magda::daw::audio::MagdaDevice& device, const std::vector<Note>& notes,
                          int blocks, bool forceRender) {
    // As the engine's render threads do; a limiter envelope decaying through denormals never
    // reaches zero.
    const juce::ScopedNoDenormals noDenormals;
    device.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});

    std::vector<float> samples;
    for (int block = 0; block < blocks; ++block) {
        magda::test::DeviceMidiBuffer midi;
        for (const auto& note : notes)
            if (note.block == block)
                midi.events.push_back({note.message, 0});
        if (forceRender && midi.events.empty())
            midi.events.push_back({juce::MidiMessage(0xfe), 0});

        juce::AudioBuffer<float> audio(2, kBlockSize);
        audio.clear();
        magda::test::DeviceMidiBuffer out;
        magda::daw::audio::DeviceProcessContext context;
        context.audio = &audio;
        context.midiIn = &midi;
        context.midiOut = &out;
        context.numSamples = kBlockSize;
        context.isPlaying = true;
        device.process(context);

        for (int channel = 0; channel < 2; ++channel)
            samples.insert(samples.end(), audio.getReadPointer(channel),
                           audio.getReadPointer(channel) + kBlockSize);
    }
    return samples;
}

/// A note, its release, a silence long enough for every voice and limiter to settle, a note.
const std::vector<Note> kTimeline = {
    {0, juce::MidiMessage::noteOn(1, 60, 0.9f)},
    {20, juce::MidiMessage::noteOff(1, 60)},
    {3200, juce::MidiMessage::noteOn(1, 64, 0.8f)},
    {3230, juce::MidiMessage::noteOff(1, 64)},
};
constexpr int kBlocks = 3400;

/// Whether the stretch before the second note went fully silent, so the idle path had a chance.
bool silentBeforeSecondNote(const std::vector<float>& samples) {
    const auto from = static_cast<std::size_t>(3100 * kBlockSize * 2);
    const auto to = static_cast<std::size_t>(3200 * kBlockSize * 2);
    for (auto i = from; i < to; ++i)
        if (samples[i] != 0.0f)
            return false;
    return true;
}

}  // namespace

TEST_CASE("An idle compiled instrument renders what the full path renders",
          "[devices][compiled][2786]") {
    using magda::daw::audio::compiled::MagdaFMCompiledPlugin;
    using magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

    SECTION("with the output stage in the dsp") {
        MagdaPolySynthCompiledPlugin skipping, rendering;
        const auto skipped = render(skipping, kTimeline, kBlocks, false);
        REQUIRE(silentBeforeSecondNote(skipped));
        CHECK(skipped == render(rendering, kTimeline, kBlocks, true));
    }

    SECTION("with the output stage in the wrapper") {
        MagdaFMCompiledPlugin skipping, rendering;
        const auto skipped = render(skipping, kTimeline, kBlocks, false);
        REQUIRE(silentBeforeSecondNote(skipped));
        CHECK(skipped == render(rendering, kTimeline, kBlocks, true));
    }
}

TEST_CASE("An idle sampler renders what the full path renders", "[devices][sampler][2786]") {
    juce::TemporaryFile wav(".wav");
    {
        juce::AudioBuffer<float> ramp(1, static_cast<int>(kSampleRate));
        for (int i = 0; i < ramp.getNumSamples(); ++i)
            ramp.setSample(0, i, 0.5f * static_cast<float>(i % 100) / 100.0f);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
            new juce::FileOutputStream(wav.getFile()), kSampleRate, 1, 16, {}, 0));
        REQUIRE(writer != nullptr);
        REQUIRE(writer->writeFromAudioSampleBuffer(ramp, 0, ramp.getNumSamples()));
    }

    magda::daw::audio::MagdaSamplerPlugin skipping, rendering;
    skipping.loadSample(wav.getFile());
    rendering.loadSample(wav.getFile());

    const auto skipped = render(skipping, kTimeline, kBlocks, false);
    REQUIRE(silentBeforeSecondNote(skipped));
    CHECK(skipped == render(rendering, kTimeline, kBlocks, true));
}
