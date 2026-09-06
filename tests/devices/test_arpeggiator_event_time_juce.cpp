#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <optional>

#include "SharedTestEngine.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// Input read at event time, through both adapters (#2415).
//
// The arp closes the note it is sounding when a panic reaches it. Where that
// note-off lands is the question: at the top of the block, which is a whole
// block early, or at the panic. The two hosts stamp their MIDI differently --
// the fork carries seconds on its container, the engine's ports count samples
// -- so the same phrase is driven through each and the answers compared.

namespace {

namespace audio = magda::daw::audio;
namespace adapter = magda::daw::audio::engine_adapter;
namespace te = tracktion::engine;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;
/// Nine tenths of the way into the second block.
constexpr int kPanicSample = 57;

juce::MidiMessage noteOn(int note) {
    return juce::MidiMessage::noteOn(1, note, juce::uint8{100});
}

/// Where the arp put the note-off closing what it was sounding, in samples from
/// the start of the block that carried the panic, or nothing if it sent none.
std::optional<int> teLegNoteOffSample(te::Edit& edit) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, audio::ArpeggiatorPlugin::xmlTypeName, nullptr);
    auto plugin = edit.getPluginCache().createNewPlugin(state);
    if (plugin == nullptr)
        return {};

    te::PluginInitialisationInfo initInfo;
    initInfo.startTime = tracktion::TimePosition();
    initInfo.sampleRate = kSampleRate;
    initInfo.blockSizeSamples = kBlockSize;
    plugin->baseClassInitialise(initInfo);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    const auto blockSeconds = kBlockSize / kSampleRate;

    const auto runBlock = [&](int blockIndex, te::MidiMessageArray& midi) {
        buffer.clear();
        te::PluginRenderContext context(
            &buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi, 0.0,
            tracktion::TimeRange(
                tracktion::TimePosition::fromSeconds(blockIndex * blockSeconds),
                tracktion::TimePosition::fromSeconds((blockIndex + 1) * blockSeconds)),
            true, false, false, false);
        plugin->applyToBuffer(context);
    };

    // A key, so the arp has something to sound.
    te::MidiMessageArray held;
    held.addMidiMessage(noteOn(60), 0.0, te::MPESourceID());
    runBlock(0, held);

    // The panic, nine tenths of the way through the block that follows.
    te::MidiMessageArray panic;
    panic.addMidiMessage(juce::MidiMessage::allNotesOff(1), kPanicSample / kSampleRate,
                         te::MPESourceID());
    runBlock(1, panic);

    std::optional<int> found;
    for (const auto& message : panic) {
        if (message.isNoteOff())
            found = juce::roundToInt(message.getTimeStamp() * kSampleRate);
    }

    plugin->deleteFromParent();
    return found;
}

/// The same phrase through the engine's adapter, whose ports carry samples.
std::optional<int> engineLegNoteOffSample() {
    adapter::EngineMagdaDevice hosted(std::make_unique<audio::ArpeggiatorPlugin>(),
                                      /*offlineRender=*/false);
    hosted.prepare({.sampleRate = kSampleRate, .maxBlockSize = kBlockSize, .numChannels = 2});

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    const auto blockSeconds = kBlockSize / kSampleRate;

    const auto runBlock = [&](int blockIndex, juce::MidiBuffer& in, juce::MidiBuffer& out) {
        buffer.clear();
        magda::engine::DeviceBlock block;
        block.audio = juce::dsp::AudioBlock<float>(buffer);
        block.midiIn = &in;
        block.midiOut = &out;
        block.block.numSamples = kBlockSize;
        block.block.sampleRate = kSampleRate;
        block.block.playing = true;
        block.block.seconds.start = blockIndex * blockSeconds;
        block.block.seconds.end = (blockIndex + 1) * blockSeconds;
        hosted.process(block);
    };

    juce::MidiBuffer held;
    juce::MidiBuffer heldOut;
    held.addEvent(noteOn(60), 0);
    runBlock(0, held, heldOut);

    juce::MidiBuffer panic;
    juce::MidiBuffer panicOut;
    panic.addEvent(juce::MidiMessage::allNotesOff(1), kPanicSample);
    runBlock(1, panic, panicOut);

    std::optional<int> found;
    for (const auto metadata : panicOut) {
        if (metadata.getMessage().isNoteOff())
            found = metadata.samplePosition;
    }
    return found;
}

class ArpeggiatorEventTimeTest final : public juce::UnitTest {
  public:
    ArpeggiatorEventTimeTest() : juce::UnitTest("Arpeggiator Event Time", "magda") {}

    void runTest() override {
        beginTest("Engine setup");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        beginTest("Both adapters close the note where the panic reached the device");

        const auto teLeg = teLegNoteOffSample(*edit);
        expect(teLeg.has_value(), "The fork's leg should close the sounding note");
        expect(teLeg.value_or(-1) == kPanicSample,
               "The fork's leg put the note-off at sample " + juce::String(teLeg.value_or(-1)) +
                   " rather than at the panic's " + juce::String(kPanicSample));

        const auto engineLeg = engineLegNoteOffSample();
        expect(engineLeg.has_value(), "The engine's leg should close the sounding note");
        expect(engineLeg.value_or(-1) == kPanicSample,
               "The engine's leg put the note-off at sample " +
                   juce::String(engineLeg.value_or(-1)) + " rather than at the panic's " +
                   juce::String(kPanicSample));
    }
};

ArpeggiatorEventTimeTest arpeggiatorEventTimeTest;

}  // namespace
