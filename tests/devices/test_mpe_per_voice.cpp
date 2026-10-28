#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "core/MpeWire.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaFMCompiledPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

// Per-voice MPE on the compiled synths: a member channel's bend, timbre and pressure reach
// only the voice struck on it, and its bend reads at the wire's range (#1569).

using magda::daw::audio::compiled::MagdaFMCompiledPlugin;
using magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 48000.0;
constexpr int kSettleBlocks = 8;
constexpr int kMeasureBlocks = 40;

using Settings = std::vector<std::pair<int, float>>;
using Events = std::vector<juce::MidiMessage>;

struct Rendered {
    std::vector<float> samples;

    /// Cycles between the first and last positive-going zero crossing, interpolated within
    /// the sample, which is a pure tone's frequency.
    double frequency() const {
        double first = -1.0, last = -1.0;
        int cycles = -1;
        for (size_t i = 1; i < samples.size(); ++i)
            if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) {
                const double at =
                    static_cast<double>(i - 1) + samples[i - 1] / (samples[i - 1] - samples[i]);
                if (first < 0.0)
                    first = at;
                last = at;
                ++cycles;
            }
        return cycles > 0 ? cycles * kSampleRate / (last - first) : 0.0;
    }

    float rms() const {
        double sum = 0.0;
        for (float s : samples)
            sum += static_cast<double>(s) * s;
        return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
    }
};

/// @p events in the first block, then the tail after the envelopes and smoothing settle.
template <typename Synth> Rendered render(const Settings& settings, const Events& events) {
    Synth synth;
    synth.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
    for (auto [slot, value] : settings)
        synth.getSlotParameter(slot).setValueFromHost(synth.displayValueToNativeValue(slot, value));

    Rendered rendered;
    for (int block = 0; block < kSettleBlocks + kMeasureBlocks; ++block) {
        juce::AudioBuffer<float> audio(2, kBlockSize);
        audio.clear();
        magda::test::DeviceMidiBuffer midi, out;
        if (block == 0)
            for (const auto& m : events)
                midi.events.push_back({m, 0});
        magda::test::DeviceTestBlock contextBlock(audio, kBlockSize);
        contextBlock.context.midiIn = &midi;
        contextBlock.context.midiOut = &out;
        contextBlock.context.isPlaying = true;
        synth.process(contextBlock.context);
        if (block >= kSettleBlocks)
            rendered.samples.insert(rendered.samples.end(), audio.getReadPointer(0),
                                    audio.getReadPointer(0) + kBlockSize);
    }
    return rendered;
}

juce::MidiMessage noteOn(int channel, int note) {
    return juce::MidiMessage::noteOn(channel, note, 1.0f);
}

juce::MidiMessage bend(int channel, double semitones) {
    return juce::MidiMessage::pitchWheel(channel, magda::mpe::wheelForSemitones(semitones));
}

juce::MidiMessage fullWheel(int channel) {
    return juce::MidiMessage::pitchWheel(channel, 16383);
}

juce::MidiMessage timbre(int channel, int value) {
    return juce::MidiMessage::controllerEvent(channel, magda::mpe::kTimbreController, value);
}

juce::MidiMessage pressure(int channel, int value) {
    return juce::MidiMessage::channelPressureChange(channel, value);
}

constexpr double kA3 = 220.0;

bool near(double hz, double expected) {
    return std::abs(hz - expected) <= expected * 0.02;
}

// One sine oscillator, so a zero crossing is a cycle.
const Settings kPolySine{{MagdaPolySynthCompiledPlugin::kOscBaseSlot, 0.0f}};
// Op 2 no longer modulates op 1, which leaves op 1's sine.
const Settings kFMSine{{4, 0.0f}};

template <typename Synth> void checkBend(const Settings& sine) {
    SECTION("A member channel's bend moves its own note at the wire's range") {
        const auto bent = render<Synth>(sine, {noteOn(2, 57), bend(2, 12.0)});
        CHECK(near(bent.frequency(), 2.0 * kA3));
    }
    SECTION("Another channel's bend leaves the note alone") {
        const auto other = render<Synth>(sine, {noteOn(2, 57), bend(3, 12.0)});
        CHECK(near(other.frequency(), kA3));
    }
    SECTION("A note takes the bend its channel already carries") {
        const auto preBent = render<Synth>(sine, {bend(2, -12.0), noteOn(2, 57)});
        CHECK(near(preBent.frequency(), kA3 / 2.0));
    }
    SECTION("Channel 1's wheel is the ordinary one, at Bend Range") {
        const auto wheel = render<Synth>(sine, {noteOn(1, 57), fullWheel(1)});
        CHECK(near(wheel.frequency(), kA3 * std::pow(2.0, 2.0 / 12.0)));
    }
}

template <typename Synth> void checkTimbreAndPressure() {
    const auto plain = render<Synth>({}, {noteOn(2, 57)});
    SECTION("Timbre and pressure on the note's channel change it") {
        CHECK(std::abs(render<Synth>({}, {noteOn(2, 57), timbre(2, 127)}).rms() - plain.rms()) >
              plain.rms() * 0.05f);
        CHECK(render<Synth>({}, {noteOn(2, 57), pressure(2, 127)}).rms() > plain.rms() * 1.2f);
    }
    SECTION("Timbre and pressure on another channel do not") {
        const auto other = render<Synth>({}, {noteOn(2, 57), timbre(3, 127), pressure(3, 127)});
        CHECK(std::abs(other.rms() - plain.rms()) <= plain.rms() * 0.001f);
    }
}

}  // namespace

TEST_CASE("Poly Synth bends each MPE note on its own channel", "[mpe][polysynth][1569]") {
    checkBend<MagdaPolySynthCompiledPlugin>(kPolySine);
}

TEST_CASE("fm0 bends each MPE note on its own channel", "[mpe][fm][1569]") {
    checkBend<MagdaFMCompiledPlugin>(kFMSine);
}

TEST_CASE("Poly Synth follows each note's timbre and pressure", "[mpe][polysynth][1569]") {
    checkTimbreAndPressure<MagdaPolySynthCompiledPlugin>();
}

TEST_CASE("fm0 follows each note's timbre and pressure", "[mpe][fm][1569]") {
    checkTimbreAndPressure<MagdaFMCompiledPlugin>();
}
