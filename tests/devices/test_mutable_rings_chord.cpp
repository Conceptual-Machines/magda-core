#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/mutable/MutableRingsPlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"

// Rings has no note-off and no per-voice gate: a note-on retunes the resonator
// and fires a strum, and the part rotates voices so earlier notes keep ringing.
// The strum is consumed once per internal 24-sample block, so a chord, which
// arrives as note-ons at one timestamp, only ever reached the resonator as its
// last note (#2364).

namespace {

namespace audio = magda::daw::audio;
using audio::MutableRingsPlugin;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 256;
constexpr int kNumBlocks = 16;

struct Note {
    int number = 60;
    double timeStamp = 0.0;
};

struct RingsRig {
    MutableRingsPlugin device;

    RingsRig() {
        device.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
        set(MutableRingsPlugin::kPolyphony, 2.0f);  // index 2 -> four voices
    }

    void set(int slot, float displayValue) {
        device.setParameterValue(slot, magda::ParameterUtils::realToNormalized(
                                           displayValue, device.parameterInfo(slot)));
    }

    /// Renders the notes struck in the first block, then lets them ring.
    std::vector<float> render(std::initializer_list<Note> notes) {
        std::vector<float> out;
        out.reserve(static_cast<size_t>(kBlockSize * kNumBlocks));

        for (int block = 0; block < kNumBlocks; ++block) {
            juce::AudioBuffer<float> audio(2, kBlockSize);
            audio.clear();

            magda::test::DeviceMidiBuffer midi;
            if (block == 0)
                for (const auto& note : notes)
                    midi.events.push_back(
                        {juce::MidiMessage::noteOn(1, note.number, juce::uint8{100})
                             .withTimeStamp(note.timeStamp),
                         0});

            audio::DeviceProcessContext context;
            context.audio = &audio;
            context.midiIn = &midi;
            context.numSamples = kBlockSize;
            context.isPlaying = true;
            device.process(context);

            const auto* samples = audio.getReadPointer(0);
            out.insert(out.end(), samples, samples + kBlockSize);
        }

        return out;
    }
};

double energy(const std::vector<float>& samples) {
    double sum = 0.0;
    for (const auto sample : samples)
        sum += static_cast<double>(sample) * sample;
    return sum;
}

bool identical(const std::vector<float>& a, const std::vector<float>& b) {
    return a == b;
}

}  // namespace

TEST_CASE("Rings strums every note of a chord struck at one timestamp", "[rings][mutable]") {
    const auto chord = RingsRig{}.render({{60, 0.0}, {64, 0.0}, {67, 0.0}});
    const auto lastAlone = RingsRig{}.render({{67, 0.0}});
    const auto firstAlone = RingsRig{}.render({{60, 0.0}});

    REQUIRE(energy(chord) > 0.0);
    CHECK_FALSE(identical(chord, lastAlone));
    CHECK_FALSE(identical(chord, firstAlone));

    // Three strings ringing carry more than one of them does.
    CHECK(energy(chord) > energy(lastAlone));
    CHECK(energy(chord) > energy(firstAlone));
}

TEST_CASE("Rings strums notes closer together than one internal block", "[rings][mutable]") {
    // An internal block is 24 samples at 48 kHz, so three notes inside ten host
    // samples all land in one, which used to collapse them the same way.
    const auto rolled =
        RingsRig{}.render({{60, 0.0}, {64, 4.0 / kSampleRate}, {67, 8.0 / kSampleRate}});
    const auto lastAlone = RingsRig{}.render({{67, 8.0 / kSampleRate}});

    REQUIRE(energy(rolled) > 0.0);
    CHECK_FALSE(identical(rolled, lastAlone));
    CHECK(energy(rolled) > energy(lastAlone));
}

TEST_CASE("Rings still sounds a chord larger than its polyphony", "[rings][mutable]") {
    // More notes than voices is not an error: the part rotates, so the last
    // four win the strings and nothing is dropped on the way in.
    const auto wide =
        RingsRig{}.render({{55, 0.0}, {59, 0.0}, {62, 0.0}, {65, 0.0}, {69, 0.0}, {72, 0.0}});
    CHECK(energy(wide) > 0.0);
}
