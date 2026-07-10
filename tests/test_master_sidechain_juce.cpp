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
        bool hasSourceMonitor = false;
        if (sourceTrack) {
            for (int i = 0; i < sourceTrack->pluginList.size(); ++i) {
                if (dynamic_cast<magda::SidechainMonitorPlugin*>(sourceTrack->pluginList[i])) {
                    hasSourceMonitor = true;
                    break;
                }
            }
        }
        expect(hasSourceMonitor, "A master sidechain destination should retain its source monitor");

        bridge->getPluginManager().triggerSidechainNoteOn(sourceTrackId);

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

        juce::AudioBuffer<float> buffer(2, 3);
        buffer.setSample(0, 0, 1.0f);
        buffer.setSample(1, 0, 1.0f);
        buffer.setSample(0, 1, 1.0f);
        buffer.setSample(1, 1, -1.0f);
        buffer.setSample(0, 2, 0.75f);
        buffer.setSample(1, 2, 0.25f);

        te::MidiMessageArray midi;
        te::PluginRenderContext context(
            &buffer, juce::AudioChannelSet::canonicalChannelSet(2), 0, buffer.getNumSamples(),
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
    }
};

static MasterSidechainTest masterSidechainTest;
