#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "magda/daw/audio/plugins/mutable/MutableRingsPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// #2370: on the TE leg the plugin node has already copied the track's input
// into the render buffer, so an instrument ahead of another device on the
// same chain must add its signal rather than overwrite what is already
// there - the same contract FaustInstrumentPlugin already follows. Covers
// the three ways that contract can break: the device's own output getting
// lost in the scratch buffer, gain being applied to the pre-existing signal
// instead of just the device's contribution, and either channel replacing
// rather than summing.

namespace {

namespace audio = magda::daw::audio;
namespace ta = magda::daw::audio::tracktion_adapter;
namespace te = tracktion::engine;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;
constexpr int kNumBlocks = 8;
constexpr float kExistingSignal = 0.5f;

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

template <typename Device> void setLevelDb(te::Plugin& plugin, const Device& device, float db) {
    auto* adapter = dynamic_cast<ta::TracktionMagdaDevicePlugin*>(&plugin);
    if (auto* param =
            adapter != nullptr ? adapter->parameterForDeviceSlot(Device::kLevel) : nullptr)
        param->setParameterFromHost(
            magda::ParameterUtils::realToNormalized(db, device.parameterInfo(Device::kLevel)),
            juce::sendNotificationSync);
}

void fillWithExistingSignal(juce::AudioBuffer<float>& buffer) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, kExistingSignal);
}

/// Renders `kNumBlocks` blocks with a note-on struck on the first block,
/// returning every rendered sample per channel in order. When `preFillExisting`
/// is set, each block starts from a buffer already carrying `kExistingSignal`,
/// standing in for an audio clip ahead of the instrument on the same chain.
struct RenderResult {
    std::vector<float> left, right;
};

RenderResult renderNote(te::Plugin& plugin, bool preFillExisting) {
    RenderResult result;

    te::MidiMessageArray noteOnMidi;
    noteOnMidi.addMidiMessage(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0.0,
                              te::MidiMessageArray::notMPE);

    for (int block = 0; block < kNumBlocks; ++block) {
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        buffer.clear();
        if (preFillExisting)
            fillWithExistingSignal(buffer);

        te::MidiMessageArray empty;
        te::MidiMessageArray& midi = block == 0 ? noteOnMidi : empty;
        te::PluginRenderContext context(&buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize,
                                        &midi, 0.0, tracktion::TimeRange(), true, false, false,
                                        true);
        plugin.applyToBuffer(context);

        for (int i = 0; i < kBlockSize; ++i) {
            result.left.push_back(buffer.getSample(0, i));
            result.right.push_back(buffer.getSample(1, i));
        }
    }
    return result;
}

float peakAbs(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float s : samples)
        peak = std::max(peak, std::abs(s));
    return peak;
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

        runDeviceTests<audio::MutableElementsPlugin>(*edit, "Materia (Elements)");
        runDeviceTests<audio::MutableRingsPlugin>(*edit, "Rings");
    }

  private:
    template <typename Device> void runDeviceTests(te::Edit& edit, const juce::String& name) {
        {
            beginTest(name + ": a silent block adds to (does not replace) the host buffer");
            auto holder = createDevice<Device>(edit);
            expect(holder != nullptr);
            if (holder == nullptr)
                return;
            prepareForRender(*holder);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            fillWithExistingSignal(buffer);
            te::MidiMessageArray midi;
            te::PluginRenderContext context(&buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize,
                                            &midi, 0.0, tracktion::TimeRange(), true, false, false,
                                            true);
            holder->applyToBuffer(context);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSize; ++i)
                    expectWithinAbsoluteError(buffer.getSample(ch, i), kExistingSignal, 0.05f);
            holder->deleteFromParent();
        }

        {
            beginTest(name + ": a struck note is not dropped by the scratch buffer");
            auto holder = createDevice<Device>(edit);
            expect(holder != nullptr);
            if (holder == nullptr)
                return;
            prepareForRender(*holder);

            const auto dry = renderNote(*holder, false);
            // Rings' main/aux outputs split partials by structure; one can sit
            // at exact silence for a given patch, so this only checks that the
            // device's contribution reaches the buffer at all, not per channel.
            // Channel fidelity is what the next test checks, sample by sample.
            expect(std::max(peakAbs(dry.left), peakAbs(dry.right)) > 0.01f,
                   "A struck note should produce audible output on at least one channel");
            holder->deleteFromParent();
        }

        {
            beginTest(name + ": a struck note adds onto, rather than replaces, an existing signal");
            auto dryHolder = createDevice<Device>(edit);
            auto wetHolder = createDevice<Device>(edit);
            expect(dryHolder != nullptr && wetHolder != nullptr);
            if (dryHolder == nullptr || wetHolder == nullptr)
                return;
            prepareForRender(*dryHolder);
            prepareForRender(*wetHolder);

            const auto dry = renderNote(*dryHolder, false);
            const auto wet = renderNote(*wetHolder, true);

            for (size_t i = 0; i < dry.left.size(); ++i) {
                expectWithinAbsoluteError(wet.left[i], dry.left[i] + kExistingSignal, 0.01f);
                expectWithinAbsoluteError(wet.right[i], dry.right[i] + kExistingSignal, 0.01f);
            }
            dryHolder->deleteFromParent();
            wetHolder->deleteFromParent();
        }

        {
            beginTest(name + ": gain scales the device's own signal, not the existing buffer");
            auto holder = createDevice<Device>(edit);
            expect(holder != nullptr);
            if (holder == nullptr)
                return;
            prepareForRender(*holder);

            auto* device = ta::deviceFromPlugin<Device>(holder.get());
            expect(device != nullptr);
            if (device == nullptr)
                return;
            setLevelDb(*holder, *device, -48.0f);  // gain ~= 0.004: device output near silent

            const auto wet = renderNote(*holder, true);
            for (size_t i = 0; i < wet.left.size(); ++i) {
                expectWithinAbsoluteError(wet.left[i], kExistingSignal, 0.02f);
                expectWithinAbsoluteError(wet.right[i], kExistingSignal, 0.02f);
            }
            holder->deleteFromParent();
        }
    }
};

MutableAddNotReplaceTest mutableAddNotReplaceTest;

}  // namespace
