#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <memory>

#include "SharedTestEngine.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

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
namespace te = tracktion::engine;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;

/// What the fork's leg reports for a block whose input carried @p panic: the
/// flag left on the buffer the host reads back.
bool teLegAnswer(te::Edit& edit, bool panic) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, audio::ArpeggiatorPlugin::xmlTypeName, nullptr);
    auto plugin = edit.getPluginCache().createNewPlugin(state);
    if (plugin == nullptr)
        return false;

    te::PluginInitialisationInfo initInfo;
    initInfo.startTime = tracktion::TimePosition();
    initInfo.sampleRate = kSampleRate;
    initInfo.blockSizeSamples = kBlockSize;
    plugin->baseClassInitialise(initInfo);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();

    te::MidiMessageArray midi;
    midi.isAllNotesOff = panic;
    te::PluginRenderContext context(
        &buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi, 0.0,
        tracktion::TimeRange(tracktion::TimePosition(),
                             tracktion::TimePosition::fromSeconds(kBlockSize / kSampleRate)),
        true, false, false, false);
    plugin->applyToBuffer(context);

    const auto answer = midi.isAllNotesOff;
    plugin->deleteFromParent();
    return answer;
}

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
        beginTest("Engine setup");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        beginTest("Both adapters answer the same for a host panic");

        expect(teLegAnswer(*edit, true), "The fork's leg should carry a raised panic through");
        expect(engineLegAnswer(true), "The engine's leg should carry the same panic through");

        beginTest("Both adapters answer the same for an ordinary block");

        expect(!teLegAnswer(*edit, false), "The fork's leg should invent no panic");
        expect(!engineLegAnswer(false), "The engine's leg should invent none either");
    }
};

DevicePanicFlagTest devicePanicFlagTest;

}  // namespace
