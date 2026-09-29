#include <juce_core/juce_core.h>

#include <memory>

#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"

// The panic flag through both adapters (#2418).
//
// A device asks one question -- DeviceMidiInput::isAllNotesOff -- and until now
// only one host answered it: the fork carries the flag on its MIDI container,
// the engine's juce::MidiBuffer has nowhere to put it, so every device on the
// native engine read false and every setAllNotesOff() was dropped.
//
// The arpeggiator is the device that acts on it (#2413): it releases what it is
// sounding and passes the panic on. Same device, same discontinuity, driven
// through each adapter, so the two answers can be compared rather than assumed.

namespace {

namespace audio = magda::daw::audio;
namespace adapter = magda::daw::audio::engine_adapter;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;

/// What the fork's leg reports for a block whose input carried @p panic: the
/// flag left on the buffer the host reads back.

/// The same question of the engine's leg: the flag left beside the port.
bool engineLegAnswer(bool panic) {
    adapter::EngineMagdaDevice hosted(std::make_unique<audio::ArpeggiatorPlugin>(),
                                      /*offlineRender=*/false);
    hosted.prepare({.sampleRate = kSampleRate, .maxBlockSize = kBlockSize, .numChannels = 2});

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    juce::MidiBuffer in;
    juce::MidiBuffer out;

    magda::engine::DeviceBlock block;
    block.audio = juce::dsp::AudioBlock<float>(buffer);
    block.midiIn = &in;
    block.midiOut = &out;
    block.midiInAllNotesOff = panic;
    block.block.numSamples = kBlockSize;
    block.block.sampleRate = kSampleRate;
    block.block.playing = true;
    block.block.seconds.end = kBlockSize / kSampleRate;

    hosted.process(block);
    return block.midiOutAllNotesOff;
}

class DevicePanicFlagTest final : public juce::UnitTest {
  public:
    DevicePanicFlagTest() : juce::UnitTest("Device Panic Flag", "magda") {}

    void runTest() override {
        beginTest("The native adapter answer the same for a host panic");

        expect(engineLegAnswer(true), "The engine's leg should carry the same panic through");

        beginTest("The native adapter answer the same for an ordinary block");

        expect(!engineLegAnswer(false), "The engine's leg should invent none either");
    }
};

DevicePanicFlagTest devicePanicFlagTest;

}  // namespace
