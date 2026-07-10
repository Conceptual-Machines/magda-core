#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/plugin_manager/PluginManager.hpp"
#include "magda/daw/audio/plugins/SidechainMonitorPlugin.hpp"
#include "magda/daw/audio/plugins/SidechainPlugin.hpp"
#include "magda/daw/core/TrackManager.hpp"

namespace te = tracktion;

namespace {

magda::DeviceInfo makeSidechainDevice() {
    magda::DeviceInfo device;
    device.name = "Sidechain";
    device.pluginId = magda::daw::audio::SidechainPlugin::xmlTypeName;
    device.format = magda::PluginFormat::Internal;
    return device;
}

te::Plugin::Ptr createSidechainPlugin(te::Edit& edit) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, magda::daw::audio::SidechainPlugin::xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

}  // namespace

class MasterSidechainTest final : public juce::UnitTest {
  public:
    MasterSidechainTest() : juce::UnitTest("Master Sidechain Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testMasterModifierAndSourceMonitor(); });
        magda::test::runWithCleanJuceState([this] { testSidesModePreservesMid(); });
    }

  private:
    void testMasterModifierAndSourceMonitor() {
        beginTest("Master sidechain creates a master modifier and source monitor");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto sourceTrackId = trackManager.createTrack("Sidechain source");
        const auto sidechainId =
            trackManager.addDeviceToTrack(magda::MASTER_TRACK_ID, makeSidechainDevice());
        expect(sourceTrackId != magda::INVALID_TRACK_ID, "Source track should be created");
        expect(sidechainId != magda::INVALID_DEVICE_ID,
               "Sidechain should be accepted on the master track");
        if (sourceTrackId == magda::INVALID_TRACK_ID || sidechainId == magda::INVALID_DEVICE_ID) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        const auto path = magda::ChainNodePath::topLevelDevice(magda::MASTER_TRACK_ID, sidechainId);
        trackManager.setSidechainSource(sidechainId, sourceTrackId,
                                        magda::SidechainConfig::Type::MIDI);

        bridge->syncTrackPlugins(sourceTrackId);
        bridge->syncTrackPlugins(magda::MASTER_TRACK_ID);

        auto sidechain = bridge->getPlugin(path);
        expect(dynamic_cast<magda::daw::audio::SidechainPlugin*>(sidechain.get()) != nullptr,
               "Master path should resolve to the Sidechain plugin");

        auto* masterTrack = wrapper.getEdit() ? wrapper.getEdit()->getMasterTrack() : nullptr;
        auto* modifierList = masterTrack ? masterTrack->getModifierList() : nullptr;
        expect(modifierList != nullptr, "Master track must expose a modifier list");

        te::LFOModifier* sidechainLfo = nullptr;
        if (modifierList) {
            for (auto* modifier : modifierList->getModifiers()) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier)) {
                    sidechainLfo = lfo;
                    break;
                }
            }
        }
        expect(sidechainLfo != nullptr, "Master sidechain should create its duck LFO");
        if (sidechainLfo) {
            expect(sidechainLfo->getSkipNativeResync(),
                   "Master LFO should be driven only by its source track");
        }

        auto* sourceTrack = bridge->getAudioTrack(sourceTrackId);
        magda::SidechainMonitorPlugin* sourceMonitor = nullptr;
        if (sourceTrack) {
            for (int i = 0; i < sourceTrack->pluginList.size(); ++i) {
                if (dynamic_cast<magda::SidechainMonitorPlugin*>(sourceTrack->pluginList[i])) {
                    sourceMonitor =
                        dynamic_cast<magda::SidechainMonitorPlugin*>(sourceTrack->pluginList[i]);
                    break;
                }
            }
        }
        expect(sourceMonitor != nullptr,
               "A master sidechain destination should retain its source monitor");

        auto* sidechainPlugin = dynamic_cast<magda::daw::audio::SidechainPlugin*>(sidechain.get());
        if (sourceMonitor && sidechainPlugin && wrapper.getEdit()) {
            beginTest("Master sidechain ducks audio from a source MIDI note");

            constexpr int numSamples = 1024;
            juce::AudioBuffer<float> sourceBuffer(2, numSamples);
            sourceBuffer.clear();
            te::MidiMessageArray sourceMidi;
            sourceMidi.addMidiMessage(
                juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0.0,
                te::MPESourceID{});
            te::PluginRenderContext sourceContext(&sourceBuffer,
                                                  juce::AudioChannelSet::canonicalChannelSet(2), 0,
                                                  numSamples, &sourceMidi, 0.0,
                                                  te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                                                te::TimePosition::fromSeconds(1.0)),
                                                  false, false, false, false);

            sidechainPlugin->reset();
            sourceMonitor->applyToBuffer(sourceContext);
            wrapper.getEdit()->updateModifierTimers(te::TimePosition::fromSeconds(0.0), numSamples);
            sidechainPlugin->updateParameterStreams(te::TimePosition::fromSeconds(0.0));

            expectWithinAbsoluteError(sidechainPlugin->gainParam->getCurrentValue(), 0.0f, 0.0001f,
                                      "Source note should drive the master Sidechain gain target");

            juce::AudioBuffer<float> masterBuffer(2, numSamples);
            masterBuffer.clear();
            for (int sample = 0; sample < numSamples; ++sample) {
                masterBuffer.setSample(0, sample, 1.0f);
                masterBuffer.setSample(1, sample, 1.0f);
            }
            te::MidiMessageArray masterMidi;
            te::PluginRenderContext masterContext(&masterBuffer,
                                                  juce::AudioChannelSet::canonicalChannelSet(2), 0,
                                                  numSamples, &masterMidi, 0.0,
                                                  te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                                                te::TimePosition::fromSeconds(1.0)),
                                                  false, false, false, false);
            sidechainPlugin->applyToBuffer(masterContext);

            expect(masterBuffer.getSample(0, numSamples - 1) < 0.1f,
                   "Master Sidechain should duck the left channel after the source note");
            expect(masterBuffer.getSample(1, numSamples - 1) < 0.1f,
                   "Master Sidechain should duck the right channel after the source note");
        }

        trackManager.removeDeviceFromChainByPath(path);
        bridge->syncTrackPlugins(magda::MASTER_TRACK_ID);
        expect(modifierList == nullptr || modifierList->getModifiers().isEmpty(),
               "Removing a master sidechain should also remove its master modifier");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testSidesModePreservesMid() {
        beginTest("Sides mode ducks only the stereo side component");

        auto& wrapper = magda::test::getSharedEngine();
        auto* edit = wrapper.getEdit();
        expect(edit != nullptr, "Edit must exist");
        if (!edit)
            return;

        auto plugin = createSidechainPlugin(*edit);
        auto* sidechain = dynamic_cast<magda::daw::audio::SidechainPlugin*>(plugin.get());
        expect(sidechain != nullptr, "Sidechain plugin should be constructible");
        if (!sidechain)
            return;

        sidechain->gainParam->setParameterFromHost(0.0f, juce::dontSendNotification);
        sidechain->channelModeParam->setParameterFromHost(
            static_cast<float>(magda::daw::audio::SidechainPlugin::ChannelMode::Sides),
            juce::dontSendNotification);
        sidechain->reset();

        juce::AudioBuffer<float> buffer(4, 3);
        buffer.clear();
        buffer.setSample(0, 0, 1.0f);
        buffer.setSample(1, 0, 1.0f);
        buffer.setSample(0, 1, 1.0f);
        buffer.setSample(1, 1, -1.0f);
        buffer.setSample(0, 2, 0.75f);
        buffer.setSample(1, 2, 0.25f);
        buffer.setSample(2, 0, 0.8f);
        buffer.setSample(3, 0, -0.6f);

        te::MidiMessageArray midi;
        te::PluginRenderContext context(
            &buffer, juce::AudioChannelSet::canonicalChannelSet(4), 0, buffer.getNumSamples(),
            &midi, 0.0,
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(1.0)),
            false, false, false, false);
        sidechain->applyToBuffer(context);

        expectWithinAbsoluteError(buffer.getSample(0, 0), 1.0f, 0.0001f,
                                  "Centre signal should remain untouched");
        expectWithinAbsoluteError(buffer.getSample(1, 0), 1.0f, 0.0001f,
                                  "Centre signal should remain untouched");
        expectWithinAbsoluteError(buffer.getSample(0, 1), 0.0f, 0.0001f,
                                  "Pure side signal should be ducked");
        expectWithinAbsoluteError(buffer.getSample(1, 1), 0.0f, 0.0001f,
                                  "Pure side signal should be ducked");
        expectWithinAbsoluteError(buffer.getSample(0, 2), 0.5f, 0.0001f,
                                  "Mixed signal should retain only its mid component");
        expectWithinAbsoluteError(buffer.getSample(1, 2), 0.5f, 0.0001f,
                                  "Mixed signal should retain only its mid component");
        expectWithinAbsoluteError(buffer.getSample(2, 0), 0.0f, 0.0001f,
                                  "Additional channels should receive the normal duck gain");
        expectWithinAbsoluteError(buffer.getSample(3, 0), 0.0f, 0.0001f,
                                  "Additional channels should receive the normal duck gain");
    }
};

static MasterSidechainTest masterSidechainTest;
