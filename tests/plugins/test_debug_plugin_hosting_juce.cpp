// The debug plugin through JUCE's real VST3 hosting, and through Tracktion's (#3155).

#include <juce_audio_processors/juce_audio_processors.h>

#include "DebugPluginHost.hpp"
#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;

namespace {

std::unique_ptr<juce::AudioPluginInstance> open(juce::AudioPluginFormatManager& formats,
                                                const juce::PluginDescription& description,
                                                juce::String& error) {
    return formats.createPluginInstance(description, 48000.0, 512, error);
}

}  // namespace

class DebugPluginHostingTest final : public juce::UnitTest {
  public:
    DebugPluginHostingTest() : juce::UnitTest("Debug Plugin Hosting Tests", "magda") {}

    void runTest() override {
        const auto description = test::debugPluginDescription();
        beginTest("The debug plugin is found in the build tree");
        expect(description.has_value(), "no plugin at " + juce::String(MAGDA_DEBUG_PLUGIN_PATH));
        if (!description.has_value())
            return;

        formats_.addFormat(std::make_unique<juce::VST3PluginFormat>());

        testPlaysTheHeldNote(*description);
        testRefusesPastMaxInstances(*description);
        testTracktionShowsARefusedInstanceAsFailed(*description);
    }

  private:
    void testPlaysTheHeldNote(const juce::PluginDescription& description) {
        beginTest("With no faults it plays the held note");

        juce::String error;
        auto instance = open(formats_, description, error);
        expect(instance != nullptr, error);
        if (instance == nullptr)
            return;

        instance->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        instance->processBlock(buffer, midi);

        expect(buffer.getMagnitude(0, 512) > 0.1f);
        expect(instance->hasEditor());
        instance->releaseResources();
    }

    void testRefusesPastMaxInstances(const juce::PluginDescription& description) {
        beginTest("max-instances refuses the instance past the limit, and only while it holds");

        test::ScopedDebugPluginFaults faults("max-instances=1");
        juce::String error;

        auto first = open(formats_, description, error);
        expect(first != nullptr, error);

        auto second = open(formats_, description, error);
        expect(second == nullptr);
        expect(error.isNotEmpty());

        first.reset();
        auto third = open(formats_, description, error);
        expect(third != nullptr, error);
    }

    void testTracktionShowsARefusedInstanceAsFailed(const juce::PluginDescription& description) {
        beginTest("Tracktion: a second instance the plugin refuses shows as failed to load");

        test::ScopedJuceTestState state;
        test::ScopedDebugPluginFaults faults("max-instances=1");

        auto& wrapper = test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr);
        if (bridge == nullptr)
            return;

        auto& known = wrapper.getEngine()->getPluginManager().knownPluginList;
        known.addType(description);

        auto& tm = TrackManager::getInstance();
        const auto firstTrack = tm.createTrack("Debug 1");
        bridge->createAudioTrack(firstTrack, "Debug 1");
        const auto secondTrack = tm.createTrack("Debug 2");
        bridge->createAudioTrack(secondTrack, "Debug 2");

        const auto first = tm.addDeviceToTrack(firstTrack, test::debugPluginDevice(description, 0));
        const auto second =
            tm.addDeviceToTrack(secondTrack, test::debugPluginDevice(description, 0));

        const auto* loaded = tm.getDevice(firstTrack, first);
        const auto* refused = tm.getDevice(secondTrack, second);
        expect(loaded != nullptr && loaded->loadState == DeviceLoadState::Loaded);
        expect(refused != nullptr && refused->loadState == DeviceLoadState::Failed);

        tm.deleteTrack(secondTrack);
        tm.deleteTrack(firstTrack);
        known.removeType(description);
    }

    juce::AudioPluginFormatManager formats_;
};

static DebugPluginHostingTest debugPluginHostingTest;
