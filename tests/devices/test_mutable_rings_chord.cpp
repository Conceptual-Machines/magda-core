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

    explicit RingsRig(float polyphonyIndex = 2.0f) {
        device.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
        set(MutableRingsPlugin::kPolyphony, polyphonyIndex);
    }

    void set(int slot, float displayValue) {
        device.setParameterValue(slot, magda::ParameterUtils::realToNormalized(
                                           displayValue, device.parameterInfo(slot)));
    }

    /// Renders the notes struck in the first block, then lets them ring.
    std::vector<float> render(std::initializer_list<Note> notes) {
        std::vector<float> out;
        out.reserve(static_cast<size_t>(kBlockSize) * kNumBlocks);

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

            // Both channels: Rings splits partials between its main and aux
            // outputs by Structure, and either can sit at exact silence for a
            // given patch, as the add-not-replace test already records.
            const auto* left = audio.getReadPointer(0);
            const auto* right = audio.getReadPointer(1);
            for (int i = 0; i < kBlockSize; ++i)
                out.push_back(left[i] + right[i]);
        }

        return out;
    }
};

/// Magnitude at one frequency, by Goertzel over the whole render. Pitch is what
/// separates a chord that was strummed from one whose voices were retuned to
/// something else after being excited (#2364).
double magnitudeAt(const std::vector<float>& samples, double hz) {
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (const auto sample : samples) {
        const double s0 = static_cast<double>(sample) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / samples.size();
}

double hzOf(int midiNote) {
    return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
}

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
    const auto topAlone = RingsRig{}.render({{67, 0.0}});

    REQUIRE(energy(chord) > 0.0);

    // The two lower notes are in the chord and not in the single note, so each
    // has to stand above what the top note alone leaves at its frequency. Level
    // says nothing here: one strum through a stale note filter is louder than
    // three clean ones, it is just rumble rather than the chord.
    CHECK(magnitudeAt(chord, hzOf(60)) > 10.0 * magnitudeAt(topAlone, hzOf(60)));
    CHECK(magnitudeAt(chord, hzOf(64)) > 3.0 * magnitudeAt(topAlone, hzOf(64)));
}

TEST_CASE("Rings strums notes closer together than one internal block", "[rings][mutable]") {
    // An internal block is 24 samples at 48 kHz, so three notes inside ten host
    // samples all land in one, which collapsed them the same way.
    const auto rolled =
        RingsRig{}.render({{60, 0.0}, {64, 4.0 / kSampleRate}, {67, 8.0 / kSampleRate}});
    const auto topAlone = RingsRig{}.render({{67, 8.0 / kSampleRate}});

    REQUIRE(energy(rolled) > 0.0);
    CHECK(magnitudeAt(rolled, hzOf(60)) > 10.0 * magnitudeAt(topAlone, hzOf(60)));
}

TEST_CASE("Rings holds each chord note at its own pitch", "[rings][mutable]") {
    // Part retunes the voice it rotates away from to the note filter's delayed
    // stable note, and that line is only written on blocks which do not strum.
    // Draining a chord one strum per block gave it nothing to carry, so a note
    // was excited at its own pitch and then retuned to whatever the line held.
    //
    // Two chords sharing their lower notes and differing in the top one. Both
    // strum three times, so what separates them is pitch and nothing else.
    const auto low = RingsRig{}.render({{60, 0.0}, {64, 0.0}, {67, 0.0}});
    const auto high = RingsRig{}.render({{60, 0.0}, {64, 0.0}, {72, 0.0}});

    REQUIRE(energy(low) > 0.0);
    REQUIRE(energy(high) > 0.0);

    // Changing only the top note has to change the render. With the stale line
    // the two came out bit for bit identical: every note but the last was
    // retuned to what it held, so the chord that was played did not survive.
    CHECK_FALSE(identical(low, high));

    // And the chord sits at its own pitches rather than at MIDI note 0, which
    // is what the line holds on a fresh instance and is 8 Hz of rumble.
    for (const auto& render : {low, high}) {
        CHECK(magnitudeAt(render, hzOf(64)) > 10.0 * magnitudeAt(render, hzOf(0)));
    }
}

TEST_CASE("Rings still sounds a chord larger than its polyphony", "[rings][mutable]") {
    // More notes than voices is not an error: the part rotates, so the last
    // four win the strings and nothing is dropped on the way in.
    const auto wide =
        RingsRig{}.render({{55, 0.0}, {59, 0.0}, {62, 0.0}, {65, 0.0}, {69, 0.0}, {72, 0.0}});
    CHECK(energy(wide) > 0.0);
}
