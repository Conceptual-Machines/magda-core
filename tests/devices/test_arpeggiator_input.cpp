#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <vector>

#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"

namespace {
namespace audio = magda::daw::audio;
using Arp = audio::ArpeggiatorPlugin;

// Like the Tracktion view, additions may invalidate references. Also detect
// writes before clear deterministically, without depending on a freed read
// happening to return the wrong note on a particular allocator.
class MidiBuffer final : public audio::DeviceMidiBuffer {
  public:
    std::vector<audio::DeviceMidiEvent> events;
    bool cleared = false;
    int additionsBeforeClear = 0;
    bool allNotesOff = false;

    int size() const override {
        return static_cast<int>(events.size());
    }
    const juce::MidiMessage& message(int index) const override {
        return events.at(index).message;
    }
    std::uint32_t sourceId(int index) const override {
        return events.at(index).sourceId;
    }
    void setEvent(int index, audio::DeviceMidiEvent event) override {
        events.at(index) = event;
    }
    void removeEvent(int index) override {
        events.erase(events.begin() + index);
    }
    void addEvent(audio::DeviceMidiEvent event) override {
        if (!cleared)
            ++additionsBeforeClear;
        events.push_back(event);
    }
    void clear() override {
        events.clear();
        cleared = true;
        allNotesOff = false;
    }
    void sortByTimestamp() override {
        std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            return a.message.getTimeStamp() < b.message.getTimeStamp();
        });
    }
    bool isAllNotesOff() const override {
        return allNotesOff;
    }
    void setAllNotesOff(bool value) override {
        allNotesOff = value;
    }
};

class Tempo final : public audio::DeviceTempoMap {
  public:
    double beatsAtSeconds(double seconds) const override {
        return seconds * 2.0;
    }
    double bpmAtSeconds(double) const override {
        return 120.0;
    }
};

struct Rig {
    Arp arp;
    Tempo tempo;

    explicit Rig(bool latch = false) {
        arp.prepare({.sampleRate = 48000.0, .maximumBlockSize = 2400});
        arp.setParameterValue(Arp::kLatch, latch ? 1.0f : 0.0f);
        arp.setParameterValue(Arp::kGate, 1.0f);
        arp.setParameterValue(
            Arp::kRate, magda::ParameterUtils::realToNormalized(
                            static_cast<float>(Arp::Rate::Quarter), arp.parameterInfo(Arp::kRate)));
    }

    MidiBuffer run(double start, std::initializer_list<juce::MidiMessage> input = {},
                   bool playing = true) {
        MidiBuffer midi;
        for (const auto& message : input)
            midi.events.push_back({message, 42});
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

void checkOff(const MidiBuffer& midi, int index = 0) {
    REQUIRE(midi.size() > index);
    CHECK(midi.message(index).isNoteOff());
    CHECK(midi.message(index).getNoteNumber() == 60);
    CHECK(midi.message(index).getChannel() == 1);
    CHECK(midi.message(index).getTimeStamp() == 0.0);
}
}  // namespace

TEST_CASE("Arpeggiator latch replacement preserves the old note-off and new input",
          "[arpeggiator][midi][input]") {
    Rig rig(true);
    rig.startNote();
    REQUIRE(rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)}).size() == 0);

    auto midi = rig.run(0.5, {juce::MidiMessage::noteOn(1, 67, juce::uint8{73})});
    REQUIRE(midi.size() == 2);
    checkOff(midi);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 67);
    CHECK(midi.message(1).getVelocity() == 73);
}

TEST_CASE("Arpeggiator upstream panic stops its sounding note exactly once",
          "[arpeggiator][midi][input]") {
    for (int controller : {120, 123}) {
        INFO("controller " << controller);
        Rig rig;
        rig.startNote();
        auto midi = rig.run(0.05, {juce::MidiMessage::controllerEvent(1, controller, 0)});
        REQUIRE(midi.size() == 1);
        checkOff(midi);
        CHECK(rig.run(0.1).size() == 0);
        CHECK(rig.run(0.5).size() == 0);
    }
}

TEST_CASE("Arpeggiator input resets retain a single pending note-off",
          "[arpeggiator][midi][input]") {
    Rig rig(true);
    rig.startNote();
    rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)});
    auto midi = rig.run(0.5, {juce::MidiMessage::noteOn(1, 64, juce::uint8{80}),
                              juce::MidiMessage::allNotesOff(1), juce::MidiMessage::allSoundOff(1),
                              juce::MidiMessage::noteOn(1, 67, juce::uint8{73})});
    REQUIRE(midi.size() == 2);
    checkOff(midi);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 67);
    CHECK(midi.message(1).getVelocity() == 73);
}

TEST_CASE("Arpeggiator replacing an unlatched chord stops the previous note",
          "[arpeggiator][midi][input]") {
    Rig rig;
    rig.startNote();
    auto midi = rig.run(0.5, {juce::MidiMessage::noteOff(1, 60),
                              juce::MidiMessage::noteOn(1, 67, juce::uint8{73})});
    REQUIRE(midi.size() == 2);
    checkOff(midi);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 67);
}

TEST_CASE("Arpeggiator release and transport stop still send one note-off",
          "[arpeggiator][midi][input]") {
    Rig rig;
    rig.startNote();
    MidiBuffer midi;
    SECTION("release") {
        midi = rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)});
    }
    SECTION("transport stop") {
        midi = rig.run(0.05, {}, false);
    }
    REQUIRE(midi.size() == 1);
    checkOff(midi);
    CHECK(rig.run(0.1, {}, false).size() == 0);
}
