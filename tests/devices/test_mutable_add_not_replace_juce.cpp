#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "magda/daw/audio/plugins/mutable/MutableRingsPlugin.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// #2370: on the TE leg the plugin node has already copied the track's input
// into the render buffer, so an instrument ahead of another device on the
// same chain must add its signal rather than overwrite what is already
// there - the same contract FaustInstrumentPlugin already follows.

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;

template <typename Device> te::Plugin::Ptr createDevice(te::Edit& edit) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, Device::xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

void prepareForRender(te::Plugin& plugin) {
    te::PluginInitialisationInfo info;
    info.startTime = tracktion::TimePosition();
    info.sampleRate = kSampleRate;
    info.blockSizeSamples = kBlockSize;
    plugin.baseClassInitialise(info);
}

/// Render one block with no MIDI over a buffer pre-filled with a DC value,
/// standing in for an audio clip ahead of the instrument on the same chain.
/// With no note played the device itself contributes silence, so the DC
/// value survives unless the device replaced the buffer outright.
float renderOverExistingSignal(te::Plugin& plugin) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, 0.5f);
        buffer.setSample(1, i, 0.5f);
    }

    te::MidiMessageArray midi;
    te::PluginRenderContext context(&buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi,
                                    0.0, tracktion::TimeRange(), true, false, false, true);
    plugin.applyToBuffer(context);
    return buffer.getSample(0, kBlockSize - 1);
}

class MutableAddNotReplaceTest final : public juce::UnitTest {
  public:
    MutableAddNotReplaceTest() : juce::UnitTest("Mutable Add Not Replace", "magda") {}

    void runTest() override {
        beginTest("Engine setup");
        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        beginTest("Materia (Elements) adds to, rather than replaces, the host buffer");
        if (auto holder = createDevice<audio::MutableElementsPlugin>(*edit)) {
            prepareForRender(*holder);
            expectWithinAbsoluteError(renderOverExistingSignal(*holder), 0.5f, 0.05f);
            holder->deleteFromParent();
        } else {
            expect(false, "Materia plugin should have been created");
        }

        beginTest("Rings adds to, rather than replaces, the host buffer");
        if (auto holder = createDevice<audio::MutableRingsPlugin>(*edit)) {
            prepareForRender(*holder);
            expectWithinAbsoluteError(renderOverExistingSignal(*holder), 0.5f, 0.05f);
            holder->deleteFromParent();
        } else {
            expect(false, "Rings plugin should have been created");
        }
    }
};

MutableAddNotReplaceTest mutableAddNotReplaceTest;

}  // namespace
