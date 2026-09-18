#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"

// All-notes-off reaches an instrument beside the events rather than among
// them, because a juce::MidiBuffer has nowhere to put it (#2418). The
// instruments walked the events alone, so every panic the host raised was
// dropped and whatever was sounding stayed sounding: a playhead jump, a mute,
// or the routing change that takes a source track's input away (#2722).

using magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 44100.0;

/// Renders one block and answers with the loudest sample in it.
float peakOf(MagdaPolySynthCompiledPlugin& synth, magda::test::DeviceMidiBuffer& midi) {
    juce::AudioBuffer<float> audio(2, kBlockSize);
    audio.clear();

    magda::test::DeviceMidiBuffer out;
    magda::daw::audio::DeviceProcessContext context;
    context.audio = &audio;
    context.midiIn = &midi;
    context.midiOut = &out;
    context.numSamples = kBlockSize;
    context.isPlaying = true;

    synth.process(context);
    return audio.getMagnitude(0, kBlockSize);
}

/// A synth with a note held down, and the peak that note is sounding at.
float soundOn(MagdaPolySynthCompiledPlugin& synth) {
    synth.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});

    magda::test::DeviceMidiBuffer midi;
    midi.events.push_back({juce::MidiMessage::noteOn(1, 60, 0.9f), 0});
    peakOf(synth, midi);

    // Let the envelope open before anything is measured against it.
    magda::test::DeviceMidiBuffer quiet;
    float peak = 0.0f;
    for (int block = 0; block < 8; ++block)
        peak = peakOf(synth, quiet);
    return peak;
}

/// What the synth settles to over enough blocks for a release to finish.
float decayOver(MagdaPolySynthCompiledPlugin& synth, int blocks) {
    magda::test::DeviceMidiBuffer quiet;
    float peak = 0.0f;
    for (int block = 0; block < blocks; ++block)
        peak = peakOf(synth, quiet);
    return peak;
}

}  // namespace

TEST_CASE("A compiled instrument lets go when the host raises all-notes-off",
          "[devices][compiled][2418]") {
    MagdaPolySynthCompiledPlugin synth;

    const auto sounding = soundOn(synth);
    REQUIRE(sounding > 0.0f);

    // Still held: nothing has told it to stop, so it keeps sounding.
    REQUIRE(decayOver(synth, 8) > 0.0f);

    // The panic, beside the events and with no note-off among them.
    magda::test::DeviceMidiBuffer panic;
    panic.allNotesOff = true;
    peakOf(synth, panic);

    CHECK(decayOver(synth, 200) < sounding * 0.01f);
}

TEST_CASE("A compiled instrument lets go for controller 123 as well", "[devices][compiled][2418]") {
    // The same message in band, which the Faust poly engine already answers in
    // ctrlChange. Pinned rather than fixed: the two routes to a panic have to
    // end the same way, and only the out-of-band one was broken.
    MagdaPolySynthCompiledPlugin synth;

    const auto sounding = soundOn(synth);
    REQUIRE(sounding > 0.0f);

    magda::test::DeviceMidiBuffer panic;
    panic.events.push_back({juce::MidiMessage::allNotesOff(1), 0});
    peakOf(synth, panic);

    CHECK(decayOver(synth, 200) < sounding * 0.01f);
}

TEST_CASE("A panic with nothing sounding leaves the instrument as it found it",
          "[devices][compiled][2722]") {
    // The host raises one on every playhead jump and every launch, mostly with
    // nothing held. Releasing a free voice marks it as releasing, which changes
    // which voice the next note-on is handed and so the phase it starts at.
    const auto render = [](bool panic) {
        MagdaPolySynthCompiledPlugin synth;
        synth.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});

        std::vector<float> samples;
        for (int block = 0; block < 400; ++block) {
            magda::test::DeviceMidiBuffer midi;
            if (block == 0)
                midi.events.push_back({juce::MidiMessage::noteOn(1, 60, 0.9f), 0});
            if (block == 8)
                midi.events.push_back({juce::MidiMessage::noteOff(1, 60), 0});
            if (block == 360)
                midi.events.push_back({juce::MidiMessage::noteOn(1, 64, 0.9f), 0});
            midi.allNotesOff = panic && block == 350;

            juce::AudioBuffer<float> audio(2, kBlockSize);
            audio.clear();
            magda::test::DeviceMidiBuffer out;
            magda::daw::audio::DeviceProcessContext context;
            context.audio = &audio;
            context.midiIn = &midi;
            context.midiOut = &out;
            context.numSamples = kBlockSize;
            context.isPlaying = true;
            synth.process(context);

            samples.insert(samples.end(), audio.getReadPointer(0),
                           audio.getReadPointer(0) + kBlockSize);
        }
        return samples;
    };

    CHECK(render(true) == render(false));
}

TEST_CASE("Materia closes its gate on the host's panic", "[devices][mutable][2722]") {
    // Bowed rather than struck, so a held gate keeps feeding the resonator and
    // only the panic can stop it.
    const auto finalPeak = [](bool panic) {
        magda::daw::audio::MutableElementsPlugin elements;
        elements.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
        const auto set = [&elements](int slot, float value) {
            elements.setParameterValue(
                slot, magda::ParameterUtils::realToNormalized(value, elements.parameterInfo(slot)));
        };
        set(magda::daw::audio::MutableElementsPlugin::kBow, 1.0f);
        set(magda::daw::audio::MutableElementsPlugin::kStrike, 0.0f);
        // No space and the shortest decay, so a released resonator dies inside the run.
        set(magda::daw::audio::MutableElementsPlugin::kSpace, 0.0f);
        set(magda::daw::audio::MutableElementsPlugin::kDamping, 0.0f);

        float peak = 0.0f;
        for (int block = 0; block < 400; ++block) {
            magda::test::DeviceMidiBuffer midi;
            if (block == 0)
                midi.events.push_back({juce::MidiMessage::noteOn(1, 60, 0.9f), 0});
            midi.allNotesOff = panic && block == 150;

            juce::AudioBuffer<float> audio(2, kBlockSize);
            audio.clear();
            magda::test::DeviceMidiBuffer out;
            magda::daw::audio::DeviceProcessContext context;
            context.audio = &audio;
            context.midiIn = &midi;
            context.midiOut = &out;
            context.numSamples = kBlockSize;
            context.isPlaying = true;
            elements.process(context);
            peak = audio.getMagnitude(0, kBlockSize);
        }
        return peak;
    };

    const auto held = finalPeak(false);
    REQUIRE(held > 0.0f);
    CHECK(finalPeak(true) < held * 0.05f);
}

TEST_CASE("The sampler lets go on the host's panic", "[devices][sampler][2722]") {
    // Two seconds of a steady level, so a held note is still sounding when the
    // measurement is taken and only a release can end it early.
    juce::TemporaryFile wav(".wav");
    {
        juce::AudioBuffer<float> level(1, 2 * static_cast<int>(kSampleRate));
        for (int i = 0; i < level.getNumSamples(); ++i)
            level.setSample(0, i, 0.5f);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
            new juce::FileOutputStream(wav.getFile()), kSampleRate, 1, 16, {}, 0));
        REQUIRE(writer != nullptr);
        REQUIRE(writer->writeFromAudioSampleBuffer(level, 0, level.getNumSamples()));
    }

    const auto finalPeak = [&wav](bool panic) {
        magda::daw::audio::MagdaSamplerPlugin sampler;
        sampler.loadSample(wav.getFile());
        sampler.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});

        float peak = 0.0f;
        for (int block = 0; block < 150; ++block) {
            magda::test::DeviceMidiBuffer midi;
            if (block == 0)
                midi.events.push_back({juce::MidiMessage::noteOn(1, 60, 0.9f), 0});
            midi.allNotesOff = panic && block == 50;

            juce::AudioBuffer<float> audio(2, kBlockSize);
            audio.clear();
            magda::test::DeviceMidiBuffer out;
            magda::daw::audio::DeviceProcessContext context;
            context.audio = &audio;
            context.midiIn = &midi;
            context.midiOut = &out;
            context.numSamples = kBlockSize;
            context.isPlaying = true;
            sampler.process(context);
            peak = audio.getMagnitude(0, kBlockSize);
        }
        return peak;
    };

    const auto held = finalPeak(false);
    REQUIRE(held > 0.0f);
    CHECK(finalPeak(true) < held * 0.01f);
}
