#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "DeviceTestBlock.hpp"
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
        magda::test::DeviceTestBlock block(audio);
        auto& context = block.context;
        context.midiIn = &midi;
        context.midiOut = &out;
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

TEST_CASE("A held note loops between the loop markers past the sample's end",
          "[devices][sampler][loop]") {
    using namespace magda::daw::audio;
    constexpr double kRate = 44100.0;
    SamplerSynth synth;
    synth.setCurrentPlaybackSampleRate(kRate);
    auto* sound = new SamplerSound();
    sound->audioData.setSize(1, 1024);
    for (int i = 0; i < 1024; ++i)
        sound->audioData.setSample(0, i, static_cast<float>(i + 1));
    synth.addSound(sound);
    auto* voice = new SamplerVoice(synth);
    voice->setRootNote(60);
    voice->setADSR(0.0f, 0.1f, 1.0f, 0.1f);
    voice->setPlaybackRegion(0.0, 1024 / kRate, true, 256 / kRate, 512 / kRate, kRate);
    synth.addVoice(voice);

    juce::AudioBuffer<float> output(1, 4096);
    output.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    synth.renderNextBlock(output, midi, 0, 4096);

    // Frame n reads back as n + 1: past the loop end it wraps to the loop start.
    CHECK(output.getSample(0, 511) == 512.0f);
    CHECK(output.getSample(0, 512) == 257.0f);
    for (int i = 1024; i < 4096; ++i) {
        const float value = output.getSample(0, i);
        REQUIRE(value >= 257.0f);
        REQUIRE(value <= 512.0f);
    }
}

TEST_CASE("The pitch envelope bends a note by its amount", "[devices][sampler][3096]") {
    using namespace magda::daw::audio;
    constexpr double kRate = 44100.0;
    // A held envelope at full level: the bend is the amount, every frame.
    for (const auto [semitones, step] : {std::pair{0.0f, 1}, std::pair{12.0f, 2}}) {
        CAPTURE(semitones);
        SamplerSynth synth;
        synth.setCurrentPlaybackSampleRate(kRate);
        auto* sound = new SamplerSound();
        sound->audioData.setSize(1, 4096);
        for (int i = 0; i < 4096; ++i)
            sound->audioData.setSample(0, i, static_cast<float>(i + 1));
        synth.addSound(sound);
        auto* voice = new SamplerVoice(synth);
        voice->setRootNote(60);
        voice->setADSR(0.0f, 0.1f, 1.0f, 0.1f);
        voice->setPitchEnvelope(0.0f, 0.1f, 1.0f, 0.1f, semitones);
        synth.addVoice(voice);

        juce::AudioBuffer<float> output(1, 512);
        output.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        synth.renderNextBlock(output, midi, 0, 512);

        for (int i : {0, 1, 100, 511})
            CHECK(output.getSample(0, i) == static_cast<float>(i * step + 1));
    }
}

TEST_CASE("A pitch envelope that decays to nothing returns the note to its pitch",
          "[devices][sampler][3096]") {
    using namespace magda::daw::audio;
    constexpr double kRate = 44100.0;
    SamplerSynth synth;
    synth.setCurrentPlaybackSampleRate(kRate);
    auto* sound = new SamplerSound();
    sound->audioData.setSize(1, 1 << 16);
    for (int i = 0; i < sound->audioData.getNumSamples(); ++i)
        sound->audioData.setSample(0, i, static_cast<float>(i + 1));
    synth.addSound(sound);
    auto* voice = new SamplerVoice(synth);
    voice->setRootNote(60);
    voice->setADSR(0.0f, 0.1f, 1.0f, 0.1f);
    voice->setPitchEnvelope(0.0f, 0.01f, 0.0f, 0.1f, -24.0f);
    synth.addVoice(voice);

    juce::AudioBuffer<float> output(1, 4096);
    output.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    synth.renderNextBlock(output, midi, 0, 4096);

    // Two octaves down at the start reads a quarter frame per sample, then unbent after the decay.
    CHECK(output.getSample(0, 1) - output.getSample(0, 0) == Catch::Approx(0.25f).margin(0.01f));
    CHECK(output.getSample(0, 4000) - output.getSample(0, 3999) == Catch::Approx(1.0f));
}

namespace {

/// One voice holding @p note over a sine at @p hz, through @p filter, for @p frames.
juce::AudioBuffer<float> renderFilteredSine(
    const magda::daw::audio::SamplerVoice::FilterSettings& filter, float hz, int frames,
    int note = 60, float velocity = 1.0f, float envDecay = 1.0f) {
    using namespace magda::daw::audio;
    constexpr double kRate = 48000.0;
    SamplerSynth synth;
    synth.setCurrentPlaybackSampleRate(kRate);
    auto* sound = new SamplerSound();
    sound->sourceSampleRate = kRate;
    sound->audioData.setSize(1, frames * 4);
    for (int i = 0; i < sound->audioData.getNumSamples(); ++i)
        sound->audioData.setSample(0, i,
                                   std::sin(2.0f * juce::MathConstants<float>::pi * hz *
                                            static_cast<float>(i) / static_cast<float>(kRate)));
    synth.addSound(sound);
    auto* voice = new SamplerVoice(synth);
    voice->setRootNote(note);
    voice->setADSR(0.0f, 0.1f, 1.0f, 0.1f);
    voice->setFilterEnvelope(0.0f, envDecay, 0.0f, 0.1f);
    voice->setFilter(filter);
    voice->prepareFilter(kRate);
    synth.addVoice(voice);

    juce::AudioBuffer<float> output(2, frames);
    output.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
    synth.renderNextBlock(output, midi, 0, frames);
    return output;
}

}  // namespace

TEST_CASE("Each low-pass filter type turns down a tone far above its cutoff",
          "[devices][sampler][3097]") {
    using Settings = magda::daw::audio::SamplerVoice::FilterSettings;
    const auto open = renderFilteredSine(Settings{}, 8000.0f, 9600);
    const float openLevel = open.getRMSLevel(0, 4800, 4800);
    REQUIRE(openLevel > 0.5f);

    // Every type but the comb, whose peaks run up the harmonics.
    for (int family = 0; family < static_cast<int>(magda::devices::faust::FilterFamily::Comb);
         ++family) {
        CAPTURE(family);
        Settings filter;
        filter.family = family;
        filter.cutoffHz = 200.0f;
        const auto filtered = renderFilteredSine(filter, 8000.0f, 9600);
        CHECK(filtered.getRMSLevel(0, 4800, 4800) < openLevel * 0.05f);
        // A mono sample still reaches both sides.
        CHECK(filtered.getRMSLevel(1, 4800, 4800) == filtered.getRMSLevel(0, 4800, 4800));
    }
}

TEST_CASE("A mode the filter type lacks plays as low-pass", "[devices][sampler][3097]") {
    magda::daw::audio::SamplerVoice::FilterSettings filter;
    filter.family = static_cast<int>(magda::devices::faust::FilterFamily::Ladder);
    filter.mode = 3;
    filter.cutoffHz = 5000.0f;
    const auto output = renderFilteredSine(filter, 200.0f, 9600);
    CHECK(output.getRMSLevel(0, 4800, 4800) > 0.5f);
}

TEST_CASE("The filter envelope opens the filter and closes it again", "[devices][sampler][3097]") {
    magda::daw::audio::SamplerVoice::FilterSettings filter;
    filter.family = static_cast<int>(magda::devices::faust::FilterFamily::SVF);
    filter.cutoffHz = 100.0f;
    filter.envelopeSemitones = 72.0f;
    const auto output = renderFilteredSine(filter, 4000.0f, 48000, 60, 1.0f, 0.05f);
    // Six octaves up at the peak passes 4 kHz; decayed to nothing, 100 Hz does not.
    CHECK(output.getRMSLevel(0, 0, 480) > 0.3f);
    CHECK(output.getRMSLevel(0, 24000, 4800) < 0.01f);
}

TEST_CASE("Key tracking and velocity move the cutoff", "[devices][sampler][3097]") {
    using Settings = magda::daw::audio::SamplerVoice::FilterSettings;
    Settings filter;
    filter.family = static_cast<int>(magda::devices::faust::FilterFamily::SVF);
    filter.cutoffHz = 500.0f;

    const auto level = [](const juce::AudioBuffer<float>& output) {
        return output.getRMSLevel(0, 4800, 4800);
    };
    const float untracked = level(renderFilteredSine(filter, 4000.0f, 9600, 84));
    filter.keyTrack = 1.0f;
    // Two octaves above middle C opens a fully tracking filter two octaves.
    const float tracked = level(renderFilteredSine(filter, 4000.0f, 9600, 84));
    CHECK(tracked > untracked * 4.0f);

    filter.keyTrack = 0.0f;
    filter.cutoffHz = 4000.0f;
    filter.velocity = 1.0f;
    const float loud = level(renderFilteredSine(filter, 2000.0f, 9600, 60, 1.0f));
    const float soft = level(renderFilteredSine(filter, 2000.0f, 9600, 60, 0.25f));
    CHECK(soft < loud * 0.2f);
}

TEST_CASE("A new sampler has its filter off", "[devices][sampler][3097]") {
    magda::daw::audio::MagdaSamplerPlugin sampler;
    CHECK(sampler.displayValue(magda::daw::audio::MagdaSamplerPlugin::kFilterType) == 0.0f);
}

TEST_CASE("More poles turn a tone above the cutoff down further", "[devices][sampler][3097]") {
    magda::daw::audio::SamplerVoice::FilterSettings filter;
    filter.family = static_cast<int>(magda::devices::faust::FilterFamily::SVF);
    filter.cutoffHz = 500.0f;
    float previous = 1.0f;
    for (int poles = 0; poles < 3; ++poles) {
        CAPTURE(poles);
        filter.poleIndex = poles;
        const float level = renderFilteredSine(filter, 2000.0f, 9600).getRMSLevel(0, 4800, 4800);
        // Two octaves up: about 24, 48 and 72 dB down.
        CHECK(level < previous * 0.2f);
        previous = level;
    }
}

TEST_CASE("Notch+ cuts its cutoff and lifts the peaks either side", "[devices][sampler][3097]") {
    magda::daw::audio::SamplerVoice::FilterSettings filter;
    filter.family = static_cast<int>(magda::devices::faust::FilterFamily::SVF);
    filter.mode = magda::devices::faust::kNotchPlusMode;
    filter.cutoffHz = 1000.0f;
    filter.resonance = 1.0f;
    const auto level = [&](float hz) {
        return renderFilteredSine(filter, hz, 9600).getRMSLevel(0, 4800, 4800);
    };
    const float open = 1.0f / std::sqrt(2.0f);
    CHECK(level(1000.0f) < open * 0.05f);
    CHECK(level(1414.2f) > open * 1.5f);
    CHECK(level(707.1f) > open * 1.5f);
}

TEST_CASE("The comb passes the cutoff's harmonics and cuts between them",
          "[devices][sampler][3097]") {
    magda::daw::audio::SamplerVoice::FilterSettings filter;
    filter.family = static_cast<int>(magda::devices::faust::FilterFamily::Comb);
    filter.cutoffHz = 1000.0f;
    const auto level = [&](float hz) {
        return renderFilteredSine(filter, hz, 9600).getRMSLevel(0, 4800, 4800);
    };
    CHECK(level(2000.0f) > 0.5f);
    CHECK(level(1500.0f) < 0.02f);

    // Minus moves the peaks onto the odd half-harmonics.
    filter.mode = 1;
    CHECK(level(1500.0f) > 0.5f);
    CHECK(level(2000.0f) < 0.02f);
}
