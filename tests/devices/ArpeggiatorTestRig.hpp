#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"

namespace magda::test {

namespace audio = magda::daw::audio;
using Arp = audio::ArpeggiatorPlugin;

// Like the Tracktion view, additions may invalidate references. Also detect
// writes before clear deterministically, without depending on a freed read
// happening to return the wrong note on a particular allocator.
class ArpMidiBuffer final : public DeviceMidiBuffer {
  public:
    bool cleared = false;
    int additionsBeforeClear = 0;

    void addEvent(audio::DeviceMidiEvent event) override {
        if (!cleared)
            ++additionsBeforeClear;
        DeviceMidiBuffer::addEvent(std::move(event));
    }
    void clear() override {
        DeviceMidiBuffer::clear();
        cleared = true;
    }
};

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
    /// Who the events come from. The arp learns which sources are live keys
    /// rather than clip playback, so a test picks a source per phrase.
    std::uint32_t source = 42;

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

    ArpMidiBuffer run(double start, std::initializer_list<juce::MidiMessage> input = {},
                      bool playing = true, bool panic = false) {
        ArpMidiBuffer midi;
        midi.setAllNotesOff(panic);
        for (const auto& message : input)
            midi.events.push_back({message, source});
        midi.events.shrink_to_fit();
        audio::DeviceProcessContext context;
        context.midi = &midi;
        context.tempoMap = &tempo;
        context.numSamples = 2400;
        context.timelineStartSeconds = start;
        context.timelineEndSeconds = start + 0.05;
        context.isPlaying = playing;
        arp.process(context);
        CHECK(midi.additionsBeforeClear == 0);
        return midi;
    }

    void startNote() {
        auto midi = run(0.0, {juce::MidiMessage::noteOn(1, 60, juce::uint8{91})});
        REQUIRE(midi.size() == 1);
        REQUIRE(midi.message(0).isNoteOn());
        REQUIRE(midi.message(0).getNoteNumber() == 60);
    }
};

inline void checkNoteOff(const ArpMidiBuffer& midi, int index = 0, int noteNumber = 60) {
    REQUIRE(midi.size() > index);
    CHECK(midi.message(index).isNoteOff());
    CHECK(midi.message(index).getNoteNumber() == noteNumber);
    CHECK(midi.message(index).getChannel() == 1);
    CHECK(midi.message(index).getTimeStamp() == 0.0);
}

}  // namespace magda::test
