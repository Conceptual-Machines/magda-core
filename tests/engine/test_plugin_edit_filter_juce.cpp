#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/ParameterUtils.hpp"
#include "exec/EngineDevice.hpp"
#include "magda/daw/audio/plugins/engine/EngineExternalDevice.hpp"
#include "param/ParamBlock.hpp"

/**
 * Which of a plugin's own edits reach the model (#2633).
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
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

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

class PluginEditFilterTest final : public juce::UnitTest {
  public:
    PluginEditFilterTest() : juce::UnitTest("Plugin Edit Filter", "magda") {}

    void runTest() override {
        testAnUnaddressedSlotReachesNothing();
        testAnAddressedSlotReachesTheModel();
        testADrivenSlotIsNotReported();
        testALearnGestureHearsEverything();
    }

  private:
    /// One device, its plugin, and what its edits reached.
    struct Rig {
        Rig() {
            auto instance = std::make_unique<StubPlugin>();
            plugin = instance.get();

            device = std::make_unique<adapter::EngineExternalDevice>(std::move(instance),
                                                                     magda::DeviceInfo{}, false);
            device->prepare(contextFor());
            device->listenForPluginEdits(
                [this](int slot, float value) { reported.emplace_back(slot, value); });
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

        std::vector<std::pair<int, float>> reported;
    };

    /// Lets the flush the callback queued run.
    static void pumpMessageLoop() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    void testAnUnaddressedSlotReachesNothing() {
        beginTest("A slot nothing addresses is dropped where the plugin reports it");

        Rig rig;
        const std::vector<int> addressed{2};
        rig.device->setAddressedSlots(addressed);

        // Drive, at slot four: the model mirrors no value for it, so its own
        // movement has nowhere to go.
        rig.plugin->parameters[2]->setValueNotifyingHost(0.8f);
        pumpMessageLoop();

        expect(rig.reported.empty(), "Nothing was reported for an unaddressed slot");
    }

    void testAnAddressedSlotReachesTheModel() {
        beginTest("The same slot reaches the model once something addresses it");

        Rig rig;
        const std::vector<int> addressed{2, 4};
        rig.device->setAddressedSlots(addressed);

        rig.plugin->parameters[2]->setValueNotifyingHost(0.8f);
        pumpMessageLoop();

        expect(rig.reported.size() == 1, "One report for the slot that is now addressed");
        if (rig.reported.size() == 1) {
            expect(rig.reported.front().first == 4, "At the plan slot, not the plugin's index");
            expectWithinAbsoluteError(rig.reported.front().second, 0.8f, 1.0e-6f);
        }
    }

    void testADrivenSlotIsNotReported() {
        beginTest("A slot a lane is playing does not report what the plugin does to it");

        Rig rig;
        const std::vector<int> addressed{2};
        rig.device->setAddressedSlots(addressed);

        Window window;
        window.carry(2, 0.25f, /*driven=*/true);
        rig.render(window);

        // The plugin's own modulation of a slot the host is writing. Reported,
        // it would be read back as the base value the lane is offsetting.
        rig.plugin->parameters[0]->setValueNotifyingHost(0.9f);
        pumpMessageLoop();

        expect(rig.reported.empty(), "Nothing was reported while the lane played");

        // The lane stops and the same edit is the user's own again.
        Window free;
        free.carry(2, 0.25f);
        rig.render(free);

        rig.plugin->parameters[0]->setValueNotifyingHost(0.6f);
        pumpMessageLoop();

        expect(rig.reported.size() == 1, "The edit after the lane stopped was reported");
    }

    void testALearnGestureHearsEverything() {
        beginTest("A learn gesture hears a slot nothing addresses");

        Rig rig;
        const std::vector<int> addressed{2};
        rig.device->setAddressedSlots(addressed);

        rig.device->listenToEveryEdit(true);
        rig.plugin->parameters[2]->setValueNotifyingHost(0.8f);
        pumpMessageLoop();

        expect(rig.reported.size() == 1, "The gesture heard the unaddressed slot");

        rig.device->listenToEveryEdit(false);
        rig.plugin->parameters[2]->setValueNotifyingHost(0.3f);
        pumpMessageLoop();

        expect(rig.reported.size() == 1, "And stopped hearing it when the gesture ended");
    }
};

PluginEditFilterTest pluginEditFilterTest;

}  // namespace
