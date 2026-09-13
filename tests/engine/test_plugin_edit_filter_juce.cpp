#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/ParameterUtils.hpp"
#include "exec/EngineDevice.hpp"
#include "magda/daw/audio/plugins/engine/EngineExternalDevice.hpp"
#include "param/ParamBlock.hpp"

/**
 * What a hosted plugin says its parameters hold, and what MAGDA may write to
 * them (docs/specs/hosted-plugin-parameter-control.md).
 *
 * Here rather than in magda_tests because the report is queued from the
 * callback and delivered on the message thread, and the assertion is about what
 * arrives there.
 */

namespace {

namespace adapter = magda::daw::audio::engine_adapter;
namespace engine = magda::engine;

class StubParameter final : public juce::AudioProcessorParameterWithID {
  public:
    StubParameter(const juce::String& id, const juce::String& name)
        : juce::AudioProcessorParameterWithID(juce::ParameterID{id, 1}, name) {}

    float getValue() const override {
        return value_;
    }

    void setValue(float newValue) override {
        value_ = newValue;
    }

    float getDefaultValue() const override {
        return 0.0f;
    }

    float getValueForText(const juce::String& text) const override {
        return text.getFloatValue();
    }

  private:
    float value_ = 0.0f;
};

/// Stereo in and out, three automatable parameters, and no state of its own.
class StubPlugin final : public juce::AudioPluginInstance {
  public:
    StubPlugin()
        : AudioPluginInstance(BusesProperties()
                                  .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        for (const auto* name : {"Gain", "Tone", "Drive"}) {
            auto parameter =
                std::make_unique<StubParameter>(juce::String(name).toLowerCase(), name);
            parameters.push_back(parameter.get());
            addHostedParameter(std::move(parameter));
        }
    }

    const juce::String getName() const override {
        return "Stub";
    }

    void fillInPluginDescription(juce::PluginDescription& description) const override {
        description.name = "Stub";
        description.pluginFormatName = "VST3";
        description.manufacturerName = "MAGDA";
    }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    /// Where a VST3's echo of a host write lands: flushed from
    /// outputParameterChanges once the plugin has run, on the audio thread.
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {
        if (echoesDuringProcess.has_value())
            parameters[0]->setValueNotifyingHost(*echoesDuringProcess);
    }

    double getTailLengthSeconds() const override {
        return 0.0;
    }

    bool acceptsMidi() const override {
        return false;
    }

    bool producesMidi() const override {
        return false;
    }

    juce::AudioProcessorEditor* createEditor() override {
        return nullptr;
    }

    bool hasEditor() const override {
        return false;
    }

    int getNumPrograms() override {
        return 1;
    }

    int getCurrentProgram() override {
        return 0;
    }

    void setCurrentProgram(int) override {}

    const juce::String getProgramName(int) override {
        return {};
    }

    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    /// In plugin order, which is slots two upwards: the wrapper pair is in
    /// front of them.
    std::vector<StubParameter*> parameters;

    /// What the plugin reports back for its first parameter every block, if
    /// anything.
    std::optional<float> echoesDuringProcess;
};

/// The plan's window for the device, built by hand: one segment per slot it
/// carries, holding a value the plugin is not already on.
class Window {
  public:
    void carry(int slot, float value, bool driven = false) {
        segments_.push_back({.startSample = 0, .startValue = value, .endValue = value});
        counts_.push_back(1);
        domains_.push_back(
            {.scale = magda::ParameterScale::Linear, .minValue = 0.0f, .maxValue = 1.0f});
        slots_.push_back(slot);
        driven_.push_back(driven ? 1 : 0);
    }

    engine::DeviceParams params(int numSamples) const {
        return {segments_, counts_, domains_, slots_, driven_, 1, numSamples};
    }

  private:
    std::vector<engine::ParamSegment> segments_;
    std::vector<int> counts_;
    std::vector<magda::ParameterUtils::ParameterDomain> domains_;
    std::vector<int> slots_;
    std::vector<std::uint8_t> driven_;
};

constexpr int kBlockSize = 64;

engine::RenderContext contextFor() {
    return {.sampleRate = 48000.0, .maxBlockSize = kBlockSize, .numChannels = 2};
}

class HostedParameterEditsTest final : public juce::UnitTest {
  public:
    HostedParameterEditsTest() : juce::UnitTest("Hosted Parameter Edits", "magda") {}

    void runTest() override {
        testEveryMovedSlotIsReported();
        testADrivenSlotIsReportedAsTheHostsOwn();
        testAWriteOfOursComesBackAsTheHostsOwn();
        testAPlanTheDeviceIsNotInDrivesNothing();
        testAOneOffWriteReachesThePluginWithoutTheTable();
        testAWriteTheDeviceCannotTakeIsRefused();
    }

  private:
    /// One device, its plugin, and what it reported.
    struct Rig {
        Rig() {
            auto instance = std::make_unique<StubPlugin>();
            plugin = instance.get();

            device = std::make_unique<adapter::EngineExternalDevice>(std::move(instance),
                                                                     magda::DeviceInfo{}, false);
            device->prepare(contextFor());
            device->listenForPluginEdits(
                [this](adapter::EngineExternalDevice::Observation observation) {
                    observed.push_back(observation);
                });
        }

        /// One block at @p window, which is what tells the device what the
        /// table carries and what it is driving.
        void render(const Window& window) {
            juce::AudioBuffer<float> audio(2, kBlockSize);
            audio.clear();

            const auto params = window.params(kBlockSize);
            engine::DeviceBlock block;
            block.audio = juce::dsp::AudioBlock<float>(audio);
            block.params = params;
            block.block.numSamples = kBlockSize;

            device->process(block);
        }

        StubPlugin* plugin = nullptr;
        std::unique_ptr<adapter::EngineExternalDevice> device;

        std::vector<adapter::EngineExternalDevice::Observation> observed;
    };

    /// Lets the flush the callback queued run.
    static void pumpMessageLoop() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    void testEveryMovedSlotIsReported() {
        beginTest("A parameter nothing addresses is still reported");

        Rig rig;

        // Drive, at slot four. Nothing in any document knows about it, and the
        // knob drawing it still has to hear that it moved.
        rig.plugin->parameters[2]->setValueNotifyingHost(0.8f);
        pumpMessageLoop();

        expect(rig.observed.size() == 1, "The move was reported");
        if (rig.observed.size() == 1) {
            expect(rig.observed.front().slot == 4, "At the plan slot, not the plugin's index");
            expectWithinAbsoluteError(rig.observed.front().normalised, 0.8f, 1.0e-6f);
            expect(!rig.observed.front().hostOwned, "Nothing was driving it");
        }
    }

    void testADrivenSlotIsReportedAsTheHostsOwn() {
        beginTest("A slot a lane plays is reported as the host's own");

        Rig rig;

        Window window;
        window.carry(2, 0.25f, /*driven=*/true);
        rig.render(window);

        // The plugin's own modulation of a slot the host is writing. Taken as
        // a base, it would move the value the lane is offsetting from.
        rig.plugin->parameters[0]->setValueNotifyingHost(0.9f);
        pumpMessageLoop();

        expect(rig.observed.size() == 1, "Still reported, because a knob still draws it");
        if (rig.observed.size() == 1)
            expect(rig.observed.front().hostOwned, "Marked as the host's, so no base takes it");
    }

    void testAWriteOfOursComesBackAsTheHostsOwn() {
        beginTest("The value the host wrote comes back marked as the host's own");

        Rig rig;

        // A knob drag: the table moves the slot, and the plugin answers with a
        // value of its own, which is what a smoothed or quantised readback is.
        rig.plugin->echoesDuringProcess = 0.4f;

        Window window;
        window.carry(2, 0.9f);
        rig.render(window);
        pumpMessageLoop();

        expect(rig.observed.size() == 1, "The readback was reported");
        if (rig.observed.size() == 1)
            expect(rig.observed.front().hostOwned, "As the host's own write coming back");

        // The drag ends. Once no write is in flight the plugin has the slot
        // back, and what it says of it is its own again.
        rig.plugin->echoesDuringProcess.reset();
        rig.observed.clear();
        rig.render(window);
        rig.render(window);

        rig.plugin->parameters[0]->setValueNotifyingHost(0.2f);
        pumpMessageLoop();

        expect(rig.observed.size() == 1, "The edit after the write settled was reported");
        if (rig.observed.size() == 1)
            expect(!rig.observed.front().hostOwned, "And is the plugin's own");
    }

    void testAPlanTheDeviceIsNotInDrivesNothing() {
        beginTest("A device the plan dropped is driving nothing");

        Rig rig;

        Window window;
        window.carry(2, 0.25f, /*driven=*/true);
        rig.render(window);

        // Bypassed while the lane played, and then the lane switched off: the
        // device renders no block, so nothing here would clear what the last
        // block it did render left behind.
        rig.device->forgetDriverState();

        rig.plugin->parameters[0]->setValueNotifyingHost(0.9f);
        pumpMessageLoop();

        expect(rig.observed.size() == 1, "Reported");
        if (rig.observed.size() == 1)
            expect(!rig.observed.front().hostOwned, "And no longer the host's, with the lane gone");
    }

    void testAOneOffWriteReachesThePluginWithoutTheTable() {
        beginTest("A one-off write reaches the plugin with no table entry behind it");

        Rig rig;

        // No block has been rendered and no window carries the slot: an
        // ordinary parameter is the plugin's, and this is a command to it.
        expect(rig.device->writeParameter(2, 0.7f), "The device took the write");
        expectWithinAbsoluteError(rig.plugin->parameters[0]->getValue(), 0.7f, 1.0e-6f);
    }

    void testAWriteTheDeviceCannotTakeIsRefused() {
        beginTest("A write the device cannot take is refused rather than clamped");

        Rig rig;

        expect(!rig.device->writeParameter(0, 0.5f), "The wrapper pair is the model's own");
        expect(!rig.device->writeParameter(2, 1.5f), "A position outside [0, 1] is not one");
        expect(!rig.device->writeParameter(99, 0.5f), "A slot the plugin does not have");
        expectWithinAbsoluteError(rig.plugin->parameters[0]->getValue(), 0.0f, 1.0e-6f);
    }
};

HostedParameterEditsTest hostedParameterEditsTest;

}  // namespace
