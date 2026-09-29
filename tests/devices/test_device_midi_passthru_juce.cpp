#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"

// Non-note MIDI through the arpeggiator, on both adapters (#2417).
//
// The device replaces the notes it is handed and passes the rest of the
// channel on, because thru would bring the held chord back with it. Both hosts
// hand the device its input and take its output through containers of their
// own -- the fork swaps a te::MidiMessageArray, the engine writes back onto a
// juce::MidiBuffer -- so the two are driven over the same block and compared
// rather than assumed.

namespace {

namespace audio = magda::daw::audio;
namespace adapter = magda::daw::audio::engine_adapter;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;
constexpr int kController = 11;
constexpr int kControllerValue = 77;

/// A held note and an expression pedal on the same instant, which is the input
/// both legs are given.
juce::MidiMessage heldNote() {
    return juce::MidiMessage::noteOn(1, 60, juce::uint8{90});
}

juce::MidiMessage pedal() {
    return juce::MidiMessage::controllerEvent(1, kController, kControllerValue);
}

int count(const std::vector<juce::MidiMessage>& midi, bool (*matches)(const juce::MidiMessage&)) {
    int total = 0;
    for (const auto& message : midi)
        if (matches(message))
            ++total;
    return total;
}

bool isForwardedPedal(const juce::MidiMessage& message) {
    return message.isController() && message.getControllerNumber() == kController &&
           message.getControllerValue() == kControllerValue && message.getChannel() == 1;
}

bool isNoteOn(const juce::MidiMessage& message) {
    return message.isNoteOn();
}

/// What the fork's leg leaves on the buffer the host reads back.

/// The same block through the engine's leg, read back off the port.
std::vector<juce::MidiMessage> engineLegOutput() {
    adapter::EngineMagdaDevice hosted(std::make_unique<audio::ArpeggiatorPlugin>(),
                                      /*offlineRender=*/false);
    hosted.prepare({.sampleRate = kSampleRate, .maxBlockSize = kBlockSize, .numChannels = 2});

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    juce::MidiBuffer in;
    juce::MidiBuffer out;
    in.addEvent(heldNote(), 0);
    in.addEvent(pedal(), 0);

    magda::engine::DeviceBlock block;
    block.audio = juce::dsp::AudioBlock<float>(buffer);
    block.midiIn = &in;
    block.midiOut = &out;
    block.block.numSamples = kBlockSize;
    block.block.sampleRate = kSampleRate;
    block.block.playing = true;
    block.block.seconds.end = kBlockSize / kSampleRate;

    hosted.process(block);

    std::vector<juce::MidiMessage> output;
    output.reserve(static_cast<std::size_t>(out.getNumEvents()));
    for (const auto metadata : out)
        output.push_back(metadata.getMessage());
    return output;
}

class DeviceMidiPassThruTest final : public juce::UnitTest {
  public:
    DeviceMidiPassThruTest() : juce::UnitTest("Device MIDI Pass-Through", "magda") {}

    void runTest() override {
        beginTest("The native adapter carry the pedal past the arpeggiator");

        const auto engineLeg = engineLegOutput();

        expect(count(engineLeg, isForwardedPedal) == 1, "The engine's leg should pass it on too");

        beginTest("The native adapter never echoes the note the arpeggiator replaced");

        // One note-on on each leg: the arpeggio's. The input's own would be
        // the chord playing under the pattern, which is what thru is for.
        expect(count(engineLeg, isNoteOn) == 1, "The engine's leg should emit only its own too");
    }
};

DeviceMidiPassThruTest deviceMidiPassThruTest;

}  // namespace
