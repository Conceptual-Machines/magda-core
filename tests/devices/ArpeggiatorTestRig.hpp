#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"

namespace magda::test {

namespace audio = magda::daw::audio;
using Arp = audio::ArpeggiatorPlugin;

class ArpTempo final : public audio::DeviceTempoMap {
  public:
    double beatsAtSeconds(double seconds) const override {
        return seconds * 2.0;
    }
    double bpmAtSeconds(double) const override {
        return 120.0;
    }
};

struct ArpRig {
    Arp arp;
    ArpTempo tempo;
    /// Who the events come from, and which sources the host calls live input.
    /// The two together are how the arp tells a player's keys from clip
    /// playback, so a test says which of the two this phrase is.
    std::uint32_t source = 42;
    std::vector<std::uint32_t> liveSourceIds;

    explicit ArpRig(bool latch = false) {
        arp.prepare({.sampleRate = 48000.0, .maximumBlockSize = 2400});
        arp.setParameterValue(Arp::kLatch, latch ? 1.0f : 0.0f);
        arp.setParameterValue(Arp::kGate, 1.0f);
        setRate(Arp::Rate::Quarter);
    }

    void setRate(Arp::Rate rate) {
        arp.setParameterValue(
            Arp::kRate, magda::ParameterUtils::realToNormalized(static_cast<float>(rate),
                                                                arp.parameterInfo(Arp::kRate)));
    }

    DeviceMidiBuffer run(double start, std::initializer_list<juce::MidiMessage> input = {},
                         bool playing = true, bool panic = false) {
        DeviceMidiBuffer in;
        in.allNotesOff = panic;
        for (const auto& message : input)
            in.events.push_back({message, source});
        DeviceMidiBuffer out;
        audio::DeviceProcessContext context;
        context.midiIn = &in;
        context.midiOut = &out;
        context.tempoMap = &tempo;
        context.numSamples = 2400;
        context.timelineStartSeconds = start;
        context.timelineEndSeconds = start + 0.05;
        context.isPlaying = playing;
        context.liveSourceIds = liveSourceIds.empty() ? nullptr : liveSourceIds.data();
        context.numLiveSourceIds = static_cast<int>(liveSourceIds.size());
        arp.process(context);
        return out;
    }

    void startNote() {
        auto midi = run(0.0, {juce::MidiMessage::noteOn(1, 60, juce::uint8{91})});
        REQUIRE(midi.size() == 1);
        REQUIRE(midi.message(0).isNoteOn());
        REQUIRE(midi.message(0).getNoteNumber() == 60);
    }
};

inline void checkNoteOff(const DeviceMidiBuffer& midi, int index = 0, int noteNumber = 60) {
    REQUIRE(midi.size() > index);
    CHECK(midi.message(index).isNoteOff());
    CHECK(midi.message(index).getNoteNumber() == noteNumber);
    CHECK(midi.message(index).getChannel() == 1);
    CHECK(midi.message(index).getTimeStamp() == 0.0);
}

}  // namespace magda::test
