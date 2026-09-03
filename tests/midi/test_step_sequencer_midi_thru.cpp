#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "audio/plugins/MagdaDevice.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"

// MIDI thru on a step sequencer: what reaches the device reaches the
// instrument downstream, all of it.
//
// A device must not size a store for its own input (#2335). A MIDI port's
// bound is bytes rather than events - the cheapest message is one byte of data,
// so 4096 bytes is over 500 events, not the ~450 a note-sized division
// suggests - and a device fed by a merge is fed the sum of several producers,
// which is why the plan executor has to tell EngineMagdaDevice its real bound.
// Any event count a device picks for itself is a count some legal block
// exceeds, and the tail it drops can be the note-off that leaves the
// instrument holding a note.

namespace {

namespace audio = magda::daw::audio;

/// The SDK's MIDI view over a plain vector, as a host adapter supplies it.
class TestMidiBuffer final : public audio::DeviceMidiBuffer {
  public:
    int size() const override {
        return static_cast<int>(events.size());
    }
    const juce::MidiMessage& message(int index) const override {
        return events[static_cast<std::size_t>(index)].message;
    }
    std::uint32_t sourceId(int index) const override {
        return events[static_cast<std::size_t>(index)].sourceId;
    }
    void setEvent(int index, audio::DeviceMidiEvent event) override {
        events[static_cast<std::size_t>(index)] = std::move(event);
    }
    void removeEvent(int index) override {
        events.erase(events.begin() + index);
    }
    void addEvent(audio::DeviceMidiEvent event) override {
        events.push_back(std::move(event));
    }
    void clear() override {
        events.clear();
    }
    void sortByTimestamp() override {
        std::sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
            return left.message.getTimeStamp() < right.message.getTimeStamp();
        });
    }
    bool isAllNotesOff() const override {
        return allNotesOff;
    }
    void setAllNotesOff(bool value) override {
        allNotesOff = value;
    }

    std::vector<audio::DeviceMidiEvent> events;
    bool allNotesOff = false;
};

/// A dense but entirely legal block: clocks are one byte of data each, so far
/// more of them fit inside a port's byte budget than notes would, and the
/// note-off is deliberately last.
void fillWithClocksAndAHeldNote(TestMidiBuffer& midi, int clockCount) {
    midi.events.push_back({juce::MidiMessage::noteOn(1, 64, 1.0f), 0});
    for (int i = 0; i < clockCount; ++i)
        midi.events.push_back({juce::MidiMessage::midiClock(), 0});
    midi.events.push_back({juce::MidiMessage::noteOff(1, 64), 0});
}

int countNoteOffs(const TestMidiBuffer& midi, int noteNumber) {
    int found = 0;
    for (const auto& event : midi.events)
        if (event.message.isNoteOff() && event.message.getNoteNumber() == noteNumber)
            ++found;
    return found;
}

int countClocks(const TestMidiBuffer& midi) {
    int found = 0;
    for (const auto& event : midi.events)
        if (event.message.isMidiClock())
            ++found;
    return found;
}

/// One block through @p device with the transport stopped, so the only MIDI
/// the buffer ends up with is what thru let through.
template <typename DeviceT> void runIdleBlock(DeviceT& device, TestMidiBuffer& midi) {
    device.prepare({.sampleRate = 48000.0, .maximumBlockSize = 512});
    audio::DeviceProcessContext context{
        .midi = &midi,
        .numSamples = 512,
        .isPlaying = false,
    };
    device.process(context);
}

}  // namespace

TEST_CASE("A step sequencer passes a dense block through whole", "[sequencer][midithru]") {
    // 600 events: past both the 64 the device used to keep and the ~450 a
    // note-sized division of the byte budget suggests.
    constexpr int kClocks = 600;

    SECTION("mono") {
        audio::StepSequencerPlugin device;
        device.midiThru.store(true);

        TestMidiBuffer midi;
        fillWithClocksAndAHeldNote(midi, kClocks);
        runIdleBlock(device, midi);

        REQUIRE(countClocks(midi) == kClocks);
        // The note-off is the last event in, and losing it is what leaves the
        // instrument downstream holding the note.
        REQUIRE(countNoteOffs(midi, 64) == 1);
    }

    SECTION("poly") {
        audio::PolyStepSequencerPlugin device;
        device.midiThru.store(true);

        TestMidiBuffer midi;
        fillWithClocksAndAHeldNote(midi, kClocks);
        runIdleBlock(device, midi);

        REQUIRE(countClocks(midi) == kClocks);
        REQUIRE(countNoteOffs(midi, 64) == 1);
    }
}

TEST_CASE("A step sequencer with thru off swallows what reaches it", "[sequencer][midithru]") {
    audio::StepSequencerPlugin device;
    device.midiThru.store(false);

    TestMidiBuffer midi;
    fillWithClocksAndAHeldNote(midi, 32);
    runIdleBlock(device, midi);

    REQUIRE(countClocks(midi) == 0);
    REQUIRE(countNoteOffs(midi, 64) == 0);
}
