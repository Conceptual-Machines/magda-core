// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

// Voices that all start their oscillators at phase zero sum a fresh synth's octaves in phase,
// so the first bars after opening a project sounded doubled.

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr int kNoteSamples = 48000;

struct Chord {
    int at;
    std::vector<int> notes;
};

/// RMS of the last chord of @p chords, each held kNoteSamples, on a freshly prepared synth.
double lastChordRms(const std::vector<Chord>& chords) {
    const juce::ScopedNoDenormals noDenormals;
    magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin synth;
    synth.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});

    const int from = chords.back().at;
    double sum = 0.0;
    for (int start = 0; start < from + kNoteSamples; start += kBlockSize) {
        magda::test::DeviceMidiBuffer midi;
        for (const auto& chord : chords)
            for (int note : chord.notes) {
                const int off = chord.at + kNoteSamples;
                if (off >= start && off < start + kBlockSize)
                    midi.events.push_back({juce::MidiMessage::noteOff(1, note),
                                           static_cast<std::uint32_t>(off - start)});
                if (chord.at >= start && chord.at < start + kBlockSize)
                    midi.events.push_back({juce::MidiMessage::noteOn(1, note, juce::uint8(80)),
                                           static_cast<std::uint32_t>(chord.at - start)});
            }
        juce::AudioBuffer<float> audio(2, kBlockSize);
        audio.clear();
        magda::test::DeviceMidiBuffer out;
        magda::test::DeviceTestBlock block(audio, kBlockSize);
        block.context.midiIn = &midi;
        block.context.midiOut = &out;
        block.context.isPlaying = true;
        synth.process(block.context);
        for (int i = std::max(from - start, 0); i < kBlockSize && start + i < from + kNoteSamples;
             ++i)
            sum += double(audio.getSample(0, i)) * audio.getSample(0, i);
    }
    return std::sqrt(sum / kNoteSamples);
}

}  // namespace

TEST_CASE("A fresh Poly Synth plays an octave as loud as one on reused voices",
          "[devices][compiled][polysynth-phase]") {
    // The first chord's voices are free again, past their release, when the octave is struck.
    const double fresh = lastChordRms({{0, {57, 69}}});
    const double reused = lastChordRms({{0, {50, 53}}, {2 * kNoteSamples, {57, 69}}});
    REQUIRE(reused > 0.0);
    CHECK(std::abs(20.0 * std::log10(fresh / reused)) < 1.0);
}
