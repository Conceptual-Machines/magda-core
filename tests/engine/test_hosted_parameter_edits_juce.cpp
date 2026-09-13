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
        testABurstOfReportsWakesTheHostOnce();
        testADrainAfterTheDeviceIsGoneDoesNothing();
        testAMoveNothingDrivesIsReadback();
        testADrivenSlotIsReportedAsDriven();
        testAMoveInsideTheEditorsGestureIsMarkedAsOne();
        testReadbackBeforeTheFlushKeepsTheGesture();
        testAnEchoOfATableWriteIsReadback();
        testAPlanTheDeviceIsNotInDrivesNothing();
        testAOneOffWriteReachesThePluginWithoutTheTable();
        testAWriteTheDeviceCannotTakeIsRefused();
        testASlotHeldInTheEditorIsNotWrittenOver();
        testADrivenValueSkippedByAGestureLandsOnRelease();
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
            device->listenForPluginEdits([this] { ++wakes; });
        }

        /// What the host's drain does when a wake lands.
        void drain() {
            device->pluginEdits().drain(
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
        int wakes = 0;
    };

    void expectOneReport(const Rig& rig, magda::ObservationSource source, const juce::String& why) {
        expect(rig.observed.size() == 1, "One report");
        if (rig.observed.size() == 1)
            expect(rig.observed.front().source == source, why);
    }

    void testABurstOfReportsWakesTheHostOnce() {
        beginTest("A burst of reports wakes the host once until it drains");

        Rig rig;
        expect(rig.wakes == 1, "Listening wakes once for anything already reported");
        rig.drain();

        for (const auto value : {0.2f, 0.3f, 0.4f})
            rig.plugin->parameters[1]->setValueNotifyingHost(value);
        expect(rig.wakes == 2, "One wake for the burst");

        rig.drain();
        expectOneReport(rig, magda::ObservationSource::Readback, "Coalesced to the latest");
        if (rig.observed.size() == 1)
            expectWithinAbsoluteError(rig.observed.front().normalised, 0.4f, 1.0e-6f);

        rig.plugin->parameters[1]->setValueNotifyingHost(0.5f);
        expect(rig.wakes == 3, "A report after the drain wakes again");
    }

    void testADrainAfterTheDeviceIsGoneDoesNothing() {
        beginTest("A drain after the device is gone does nothing");

        Rig rig;
        const auto source = rig.device->pluginEdits();
        rig.plugin->parameters[0]->setValueNotifyingHost(0.9f);
        rig.device.reset();

        const auto drained = source.drain([&rig](adapter::EngineExternalDevice::Observation seen) {
            rig.observed.push_back(seen);
        });
        expect(!drained, "The source says the device has gone");
        expect(rig.observed.empty(), "Nothing reported from it");
    }

    void testAMoveNothingDrivesIsReadback() {
        beginTest("A parameter nothing addresses is still reported, as readback");

        Rig rig;

        // Drive, at slot four. Nothing in any document knows about it, and the
        // knob drawing it still has to hear that it moved.
        rig.plugin->parameters[2]->setValueNotifyingHost(0.8f);
        rig.drain();

        expectOneReport(rig, magda::ObservationSource::Readback,
                        "No gesture around it, so nothing may take it as a base");
        if (rig.observed.size() == 1) {
            expect(rig.observed.front().slot == 4, "At the plan slot, not the plugin's index");
            expectWithinAbsoluteError(rig.observed.front().normalised, 0.8f, 1.0e-6f);
        }
    }

    void testADrivenSlotIsReportedAsDriven() {
        beginTest("A slot a lane plays is reported as driven");

        Rig rig;

        Window window;
        window.carry(2, 0.25f, /*driven=*/true);
        rig.render(window);

        // The plugin's own modulation of a slot the host is writing. Taken as
        // a base, it would move the value the lane is offsetting from.
        rig.plugin->parameters[0]->setValueNotifyingHost(0.9f);
        rig.drain();

        expectOneReport(rig, magda::ObservationSource::Driven,
                        "Still reported, because a knob still draws it");
    }

    void testAMoveInsideTheEditorsGestureIsMarkedAsOne() {
        beginTest("A move inside the plugin's editor gesture is marked as one");

        Rig rig;

        auto& gain = *rig.plugin->parameters[0];
        gain.beginChangeGesture();
        gain.setValueNotifyingHost(0.6f);
        rig.drain();

        expectOneReport(rig, magda::ObservationSource::EditorGesture,
                        "A person moving it in the editor");

        gain.endChangeGesture();
        rig.observed.clear();

        gain.setValueNotifyingHost(0.4f);
        rig.drain();

        expectOneReport(rig, magda::ObservationSource::Readback,
                        "After the gesture ends, a move is readback again");
    }

    void testReadbackBeforeTheFlushKeepsTheGesture() {
        beginTest("Readback landing before the flush does not erase an editor gesture");

        Rig rig;

        auto& gain = *rig.plugin->parameters[0];
        gain.beginChangeGesture();
        gain.setValueNotifyingHost(0.6f);
        gain.endChangeGesture();

        // A quantised answer to the same move, reported before the flush runs.
        gain.setValueNotifyingHost(0.61f);
        rig.drain();

        expect(rig.observed.size() == 2, "The gesture and the readback after it");
        if (rig.observed.size() == 2) {
            expect(rig.observed[0].source == magda::ObservationSource::EditorGesture,
                   "The gesture first, so a base still takes it");
            expectWithinAbsoluteError(rig.observed[0].normalised, 0.6f, 1.0e-6f);
            expect(rig.observed[1].source == magda::ObservationSource::Readback,
                   "Then the latest value, for the knob");
            expectWithinAbsoluteError(rig.observed[1].normalised, 0.61f, 1.0e-6f);
        }
    }

    void testAnEchoOfATableWriteIsReadback() {
        beginTest("A plugin answering a table write is readback, not an edit");

        Rig rig;

        // A knob drag on a slot the table carries: the plugin answers with a
        // value of its own, which is what a smoothed or quantised readback is.
        rig.plugin->echoesDuringProcess = 0.4f;

        Window window;
        window.carry(2, 0.9f);
        rig.render(window);
        rig.drain();

        expectOneReport(rig, magda::ObservationSource::Readback,
                        "No gesture, so no base takes the plugin's answer");
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
        rig.drain();

        expectOneReport(rig, magda::ObservationSource::Readback,
                        "No longer driven, with the lane gone");
    }

    void testAOneOffWriteReachesThePluginWithoutTheTable() {
        beginTest("A one-off write reaches the plugin with no table entry behind it");

        Rig rig;

        // No block has been rendered and no window carries the slot: an
        // ordinary parameter is the plugin's, and this is a command to it.
        expect(rig.device->writeParameter(2, 0.7f), "The device took the write");
        expectWithinAbsoluteError(rig.plugin->parameters[0]->getValue(), 0.7f, 1.0e-6f);

        const auto read = rig.device->readParameter(2);
        expect(read.has_value(), "And reads it back");
        if (read.has_value())
            expectWithinAbsoluteError(*read, 0.7f, 1.0e-6f);
    }

    void testAWriteTheDeviceCannotTakeIsRefused() {
        beginTest("A write the device cannot take is refused rather than clamped");

        Rig rig;

        expect(!rig.device->writeParameter(0, 0.5f), "The wrapper pair is the model's own");
        expect(!rig.device->writeParameter(2, 1.5f), "A position outside [0, 1] is not one");
        expect(!rig.device->writeParameter(99, 0.5f), "A slot the plugin does not have");
        expect(!rig.device->readParameter(0).has_value(), "Nothing of the wrapper pair to read");
        expectWithinAbsoluteError(rig.plugin->parameters[0]->getValue(), 0.0f, 1.0e-6f);
    }

    void testASlotHeldInTheEditorIsNotWrittenOver() {
        beginTest("A slot held in the plugin's editor is not written over");

        Rig rig;

        auto& gain = *rig.plugin->parameters[0];
        gain.setValue(0.5f);
        gain.beginChangeGesture();

        expect(!rig.device->writeParameter(2, 0.7f), "A one-off write waits for the gesture");

        Window moved;
        moved.carry(2, 0.9f);
        rig.render(moved);
        expectWithinAbsoluteError(gain.getValue(), 0.5f, 1.0e-6f);

        // Letting go does not bring back a table value the gesture moved past.
        gain.endChangeGesture();
        rig.render(moved);
        expectWithinAbsoluteError(gain.getValue(), 0.5f, 1.0e-6f);

        Window movedAgain;
        movedAgain.carry(2, 0.3f);
        rig.render(movedAgain);
        expectWithinAbsoluteError(gain.getValue(), 0.3f, 1.0e-6f);
    }

    void testADrivenValueSkippedByAGestureLandsOnRelease() {
        beginTest("A driven value skipped during an editor gesture lands on release");

        Rig rig;

        auto& gain = *rig.plugin->parameters[0];
        gain.setValue(0.5f);
        gain.beginChangeGesture();

        Window flat;
        flat.carry(2, 0.9f, /*driven=*/true);
        rig.render(flat);
        expectWithinAbsoluteError(gain.getValue(), 0.5f, 1.0e-6f);

        // The lane stays flat, so only an owed value would bring it back.
        gain.endChangeGesture();
        rig.render(flat);
        expectWithinAbsoluteError(gain.getValue(), 0.9f, 1.0e-6f);
    }
};

HostedParameterEditsTest hostedParameterEditsTest;

}  // namespace
