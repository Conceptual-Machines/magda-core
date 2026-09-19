#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "TestDeviceMidiBuffer.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"

/**
 * @file test_sampler_playback.cpp
 * @brief The sampler's one cross-thread boundary (#2384).
 *
 * What the audio thread may read about the loaded sound, and nothing else: an
 * installed SamplerSound is immutable, so anything a reader needs is published
 * here or it does not cross.
 */

using magda::daw::audio::SamplerPlayback;
using magda::daw::audio::SamplerSound;

TEST_CASE("A sampler that has loaded nothing reads its fallbacks", "[devices][sampler][playback]") {
    // What the audio thread gets before any sound is installed, which is a
    // rate it can divide by rather than a zero.
    const SamplerPlayback playback;
    const auto facts = playback.read();

    CHECK(facts.sourceRate == 44100.0);
    CHECK(facts.lengthSeconds == 0.0);
    CHECK(facts.rootNote == 60);
}

TEST_CASE("A publication carries every field the audio thread reads",
          "[devices][sampler][playback]") {
    SamplerPlayback playback;
    playback.publish(SamplerPlayback::Facts{96000.0, 2.5, 48});

    const auto facts = playback.read();

    CHECK(facts.sourceRate == 96000.0);
    CHECK(facts.lengthSeconds == 2.5);
    CHECK(facts.rootNote == 48);
}

TEST_CASE("Moving the root note leaves the loaded sound's facts alone",
          "[devices][sampler][playback]") {
    // The one field a user changes without loading anything, so it publishes on
    // its own rather than round-tripping the sound.
    SamplerPlayback playback;
    playback.publish(SamplerPlayback::Facts{48000.0, 1.25, 60});

    playback.publishRootNote(72);

    const auto facts = playback.read();

    CHECK(facts.rootNote == 72);
    CHECK(facts.sourceRate == 48000.0);
    CHECK(facts.lengthSeconds == 1.25);
}

TEST_CASE("An installed sound carries nothing that changes", "[devices][sampler][playback]") {
    // The invariant the boundary rests on: every field of a SamplerSound is set
    // before the synthesiser takes it and never written again, so the audio
    // thread reading one cannot race a message-thread edit.
    static_assert(std::is_same_v<decltype(SamplerSound::sourceSampleRate), double>,
                  "a sound's rate is plain data, not something published through it");

    SamplerSound sound;
    CHECK_FALSE(sound.hasData());
    CHECK(sound.sourceSampleRate == 44100.0);
}

TEST_CASE("Short sampler notes render between their MIDI edges",
          "[devices][sampler][short-notes]") {
    using namespace magda::daw::audio;
    // Include an onset inside the block: JUCE's default batching treats the
    // first event differently from later events.
    for (const int onset : {0, 64}) {
        for (const int length : {1, 16, 31, 32, 128}) {
            CAPTURE(onset, length);
            SamplerSynth synth;
            synth.setCurrentPlaybackSampleRate(44100.0);
            auto* sound = new SamplerSound();
            sound->audioData.setSize(1, 1024);
            for (int i = 0; i < 1024; ++i)
                sound->audioData.setSample(0, i, 1.0f);
            synth.addSound(sound);
            auto* voice = new SamplerVoice();
            voice->setRootNote(60);
            voice->setADSR(0.001f, 0.1f, 1.0f, 0.1f);
            synth.addVoice(voice);
            juce::AudioBuffer<float> output(1, 512);
            output.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), onset);
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), onset + length);
            synth.renderNextBlock(output, midi, 0, 512);
            CHECK(output.getMagnitude(0, onset, length) > 0.0f);
            if (onset > 0)
                CHECK(output.getMagnitude(0, 0, onset) == 0.0f);
        }
    }
}

TEST_CASE("A note a fraction into its sample starts the sample that fraction late",
          "[devices][sampler][2741]") {
    using namespace magda::daw::audio;

    // Frame n reads back as n + 1, and the envelope is open from the first
    // sample, so what the voice read is readable off the output.
    const auto render = [](float fraction) {
        SamplerSynth synth;
        synth.setCurrentPlaybackSampleRate(44100.0);
        auto* sound = new SamplerSound();
        sound->audioData.setSize(1, 1024);
        for (int i = 0; i < 1024; ++i)
            sound->audioData.setSample(0, i, static_cast<float>(i + 1));
        synth.addSound(sound);
        auto* voice = new SamplerVoice(synth);
        voice->setRootNote(60);
        voice->setADSR(0.0f, 0.1f, 1.0f, 0.1f);
        synth.addVoice(voice);

        juce::AudioBuffer<float> output(1, 256);
        output.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 64);
        const std::array<float, 1> fractions{fraction};
        synth.beginBlock(fractions);
        synth.renderNextBlock(output, midi, 0, 256);
        return output;
    };

    const auto onTheSample = render(0.0f);
    CHECK(onTheSample.getSample(0, 63) == 0.0f);
    CHECK(onTheSample.getSample(0, 64) == 1.0f);
    CHECK(onTheSample.getSample(0, 70) == 7.0f);

    // Half a sample late: the first output sample is half way from silence to
    // the first frame, and every one after sits half way between two frames.
    const auto halfLate = render(0.5f);
    CHECK(halfLate.getSample(0, 63) == 0.0f);
    CHECK(halfLate.getSample(0, 64) == 0.5f);
    CHECK(halfLate.getSample(0, 65) == 1.5f);
    CHECK(halfLate.getSample(0, 70) == 6.5f);
}

TEST_CASE("Two note-ons of one pitch inside one sample are two notes", "[devices][sampler][2741]") {
    using namespace magda::daw::audio;
    constexpr double kRate = 44100.0;
    constexpr int kBlock = 512;

    // A steady level, so where a note starts reads off the first samples.
    juce::TemporaryFile wav(".wav");
    {
        juce::AudioBuffer<float> level(1, static_cast<int>(kRate));
        for (int i = 0; i < level.getNumSamples(); ++i)
            level.setSample(0, i, 0.25f);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(new juce::FileOutputStream(wav.getFile()), kRate, 1, 16, {}, 0));
        REQUIRE(writer != nullptr);
        REQUIRE(writer->writeFromAudioSampleBuffer(level, 0, level.getNumSamples()));
    }

    // Note-ons of middle C at each stamp, in samples, and what they render.
    const auto render = [&wav](std::vector<double> stamps) {
        MagdaSamplerPlugin sampler;
        sampler.loadSample(wav.getFile());
        sampler.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});

        magda::test::DeviceMidiBuffer midi;
        for (const auto stamp : stamps)
            midi.events.push_back(
                {juce::MidiMessage::noteOn(1, 60, 0.9f).withTimeStamp(stamp / kRate), 0});

        juce::AudioBuffer<float> audio(2, kBlock);
        audio.clear();
        magda::test::DeviceMidiBuffer out;
        DeviceProcessContext context;
        context.audio = &audio;
        context.midiIn = &midi;
        context.midiOut = &out;
        context.numSamples = kBlock;
        context.isPlaying = true;
        sampler.process(context);

        const auto* left = audio.getReadPointer(0);
        return std::vector<float>(left, left + kBlock);
    };

    REQUIRE(render({100.2}) != render({100.8}));

    // The second retriggers the pitch before the first has sounded, so what is
    // heard is the second, at its own fraction, rather than the first.
    CHECK(render({100.2, 100.8}) == render({100.8}));

    // The same instant twice is one note routed two ways.
    CHECK(render({100.2, 100.2}) == render({100.2}));
}
