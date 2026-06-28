#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/DeviceMeteringManager.hpp"
#include "magda/daw/audio/plugin_manager/PluginManager.hpp"
#include "magda/daw/audio/plugins/DrumGridPlugin.hpp"
#include "magda/daw/audio/plugins/InstrumentMeterTapPlugin.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/MidiReceivePlugin.hpp"
#include "magda/daw/audio/plugins/PolyStepSequencerPlugin.hpp"
#include "magda/daw/audio/plugins/SidechainMonitorPlugin.hpp"
#include "magda/daw/audio/plugins/StepSequencerPlugin.hpp"
#include "magda/daw/audio/racks/InstrumentRackManager.hpp"
#include "magda/daw/audio/transport/StepClock.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/components/chain/slot/StepSequencerClipExport.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace te = tracktion;

namespace {

te::Plugin::Ptr createCustomPlugin(te::Edit& edit, const juce::String& type) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, type, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

bool hasConnection(te::RackType& rackType, te::EditItemID src, int srcPin, te::EditItemID dst,
                   int dstPin) {
    for (auto* conn : rackType.getConnections()) {
        if (conn->sourceID.get() == src && conn->sourcePin.get() == srcPin &&
            conn->destID.get() == dst && conn->destPin.get() == dstPin) {
            return true;
        }
    }
    return false;
}

bool hasMidiOutputConnection(te::RackType& rackType) {
    for (auto* conn : rackType.getConnections()) {
        if (conn->destID.get().isInvalid() && conn->destPin.get() == 0)
            return true;
    }
    return false;
}

}  // namespace

class MidiSignalRoutingTest final : public juce::UnitTest {
  public:
    MidiSignalRoutingTest() : juce::UnitTest("MIDI Signal Routing Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testInstrumentRackPassesMidiThrough(); });
        magda::test::runWithCleanJuceState([this] { testStepSequencerDefaultsToMidiThru(); });
        magda::test::runWithCleanJuceState(
            [this] { testStepSequencerMidiThruMirrorsDeviceState(); });
        magda::test::runWithCleanJuceState(
            [this] { testStepSequencerMidiThruPassesWhileStopped(); });
        magda::test::runWithCleanJuceState([this] { testStepSequencerStepRecordingStopsAtEnd(); });
        magda::test::runWithCleanJuceState(
            [this] { testPolyStepSequencerStepRecordingStopsAtEnd(); });
        magda::test::runWithCleanJuceState(
            [this] { testStepSequencerStepEditsSurviveStateSave(); });
        magda::test::runWithCleanJuceState([this] { testStepSequencerCopyPatternToClipboard(); });
        magda::test::runWithCleanJuceState(
            [this] { testPolyStepSequencerCopyPatternToClipboard(); });
        magda::test::runWithCleanJuceState([this] { testRackSyncWiresNestedRackAsGraphNode(); });
        magda::test::runWithCleanJuceState(
            [this] { testRackSyncKeepsRawMidiPassthroughOrderedThroughProcessors(); });
        magda::test::runWithCleanJuceState(
            [this] { testRackSyncMergesRawAndPluginMidiForThruProducer(); });
        magda::test::runWithCleanJuceState([this] { testRackTypeRejectsRecursiveRackInstances(); });
        magda::test::runWithCleanJuceState([this] { testRackSyncBypassedChainPreservesMidi(); });
        magda::test::runWithCleanJuceState(
            [this] { testRackSyncMutedChainSuppressesMidiOutput(); });
        magda::test::runWithCleanJuceState(
            [this] { testRackSyncRoutesChainOutputIndexToRackPins(); });
        magda::test::runWithCleanJuceState(
            [this] { testRackSyncInstrumentInjectsAudioAndPassesMidiToFx(); });
        magda::test::runWithCleanJuceState(
            [this] { testRackSyncMidiSidechainFxDoesNotReceiveChainMidi(); });
        magda::test::runWithCleanJuceState(
            [this] { testTopLevelMidiSidechainFxGetsExclusiveSourceMidiAndRestoresChainMidi(); });
        magda::test::runWithCleanJuceState(
            [this] { testMoveDeviceIntoRackRemovesTrackRuntimePlugin(); });
        magda::test::runWithCleanJuceState([this] { testPostFxRoutesAfterFxBeforeFader(); });
        magda::test::runWithCleanJuceState(
            [this] { testDrumGridLoadsInternalInstrumentAsPadVoice(); });
    }

  private:
    void testInstrumentRackPassesMidiThrough() {
        beginTest("Instrument wrapper passes audio and MIDI through");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto instrument =
            createCustomPlugin(*edit, magda::daw::audio::MagdaSamplerPlugin::xmlTypeName);
        expect(instrument != nullptr, "Sampler plugin must be created");
        if (!instrument)
            return;

        magda::InstrumentRackManager rackManager(*edit);
        auto rackPlugin = rackManager.wrapInstrument(instrument);
        expect(rackPlugin != nullptr, "Instrument must be wrapped");

        auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
        expect(rackInstance != nullptr, "Wrapper plugin must be a RackInstance");
        if (rackInstance == nullptr || rackInstance->type == nullptr)
            return;

        auto& rackType = *rackInstance->type;
        const auto rackIO = te::EditItemID();
        const auto synthId = instrument->itemID;
        te::Plugin* meterTap = nullptr;
        for (auto* plugin : rackType.getPlugins()) {
            if (plugin && plugin->getPluginType() ==
                              magda::daw::audio::InstrumentMeterTapPlugin::xmlTypeName) {
                meterTap = plugin;
                break;
            }
        }
        expect(meterTap != nullptr, "Instrument wrapper must contain an explicit meter tap");
        if (meterTap == nullptr)
            return;
        const auto meterTapId = meterTap->itemID;

        expect(hasConnection(rackType, rackIO, 0, synthId, 0),
               "Rack MIDI input must feed the instrument");
        expect(hasConnection(rackType, synthId, 0, rackIO, 0),
               "Instrument's own MIDI output must always reach the rack output so a "
               "wrapped sequencer/arp triggers downstream instruments");
        expect(hasConnection(rackType, rackIO, 0, rackIO, 0),
               "Raw MIDI in thru is on by default (preserves historic passthrough)");

        // Toggling MIDI in thru adds/removes only the raw-input passthrough.
        rackManager.recordWrapping(magda::ChainNodePath::topLevelDevice(0, 1), rackInstance->type,
                                   instrument, rackPlugin);
        rackManager.setMidiInThru(1, true);
        expect(hasConnection(rackType, rackIO, 0, rackIO, 0),
               "Enabling MIDI in thru wires the raw-input passthrough");
        expect(hasConnection(rackType, synthId, 0, rackIO, 0),
               "Plugin MIDI output stays wired when in-thru is enabled");
        rackManager.setMidiInThru(1, false);
        expect(!hasConnection(rackType, rackIO, 0, rackIO, 0),
               "Disabling MIDI in thru removes the raw-input passthrough");
        expect(hasConnection(rackType, synthId, 0, rackIO, 0),
               "Plugin MIDI output stays wired when in-thru is disabled");

        expect(hasConnection(rackType, rackIO, 1, rackIO, 1),
               "Rack must preserve left audio passthrough");
        expect(hasConnection(rackType, rackIO, 2, rackIO, 2),
               "Rack must preserve right audio passthrough");
        expect(hasConnection(rackType, synthId, 1, meterTapId, 1),
               "Instrument left audio must feed the meter tap");
        expect(hasConnection(rackType, synthId, 2, meterTapId, 2),
               "Instrument right audio must feed the meter tap");
        expect(hasConnection(rackType, meterTapId, 1, rackIO, 1),
               "Meter tap left audio must feed rack output");
        expect(hasConnection(rackType, meterTapId, 2, rackIO, 2),
               "Meter tap right audio must feed rack output");

        constexpr magda::DeviceId deviceId = 4242;
        const auto devicePath = magda::ChainNodePath::topLevelDevice(1, deviceId);
        magda::DeviceMeteringManager metering;
        magda::DeviceMeteringManager::registerForEdit(*edit, &metering);

        rackManager.recordWrapping(devicePath, rackInstance->type, instrument, rackPlugin, false,
                                   2);

        auto* typedTap = dynamic_cast<magda::daw::audio::InstrumentMeterTapPlugin*>(meterTap);
        expect(typedTap != nullptr, "Meter tap must be the MAGDA tap plugin");
        expect(typedTap == nullptr || typedTap->getDeviceId() == deviceId,
               "Meter tap must be bound to the wrapped device id");

        juce::AudioBuffer<float> buffer(2, 8);
        buffer.clear();
        buffer.setSample(0, 0, 0.5f);
        buffer.setSample(1, 0, -0.25f);
        metering.setGain(devicePath, 0.5f);

        te::MidiMessageArray midi;
        te::PluginRenderContext rc(
            &buffer, juce::AudioChannelSet::canonicalChannelSet(2), 0, buffer.getNumSamples(),
            &midi, 0.0,
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(1.0)),
            false, false, false, false);
        typedTap->applyToBuffer(rc);

        magda::DeviceMeteringManager::DeviceMeterData levels;
        metering.updateAllClients();
        expect(metering.getLatestLevels(devicePath, levels), "Meter levels should exist");
        expectWithinAbsoluteError(buffer.getSample(0, 0), 0.25f, 0.0001f,
                                  "Meter tap should apply device gain to left channel");
        expectWithinAbsoluteError(buffer.getSample(1, 0), -0.125f, 0.0001f,
                                  "Meter tap should apply device gain to right channel");
        expectWithinAbsoluteError(levels.peakL, 0.25f, 0.0001f,
                                  "Meter tap should publish post-gain left peak");
        expectWithinAbsoluteError(levels.peakR, 0.125f, 0.0001f,
                                  "Meter tap should publish post-gain right peak");

        buffer.clear();
        buffer.setSample(0, 0, 0.5f);
        buffer.setSample(1, 0, -0.25f);
        metering.clear();
        typedTap->applyToBuffer(rc);
        expectWithinAbsoluteError(buffer.getSample(0, 0), 0.25f, 0.0001f,
                                  "Meter tap storage should survive manager entry removal");
        expectWithinAbsoluteError(buffer.getSample(1, 0), -0.125f, 0.0001f,
                                  "Meter tap storage should survive manager clear");

        typedTap->setDeviceId(magda::INVALID_DEVICE_ID);
        buffer.clear();
        buffer.setSample(0, 0, 0.5f);
        buffer.setSample(1, 0, -0.25f);
        typedTap->applyToBuffer(rc);
        expectWithinAbsoluteError(buffer.getSample(0, 0), 0.5f, 0.0001f,
                                  "Unbound meter tap should pass audio through unchanged");
        expectWithinAbsoluteError(buffer.getSample(1, 0), -0.25f, 0.0001f,
                                  "Unbound meter tap should pass audio through unchanged");

        magda::DeviceMeteringManager::unregisterForEdit(*edit);
    }

    static magda::DeviceInfo makeInternalDevice(magda::DeviceId id, const juce::String& name,
                                                const juce::String& pluginId) {
        magda::DeviceInfo device;
        device.id = id;
        device.name = name;
        device.format = magda::PluginFormat::Internal;
        device.pluginId = pluginId;
        return device;
    }

    static magda::DeviceInfo makeInternalInstrument(magda::DeviceId id, const juce::String& name,
                                                    const juce::String& pluginId) {
        auto device = makeInternalDevice(id, name, pluginId);
        device.isInstrument = true;
        device.deviceType = magda::DeviceType::Instrument;
        return device;
    }

    static int countTrackPluginMappings(te::AudioTrack* track, magda::PluginManager& pluginManager,
                                        magda::DeviceId deviceId) {
        if (track == nullptr)
            return 0;

        int count = 0;
        for (int i = 0; i < track->pluginList.size(); ++i) {
            if (pluginManager.getDeviceIdForPlugin(track->pluginList[i]) == deviceId)
                ++count;
        }
        return count;
    }

    static int countTrackPluginPathMappings(te::AudioTrack* track,
                                            magda::PluginManager& pluginManager,
                                            const magda::ChainNodePath& devicePath) {
        if (track == nullptr)
            return 0;

        int count = 0;
        for (int i = 0; i < track->pluginList.size(); ++i) {
            if (pluginManager.getDevicePathForPlugin(track->pluginList[i]) == devicePath)
                ++count;
        }
        return count;
    }

    static int pluginIndex(te::AudioTrack* track, te::Plugin* plugin) {
        return track != nullptr && plugin != nullptr ? track->pluginList.indexOf(plugin) : -1;
    }

    te::RackType* syncTestRack(magda::RackInfo& rack, magda::TrackId trackId) {
        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return nullptr;

        auto& rackSync = bridge->getPluginManager().getRackSyncManager();
        rackSync.removeRack(rack.id);
        auto rackPlugin = rackSync.syncRack(trackId, rack);
        expect(rackPlugin != nullptr, "Rack sync must create a rack plugin");
        auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
        expect(rackInstance != nullptr, "Synced rack plugin must be a RackInstance");
        return rackInstance != nullptr ? rackInstance->type.get() : nullptr;
    }

    void testRackSyncWiresNestedRackAsGraphNode() {
        beginTest("Rack sync wires nested rack as an ordered graph node");

        magda::RackInfo outer;
        outer.id = 9100;
        outer.name = "Outer";

        magda::ChainInfo outerChain;
        outerChain.id = 9101;

        auto before = makeInternalDevice(9102, "Before Delay", "delay");
        auto after = makeInternalDevice(9103, "After Reverb", "reverb");

        magda::RackInfo nested;
        nested.id = 9104;
        nested.name = "Nested";

        magda::ChainInfo nestedChain;
        nestedChain.id = 9105;
        auto nestedDevice = makeInternalDevice(9106, "Nested Lowpass", "lowpass");
        nestedChain.elements.push_back(magda::makeDeviceElement(nestedDevice));
        nested.chains.push_back(std::move(nestedChain));

        outerChain.elements.push_back(magda::makeDeviceElement(before));
        outerChain.elements.push_back(magda::makeRackElement(nested));
        outerChain.elements.push_back(magda::makeDeviceElement(after));
        outer.chains.push_back(std::move(outerChain));

        auto* rackType = syncTestRack(outer, 910);
        if (!rackType)
            return;

        auto& rackSync = magda::test::getSharedEngine()
                             .getAudioBridge()
                             ->getPluginManager()
                             .getRackSyncManager();
        auto* beforePlugin = rackSync.getInnerPlugin(before.id);
        auto* afterPlugin = rackSync.getInnerPlugin(after.id);
        auto* nestedDevicePlugin = rackSync.getInnerPlugin(nestedDevice.id);
        expect(beforePlugin != nullptr, "Before device should be loaded into the outer rack");
        expect(afterPlugin != nullptr, "After device should be loaded into the outer rack");
        expect(nestedDevicePlugin != nullptr, "Nested rack device should be loaded recursively");
        if (!beforePlugin || !afterPlugin || !nestedDevicePlugin)
            return;

        te::RackInstance* nestedInstance = nullptr;
        for (auto* plugin : rackType->getPlugins()) {
            auto* candidate = dynamic_cast<te::RackInstance*>(plugin);
            if (candidate != nullptr) {
                nestedInstance = candidate;
                break;
            }
        }
        expect(nestedInstance != nullptr, "Outer rack should contain a nested RackInstance node");
        if (!nestedInstance)
            return;

        expect(hasConnection(*rackType, beforePlugin->itemID, 1, nestedInstance->itemID, 1),
               "Outer chain left audio should flow into nested rack");
        expect(hasConnection(*rackType, nestedInstance->itemID, 1, afterPlugin->itemID, 1),
               "Nested rack left audio should flow into following device");
        expect(hasConnection(*rackType, beforePlugin->itemID, 0, nestedInstance->itemID, 0),
               "Outer chain MIDI should flow into nested rack");
        expect(hasConnection(*rackType, nestedInstance->itemID, 0, afterPlugin->itemID, 0),
               "Nested rack MIDI should flow into following device");

        expect(nestedInstance->type != nullptr, "Nested RackInstance should own a RackType");
        if (nestedInstance->type != nullptr) {
            const auto rackIO = te::EditItemID();
            expect(hasConnection(*nestedInstance->type, rackIO, 1, nestedDevicePlugin->itemID, 1),
                   "Nested rack input should feed its first device");
            expect(hasConnection(*nestedInstance->type, rackIO, 0, nestedDevicePlugin->itemID, 0),
                   "Nested rack MIDI input should feed its first device");
        }

        rackSync.removeRack(outer.id);
    }

    void testRackSyncKeepsRawMidiPassthroughOrderedThroughProcessors() {
        beginTest("Rack sync keeps raw MIDI passthrough ordered through processors");

        magda::RackInfo rack;
        rack.id = 9150;
        rack.name = "Ordered MIDI Passthrough Rack";

        magda::ChainInfo chain;
        chain.id = 9151;
        auto first = makeInternalDevice(9152, "First MIDI FX", "magda_filter");
        auto second = makeInternalDevice(9153, "Second MIDI FX", "magda_filter");
        first.canReceiveMidi = true;
        second.canReceiveMidi = true;
        chain.elements.push_back(magda::makeDeviceElement(first));
        chain.elements.push_back(magda::makeDeviceElement(second));
        rack.chains.push_back(std::move(chain));

        auto* rackType = syncTestRack(rack, 915);
        if (!rackType)
            return;

        auto& rackSync = magda::test::getSharedEngine()
                             .getAudioBridge()
                             ->getPluginManager()
                             .getRackSyncManager();
        auto* firstPlugin = rackSync.getInnerPlugin(first.id);
        auto* secondPlugin = rackSync.getInnerPlugin(second.id);
        expect(firstPlugin != nullptr, "First processor should be loaded into the rack");
        expect(secondPlugin != nullptr, "Second processor should be loaded into the rack");
        if (!firstPlugin || !secondPlugin)
            return;

        const auto rackIO = te::EditItemID();
        expect(hasConnection(*rackType, rackIO, 0, firstPlugin->itemID, 0),
               "Rack MIDI input should feed the first processor");
        expect(hasConnection(*rackType, firstPlugin->itemID, 0, secondPlugin->itemID, 0),
               "Raw MIDI passthrough should advance through the first processor");
        expect(!hasConnection(*rackType, rackIO, 0, secondPlugin->itemID, 0),
               "Second processor should not bypass the ordered MIDI graph");
        expect(hasConnection(*rackType, secondPlugin->itemID, 0, rackIO, 0),
               "Rack MIDI output should come from the final passthrough processor");

        rackSync.removeRack(rack.id);
    }

    void testRackSyncMergesRawAndPluginMidiForThruProducer() {
        beginTest("Rack sync merges raw and plugin MIDI for thru producers");

        magda::RackInfo rack;
        rack.id = 9160;
        rack.name = "MIDI Merge Rack";

        magda::ChainInfo chain;
        chain.id = 9161;
        auto sequencer = makeInternalDevice(9162, "Step Sequencer",
                                            magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        sequencer.canReceiveMidi = true;
        sequencer.producesMidi = true;
        sequencer.midiInThru = true;
        chain.elements.push_back(magda::makeDeviceElement(sequencer));
        rack.chains.push_back(std::move(chain));

        auto* rackType = syncTestRack(rack, 916);
        if (!rackType)
            return;

        auto& rackSync = magda::test::getSharedEngine()
                             .getAudioBridge()
                             ->getPluginManager()
                             .getRackSyncManager();
        auto* sequencerPlugin = rackSync.getInnerPlugin(sequencer.id);
        expect(sequencerPlugin != nullptr, "MIDI producer should be loaded into the rack");
        if (!sequencerPlugin)
            return;

        const auto rackIO = te::EditItemID();
        expect(hasConnection(*rackType, rackIO, 0, sequencerPlugin->itemID, 0),
               "Rack MIDI input should feed the MIDI producer");
        expect(hasConnection(*rackType, rackIO, 0, rackIO, 0),
               "MIDI thru should keep raw input connected to rack output");
        expect(hasConnection(*rackType, sequencerPlugin->itemID, 0, rackIO, 0),
               "MIDI thru should also connect plugin MIDI output to rack output");

        rackSync.removeRack(rack.id);
    }

    void testRackTypeRejectsRecursiveRackInstances() {
        beginTest("RackType rejects recursive rack instances");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto rackA = edit->getRackList().addNewRack();
        auto rackB = edit->getRackList().addNewRack();
        expect(rackA != nullptr, "Rack A must be created");
        expect(rackB != nullptr, "Rack B must be created");
        if (rackA == nullptr || rackB == nullptr)
            return;

        rackA->rackName = "A";
        rackB->rackName = "B";

        auto instanceB = edit->getPluginCache().createNewPlugin(te::RackInstance::create(*rackB));
        expect(instanceB != nullptr, "Rack B instance must be created");
        expect(rackA->addPlugin(instanceB, {0.5f, 0.5f}, false),
               "Rack A should accept acyclic nested rack B");

        auto instanceA = edit->getPluginCache().createNewPlugin(te::RackInstance::create(*rackA));
        expect(instanceA != nullptr, "Rack A instance must be created");
        expect(!rackB->addPlugin(instanceA, {0.5f, 0.5f}, false),
               "Rack B should reject adding A because A already contains B");

        auto selfInstanceA =
            edit->getPluginCache().createNewPlugin(te::RackInstance::create(*rackA));
        expect(selfInstanceA != nullptr, "Self rack instance must be created");
        expect(!rackA->addPlugin(selfInstanceA, {0.5f, 0.5f}, false),
               "Rack A should reject a RackInstance of itself");
    }

    void testRackSyncBypassedChainPreservesMidi() {
        beginTest("Rack sync bypassed chain preserves audio and MIDI passthrough");

        magda::RackInfo rack;
        rack.id = 9200;
        rack.name = "Bypass Rack";
        magda::ChainInfo chain;
        chain.id = 9201;
        chain.bypassed = true;
        chain.elements.push_back(
            magda::makeDeviceElement(makeInternalDevice(9202, "Delay", "delay")));
        rack.chains.push_back(std::move(chain));

        auto* rackType = syncTestRack(rack, 920);
        if (!rackType)
            return;
        const auto rackIO = te::EditItemID();
        expect(hasConnection(*rackType, rackIO, 1, rackIO, 1),
               "Bypassed chain should preserve left audio passthrough");
        expect(hasConnection(*rackType, rackIO, 2, rackIO, 2),
               "Bypassed chain should preserve right audio passthrough");
        expect(hasConnection(*rackType, rackIO, 0, rackIO, 0),
               "Bypassed chain should preserve MIDI passthrough");

        magda::test::getSharedEngine()
            .getAudioBridge()
            ->getPluginManager()
            .getRackSyncManager()
            .removeRack(rack.id);
    }

    void testRackSyncMutedChainSuppressesMidiOutput() {
        beginTest("Rack sync muted chain suppresses MIDI output");

        magda::RackInfo rack;
        rack.id = 9300;
        rack.name = "Muted Rack";
        magda::ChainInfo chain;
        chain.id = 9301;
        chain.muted = true;
        chain.elements.push_back(
            magda::makeDeviceElement(makeInternalDevice(9302, "Delay", "delay")));
        rack.chains.push_back(std::move(chain));

        auto* rackType = syncTestRack(rack, 930);
        if (!rackType)
            return;

        expect(!hasMidiOutputConnection(*rackType),
               "Muted chain should not emit MIDI to the rack output");

        magda::test::getSharedEngine()
            .getAudioBridge()
            ->getPluginManager()
            .getRackSyncManager()
            .removeRack(rack.id);
    }

    void testRackSyncRoutesChainOutputIndexToRackPins() {
        beginTest("Rack sync routes chain outputIndex to rack output pins");

        magda::RackInfo rack;
        rack.id = 9400;
        rack.name = "Output Rack";

        magda::ChainInfo chain;
        chain.id = 9401;
        chain.outputIndex = 1;
        auto device = makeInternalDevice(9402, "Delay", "delay");
        chain.elements.push_back(magda::makeDeviceElement(device));
        rack.chains.push_back(std::move(chain));

        auto* rackType = syncTestRack(rack, 940);
        if (!rackType)
            return;

        auto& rackSync = magda::test::getSharedEngine()
                             .getAudioBridge()
                             ->getPluginManager()
                             .getRackSyncManager();
        auto* devicePlugin = rackSync.getInnerPlugin(device.id);
        expect(devicePlugin != nullptr, "Device should be loaded into the rack");
        if (!devicePlugin)
            return;

        te::VolumeAndPanPlugin* volPan = nullptr;
        for (auto* plugin : rackType->getPlugins()) {
            if (auto* candidate = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                volPan = candidate;
                break;
            }
        }

        expect(volPan != nullptr, "Chain should have a VolumeAndPan endpoint");
        if (!volPan)
            return;

        const auto rackIO = te::EditItemID();
        expect(rackType->getOutputNames().size() > 4,
               "Rack type should expose the requested secondary output pair");
        expect(hasConnection(*rackType, volPan->itemID, 1, rackIO, 3),
               "Chain left output should route to output pair 2 left");
        expect(hasConnection(*rackType, volPan->itemID, 2, rackIO, 4),
               "Chain right output should route to output pair 2 right");

        rackSync.removeRack(rack.id);
    }

    void testRackSyncInstrumentInjectsAudioAndPassesMidiToFx() {
        beginTest("Rack sync treats instruments as audio injectors with MIDI thru");

        magda::RackInfo rack;
        rack.id = 9500;
        rack.name = "Instrument FX Rack";

        magda::ChainInfo chain;
        chain.id = 9501;
        auto instrument = makeInternalInstrument(9502, "4OSC Synth", "4OSC Synth");
        auto triggeredFx = makeInternalDevice(9503, "Filter", "magda_filter");
        triggeredFx.canReceiveMidi = true;
        chain.elements.push_back(magda::makeDeviceElement(instrument));
        chain.elements.push_back(magda::makeDeviceElement(triggeredFx));
        rack.chains.push_back(std::move(chain));

        auto* rackType = syncTestRack(rack, 950);
        if (!rackType)
            return;

        auto& rackSync = magda::test::getSharedEngine()
                             .getAudioBridge()
                             ->getPluginManager()
                             .getRackSyncManager();
        auto* instrumentPlugin = rackSync.getInnerPlugin(instrument.id);
        auto* fxPlugin = rackSync.getInnerPlugin(triggeredFx.id);
        expect(instrumentPlugin != nullptr, "Instrument should be loaded into the rack");
        expect(fxPlugin != nullptr, "Triggered FX should be loaded into the rack");
        if (!instrumentPlugin || !fxPlugin)
            return;

        const auto rackIO = te::EditItemID();
        expect(hasConnection(*rackType, rackIO, 0, instrumentPlugin->itemID, 0),
               "Rack MIDI input should feed the instrument");
        expect(hasConnection(*rackType, rackIO, 0, fxPlugin->itemID, 0),
               "Rack MIDI input should continue to downstream MIDI-triggered FX");
        expect(!hasConnection(*rackType, instrumentPlugin->itemID, 0, fxPlugin->itemID, 0),
               "Instrument MIDI output should not be required for MIDI thru");

        expect(!hasConnection(*rackType, rackIO, 1, instrumentPlugin->itemID, 1),
               "Rack audio should not be processed by the instrument as if it were FX");
        expect(!hasConnection(*rackType, rackIO, 2, instrumentPlugin->itemID, 2),
               "Rack audio should not be processed by the instrument as if it were FX");
        expect(hasConnection(*rackType, rackIO, 1, fxPlugin->itemID, 1),
               "Rack left audio should reach the first downstream audio processor");
        expect(hasConnection(*rackType, rackIO, 2, fxPlugin->itemID, 2),
               "Rack right audio should reach the first downstream audio processor");
        expect(hasConnection(*rackType, instrumentPlugin->itemID, 1, fxPlugin->itemID, 1),
               "Instrument left audio should be injected into the downstream FX");
        expect(hasConnection(*rackType, instrumentPlugin->itemID, 2, fxPlugin->itemID, 2),
               "Instrument right audio should be injected into the downstream FX");

        rackSync.removeRack(rack.id);
    }

    void testRackSyncMidiSidechainFxDoesNotReceiveChainMidi() {
        beginTest("Rack sync excludes MIDI-sidechained FX from chain MIDI");

        magda::RackInfo rack;
        rack.id = 9600;
        rack.name = "MIDI Sidechain Rack";

        magda::ChainInfo chain;
        chain.id = 9601;
        auto instrument = makeInternalInstrument(9602, "4OSC Synth", "4OSC Synth");
        auto sidechainedFx = makeInternalDevice(9603, "Filter", "magda_filter");
        sidechainedFx.canReceiveMidi = true;
        sidechainedFx.sidechain.type = magda::SidechainConfig::Type::MIDI;
        sidechainedFx.sidechain.sourceTrackId = 12345;
        chain.elements.push_back(magda::makeDeviceElement(instrument));
        chain.elements.push_back(magda::makeDeviceElement(sidechainedFx));
        rack.chains.push_back(std::move(chain));

        auto* rackType = syncTestRack(rack, 960);
        if (!rackType)
            return;

        auto& rackSync = magda::test::getSharedEngine()
                             .getAudioBridge()
                             ->getPluginManager()
                             .getRackSyncManager();
        auto* instrumentPlugin = rackSync.getInnerPlugin(instrument.id);
        auto* fxPlugin = rackSync.getInnerPlugin(sidechainedFx.id);
        expect(instrumentPlugin != nullptr, "Instrument should be loaded into the rack");
        expect(fxPlugin != nullptr, "MIDI-sidechained FX should be loaded into the rack");
        if (!instrumentPlugin || !fxPlugin)
            return;

        const auto rackIO = te::EditItemID();
        expect(hasConnection(*rackType, rackIO, 0, instrumentPlugin->itemID, 0),
               "Rack MIDI input should still feed the instrument");
        expect(!hasConnection(*rackType, rackIO, 0, fxPlugin->itemID, 0),
               "MIDI-sidechained FX should not receive chain MIDI");
        expect(!hasConnection(*rackType, instrumentPlugin->itemID, 0, fxPlugin->itemID, 0),
               "MIDI-sidechained FX should not receive instrument MIDI");
        expect(hasConnection(*rackType, rackIO, 1, fxPlugin->itemID, 1),
               "Sidechain mode should not change normal audio routing");
        expect(hasConnection(*rackType, instrumentPlugin->itemID, 1, fxPlugin->itemID, 1),
               "Sidechain mode should not change instrument audio injection");

        rackSync.removeRack(rack.id);
    }

    void testTopLevelMidiSidechainFxGetsExclusiveSourceMidiAndRestoresChainMidi() {
        beginTest("Top-level MIDI sidechain FX gets exclusive source MIDI and restores chain MIDI");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto sourceTrackId = trackManager.createTrack("MIDI Source");
        const auto destinationTrackId = trackManager.createTrack("Instrument + Sidechain FX");

        auto instrument =
            makeInternalInstrument(magda::INVALID_DEVICE_ID, "4OSC Synth", "4OSC Synth");
        const auto instrumentId = trackManager.addDeviceToTrack(destinationTrackId, instrument);

        auto sidechainedFx =
            makeInternalDevice(magda::INVALID_DEVICE_ID, "Triggered FX", "magda_filter");
        sidechainedFx.canReceiveMidi = true;
        sidechainedFx.sidechain.type = magda::SidechainConfig::Type::MIDI;
        sidechainedFx.sidechain.sourceTrackId = sourceTrackId;
        const auto sidechainedFxId =
            trackManager.addDeviceToTrack(destinationTrackId, sidechainedFx);

        auto downstreamFx =
            makeInternalDevice(magda::INVALID_DEVICE_ID, "Downstream FX", "magda_filter");
        downstreamFx.canReceiveMidi = true;
        const auto downstreamFxId = trackManager.addDeviceToTrack(destinationTrackId, downstreamFx);

        expect(instrumentId != magda::INVALID_DEVICE_ID, "Instrument must be added");
        expect(sidechainedFxId != magda::INVALID_DEVICE_ID, "Sidechained FX must be added");
        expect(downstreamFxId != magda::INVALID_DEVICE_ID, "Downstream FX must be added");
        if (instrumentId == magda::INVALID_DEVICE_ID ||
            sidechainedFxId == magda::INVALID_DEVICE_ID ||
            downstreamFxId == magda::INVALID_DEVICE_ID) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        bridge->syncTrackPlugins(sourceTrackId);
        bridge->syncTrackPlugins(destinationTrackId);

        auto& pluginManager = bridge->getPluginManager();
        auto* sourceTeTrack = bridge->getAudioTrack(sourceTrackId);
        auto* destinationTeTrack = bridge->getAudioTrack(destinationTrackId);
        expect(sourceTeTrack != nullptr, "Source TE track must exist");
        expect(destinationTeTrack != nullptr, "Destination TE track must exist");
        if (!sourceTeTrack || !destinationTeTrack) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        auto targetPlugin = pluginManager.getPlugin(
            magda::ChainNodePath::topLevelDevice(destinationTrackId, sidechainedFxId));
        auto downstreamPlugin = pluginManager.getPlugin(
            magda::ChainNodePath::topLevelDevice(destinationTrackId, downstreamFxId));
        expect(targetPlugin != nullptr, "Sidechained FX plugin must exist");
        expect(downstreamPlugin != nullptr, "Downstream FX plugin must exist");
        if (!targetPlugin || !downstreamPlugin) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        magda::MidiReceivePlugin* sidechainReceive = nullptr;
        magda::MidiReceivePlugin* chainRestore = nullptr;
        magda::SidechainMonitorPlugin* destinationMonitor = nullptr;
        for (int i = 0; i < destinationTeTrack->pluginList.size(); ++i) {
            auto* plugin = destinationTeTrack->pluginList[i];
            if (auto* rx = dynamic_cast<magda::MidiReceivePlugin*>(plugin)) {
                if (rx->getSourceTrackId() == sourceTrackId)
                    sidechainReceive = rx;
                else if (rx->getSourceTrackId() == destinationTrackId)
                    chainRestore = rx;
            } else if (auto* monitor = dynamic_cast<magda::SidechainMonitorPlugin*>(plugin)) {
                if (monitor->getSourceTrackId() == destinationTrackId)
                    destinationMonitor = monitor;
            }
        }

        expect(sidechainReceive != nullptr,
               "Destination track must insert a source MIDI receive before the sidechained FX");
        expect(chainRestore != nullptr,
               "Destination track must insert a restore MIDI receive after the sidechained FX");
        expect(destinationMonitor != nullptr,
               "Destination track must monitor its own chain MIDI for downstream restore");

        if (sidechainReceive && chainRestore) {
            expect(sidechainReceive->getReplaceExistingMidi(),
                   "Source MIDI receive must replace original chain MIDI for the target");
            expect(chainRestore->getReplaceExistingMidi(),
                   "Restore MIDI receive must replace sidechain MIDI for downstream devices");

            const auto sidechainReceiveIndex = pluginIndex(destinationTeTrack, sidechainReceive);
            const auto targetIndex = pluginIndex(destinationTeTrack, targetPlugin.get());
            const auto chainRestoreIndex = pluginIndex(destinationTeTrack, chainRestore);
            const auto downstreamIndex = pluginIndex(destinationTeTrack, downstreamPlugin.get());

            expect(sidechainReceiveIndex >= 0 && targetIndex >= 0 &&
                       sidechainReceiveIndex < targetIndex,
                   "Source MIDI receive must be before the target FX");
            expect(chainRestoreIndex >= 0 && targetIndex >= 0 && chainRestoreIndex > targetIndex,
                   "Chain MIDI restore must be after the target FX");
            expect(downstreamIndex >= 0 && chainRestoreIndex >= 0 &&
                       chainRestoreIndex < downstreamIndex,
                   "Downstream FX must be after restored original chain MIDI");
        }

        magda::SidechainMonitorPlugin* sourceMonitor = nullptr;
        for (int i = 0; i < sourceTeTrack->pluginList.size(); ++i) {
            if (auto* monitor =
                    dynamic_cast<magda::SidechainMonitorPlugin*>(sourceTeTrack->pluginList[i])) {
                if (monitor->getSourceTrackId() == sourceTrackId) {
                    sourceMonitor = monitor;
                    break;
                }
            }
        }
        expect(sourceMonitor != nullptr, "Source track must monitor MIDI for sidechain broadcast");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testMoveDeviceIntoRackRemovesTrackRuntimePlugin() {
        beginTest("Moving a device into a rack detaches its old track plugin");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Move Runtime");
        const auto rackId = trackManager.addRackToTrack(trackId, "Rack");
        auto* rack = trackManager.getRack(trackId, rackId);
        expect(rack != nullptr, "Rack must exist");
        if (!rack)
            return;

        const auto chainPath = magda::ChainNodePath::chain(trackId, rackId, rack->chains[0].id);
        auto filter = makeInternalDevice(magda::INVALID_DEVICE_ID, "Filter", "magda_filter");
        const auto filterId = trackManager.addDeviceToTrack(trackId, filter);
        expect(filterId != magda::INVALID_DEVICE_ID, "Filter must be added");
        if (filterId == magda::INVALID_DEVICE_ID)
            return;

        bridge->syncTrackPlugins(trackId);
        auto& pluginManager = bridge->getPluginManager();
        auto* teTrack = bridge->getAudioTrack(trackId);
        expectEquals(countTrackPluginMappings(teTrack, pluginManager, filterId), 1,
                     "Filter starts as exactly one top-level TE plugin");

        expect(trackManager.moveChainElement(
                   magda::ChainNodePath::topLevelDevice(trackId, filterId), chainPath, 0),
               "Move into rack must succeed");
        bridge->syncTrackPlugins(trackId);

        expectEquals(countTrackPluginMappings(teTrack, pluginManager, filterId), 0,
                     "Moved rack device must not leave a stale top-level TE plugin");
        expect(pluginManager.getRackSyncManager().getInnerPlugin(filterId) != nullptr,
               "Moved device must exist as a rack inner plugin");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testPostFxRoutesAfterFxBeforeFader() {
        beginTest("Post-FX devices route after the FX chain and before the fader");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Post-FX Routing");
        const auto fxId = trackManager.addDeviceToTrack(
            trackId, makeInternalDevice(magda::INVALID_DEVICE_ID, "FX Filter", "magda_filter"));
        const auto postFxId = trackManager.addDeviceToPostFx(
            trackId, makeInternalDevice(magda::INVALID_DEVICE_ID, "Post Delay", "delay"));
        expect(fxId != magda::INVALID_DEVICE_ID, "FX device must be added");
        expect(postFxId != magda::INVALID_DEVICE_ID, "Post-FX device must be added");
        expectEquals(fxId, postFxId, "FX and post-FX devices should use section-local ids");

        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);
        const auto postFxPath = magda::ChainNodePath::postFxDevice(trackId, postFxId);

        bridge->syncTrackPlugins(trackId);

        auto& pluginManager = bridge->getPluginManager();
        auto* teTrack = bridge->getAudioTrack(trackId);
        expect(teTrack != nullptr, "TE track must exist");
        if (!teTrack) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        int fxIdx = -1, postFxIdx = -1, faderIdx = -1, meterIdx = -1;
        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            auto* plugin = teTrack->pluginList[i];
            const auto devPath = pluginManager.getDevicePathForPlugin(plugin);
            const auto devId = pluginManager.getDeviceIdForPlugin(plugin);
            if (devPath == fxPath)
                fxIdx = i;
            else if (devPath == postFxPath)
                postFxIdx = i;
            // The fader is a VolumeAndPanPlugin that is NOT a MAGDA device
            // (Utility/gain devices are also VolumeAndPanPlugins).
            if (faderIdx < 0 && dynamic_cast<te::VolumeAndPanPlugin*>(plugin) != nullptr &&
                devId == magda::INVALID_DEVICE_ID)
                faderIdx = i;
            if (dynamic_cast<te::LevelMeterPlugin*>(plugin) != nullptr)
                meterIdx = i;
        }

        expect(fxIdx >= 0, "FX plugin must be on the track");
        expect(postFxIdx >= 0, "Post-FX plugin must be on the track");
        expect(faderIdx >= 0, "VolumeAndPan fader must be on the track");
        expect(meterIdx >= 0, "LevelMeter must be on the track");
        expect(fxIdx < postFxIdx, "FX must come before post-FX");
        expect(postFxIdx < faderIdx, "Post-FX must come before the fader");
        expect(faderIdx < meterIdx, "Fader must come before the meter");

        // Removing the post-FX device must drop its TE plugin.
        trackManager.removeDeviceFromChainByPath(postFxPath);
        bridge->syncTrackPlugins(trackId);
        expectEquals(countTrackPluginPathMappings(teTrack, pluginManager, postFxPath), 0,
                     "Removing the post-FX device must remove its TE plugin");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testStepSequencerDefaultsToMidiThru() {
        beginTest("Step sequencer defaults to MIDI thru and restores MIDI thru state");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::StepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        expect(seq->midiThru.get(), "Fresh step sequencers should pass incoming MIDI");

        juce::ValueTree saved(te::IDs::PLUGIN);
        saved.setProperty(te::IDs::type, magda::daw::audio::StepSequencerPlugin::xmlTypeName,
                          nullptr);
        saved.setProperty("seqMidiThru", false, nullptr);
        seq->restorePluginStateFromValueTree(saved);

        expect(!seq->midiThru.get(), "Saved MIDI thru state should be restored");
    }

    void testDrumGridLoadsInternalInstrumentAsPadVoice() {
        beginTest("DrumGrid loads internal instruments as pad voices");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin = createCustomPlugin(*edit, magda::daw::audio::DrumGridPlugin::xmlTypeName);
        auto* drumGrid = dynamic_cast<magda::daw::audio::DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "DrumGrid plugin must be created");
        if (drumGrid == nullptr)
            return;

        constexpr int padIndex = 0;
        drumGrid->loadInternalPluginToPad(padIndex, "magda_polysynth");

        const int midiNote = magda::daw::audio::DrumGridPlugin::baseNote + padIndex;
        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Loading an internal instrument should create the pad chain");
        if (chain == nullptr)
            return;

        expectEquals(static_cast<int>(chain->plugins.size()), 1,
                     "Internal instrument should become the single pad voice");
        expect(chain->plugins[0] != nullptr, "Pad voice plugin must exist");
        if (chain->plugins[0] == nullptr)
            return;
        expect(chain->plugins[0]->getPluginType() == juce::String("magda_polysynth"),
               "Pad voice should be the requested compiled instrument");
        expect(drumGrid->getPluginDeviceId(chain->index, 0) != magda::INVALID_DEVICE_ID,
               "Pad voice should get a stable MAGDA device id");

        drumGrid->addInternalPluginToChain(chain->index, "eq");
        expectEquals(static_cast<int>(chain->plugins.size()), 2,
                     "Internal FX should append after the pad voice");

        drumGrid->loadInternalPluginToPad(padIndex, "magda_fm");
        chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Pad chain should still exist after replacing the voice");
        if (chain == nullptr)
            return;
        expectEquals(static_cast<int>(chain->plugins.size()), 1,
                     "Loading another instrument should replace the pad voice and clear old FX");
        expect(chain->plugins[0] != nullptr &&
                   chain->plugins[0]->getPluginType() == juce::String("magda_fm"),
               "Pad voice should be replaced by the new internal instrument");
    }

    void testStepSequencerMidiThruMirrorsDeviceState() {
        beginTest("Step sequencer MIDI thru mirrors DeviceInfo state");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "Audio bridge must be available");
        if (bridge == nullptr)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("MIDI Thru Mirror");
        auto sequencer = makeInternalDevice(magda::INVALID_DEVICE_ID, "Step Sequencer",
                                            magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        sequencer.canReceiveMidi = true;
        sequencer.producesMidi = true;
        sequencer.midiInThru = false;

        const auto deviceId = trackManager.addDeviceToTrack(trackId, sequencer);
        expect(deviceId != magda::INVALID_DEVICE_ID, "Step sequencer device must be added");
        if (deviceId == magda::INVALID_DEVICE_ID) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        const auto devicePath = magda::ChainNodePath::topLevelDevice(trackId, deviceId);
        bridge->syncTrackPlugins(trackId);

        auto plugin = bridge->getPlugin(devicePath);
        auto* seq = dynamic_cast<magda::daw::audio::StepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Step sequencer runtime plugin must be created");
        if (seq == nullptr) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        expect(!seq->midiThru.get(), "Runtime MIDI thru should load from DeviceInfo::midiInThru");

        trackManager.setDeviceInChainMidiInThruByPath(devicePath, true);
        expect(seq->midiThru.get(),
               "Runtime MIDI thru should follow DeviceInfo when the header control enables it");

        trackManager.setDeviceInChainMidiInThruByPath(devicePath, false);
        expect(!seq->midiThru.get(),
               "Runtime MIDI thru should follow DeviceInfo when the header control disables it");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testStepSequencerMidiThruPassesWhileStopped() {
        beginTest("Step sequencer MIDI thru passes input while transport is stopped");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::StepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        seq->midiThru = true;

        juce::AudioBuffer<float> buffer(2, 8);
        buffer.clear();
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0.0,
                            te::MPESourceID{});

        te::PluginRenderContext rc(
            &buffer, juce::AudioChannelSet::canonicalChannelSet(2), 0, buffer.getNumSamples(),
            &midi, 0.0,
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(1.0)),
            false, false, false, false);
        seq->applyToBuffer(rc);

        bool sawThruNote = false;
        for (auto& msg : midi) {
            if (msg.isNoteOn() && msg.getNoteNumber() == 64)
                sawThruNote = true;
        }
        expect(sawThruNote, "MIDI thru should pass note-ons while transport is stopped");
    }

    void testStepSequencerStepRecordingStopsAtEnd() {
        beginTest("Step sequencer step recording stops after the final step");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::StepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        seq->numSteps = 1;
        seq->setStepRecording(true);

        juce::AudioBuffer<float> buffer(2, 8);
        buffer.clear();
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 65, static_cast<juce::uint8>(100)), 0.0,
                            te::MPESourceID{});

        te::PluginRenderContext rc(
            &buffer, juce::AudioChannelSet::canonicalChannelSet(2), 0, buffer.getNumSamples(),
            &midi, 0.0,
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(1.0)),
            false, false, false, false);
        seq->applyToBuffer(rc);

        expect(!seq->isStepRecording(), "Step recording should stop at the end of the pattern");
        expectEquals(seq->stepRecordPosition_.load(std::memory_order_relaxed), 1,
                     "Step recording position should advance to the pattern end");
    }

    void testPolyStepSequencerStepRecordingStopsAtEnd() {
        beginTest("Poly step sequencer step recording stops after the final step");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::PolyStepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::PolyStepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Poly step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        seq->numSteps = 1;
        seq->setStepRecording(true);

        juce::AudioBuffer<float> buffer(2, 8);
        buffer.clear();
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 65, static_cast<juce::uint8>(100)), 0.0,
                            te::MPESourceID{});
        midi.addMidiMessage(juce::MidiMessage::noteOff(1, 65), 1.0, te::MPESourceID{});

        te::PluginRenderContext rc(
            &buffer, juce::AudioChannelSet::canonicalChannelSet(2), 0, buffer.getNumSamples(),
            &midi, 0.0,
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(1.0)),
            false, false, false, false);
        seq->applyToBuffer(rc);

        expect(!seq->isStepRecording(),
               "Poly step recording should stop at the end of the pattern");
        expectEquals(seq->stepRecordPosition_.load(std::memory_order_relaxed), 1,
                     "Poly step recording position should advance to the pattern end");
    }

    void testStepSequencerStepEditsSurviveStateSave() {
        beginTest("Step sequencer step edits survive ValueTree saves");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::StepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        seq->setStepNote(0, 64);
        seq->setStepGate(0, true);
        seq->setStepNote(1, 67);
        seq->setStepGate(1, false);

        expectEquals(seq->getStep(0).noteNumber, 64, "First edited note should persist");
        expect(seq->getStep(0).gate, "First edited gate should persist");
        expectEquals(seq->getStep(1).noteNumber, 67, "Second edited note should persist");
        expect(!seq->getStep(1).gate, "Second edited gate should persist");

        seq->randomizePattern();
        expect(seq->getStep(0).noteNumber >= 36 && seq->getStep(0).noteNumber < 60,
               "Randomized note should persist after saving the pattern");
    }

    void testStepSequencerCopyPatternToClipboard() {
        beginTest("Step sequencer copy pattern writes MIDI notes to clipboard");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::StepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        seq->numSteps = 4;
        seq->rate = static_cast<int>(magda::daw::audio::StepClock::Rate::Quarter);
        seq->gateLength = 0.5f;
        seq->normalVelocity = 70;
        seq->accentVelocity = 110;
        seq->setStepNote(0, 60);
        seq->setStepGate(0, true);
        seq->setStepAccent(0, false);
        seq->setStepGate(1, false);
        seq->setStepNote(2, 59);
        seq->setStepOctaveShift(2, 1);
        seq->setStepGate(2, true);
        seq->setStepAccent(2, true);
        seq->setStepGate(3, false);

        magda::TrackManager::ScopedListenerMuteForTests muteTrackListeners;
        magda::ClipManager::ScopedListenerMuteForTests muteClipListeners;

        auto& clipManager = magda::ClipManager::getInstance();
        clipManager.setNoteClipboard({});
        clipManager.clearClipboard();
        magda::daw::ui::copyStepSequencerPatternToClipboard(*seq);

        expect(!clipManager.hasNotesInClipboard(),
               "Copying a mono step sequence should not seed note-paste clipboard");
        expect(clipManager.clipboardRequiresTargetTrack(),
               "Copied mono pattern clips should require an explicit paste target track");
        expect(clipManager.hasClipsInClipboard(),
               "Copying a mono step sequence should seed the clip clipboard");
        expect(clipManager.pasteFromClipboard(0.0, magda::INVALID_TRACK_ID).empty(),
               "Copied mono pattern must not paste a ghost clip without a target track");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto targetTrackId = trackManager.createTrack("Step Sequencer Pattern Paste Target",
                                                            magda::TrackType::Audio);
        const auto targetColour = juce::Colour(0xff3366aa);
        trackManager.setTrackColour(targetTrackId, targetColour);

        const auto pastedIds = clipManager.pasteFromClipboard(0.0, targetTrackId);
        expectEquals(static_cast<int>(pastedIds.size()), 1,
                     "Copied mono pattern should paste as one MIDI clip with a target track");
        if (pastedIds.empty())
            return;

        const auto* pastedClip = clipManager.getClip(pastedIds.front());
        expect(pastedClip != nullptr && pastedClip->isMidi(), "Pasted pattern clip should be MIDI");
        if (pastedClip == nullptr || !pastedClip->isMidi())
            return;

        expectWithinAbsoluteError(pastedClip->placement.lengthBeats, 4.0, 0.0001,
                                  "Copied mono pattern clip should keep the full step-sequencer "
                                  "pattern length");
        expect(pastedClip->colour == targetColour,
               "Copied mono pattern clip should inherit the destination track colour");

        const auto& notes = pastedClip->midiNotes;
        expectEquals(static_cast<int>(notes.size()), 2,
                     "Copying a mono step sequence should copy active gated steps only");
        if (notes.size() < 2)
            return;

        expectEquals(notes[0].noteNumber, 60, "First copied note should keep its pitch");
        expectEquals(notes[0].velocity, 70, "First copied note should use normal velocity");
        expectWithinAbsoluteError(notes[0].startBeat, 0.0, 0.0001,
                                  "First copied note should start at beat 0");
        expectWithinAbsoluteError(notes[0].lengthBeats, 0.5, 0.0001,
                                  "Copied note length should apply gate length");

        expectEquals(notes[1].noteNumber, 71,
                     "Second copied note should apply octave shift and clamp pitch");
        expectEquals(notes[1].velocity, 110, "Accented copied note should use accent velocity");
        expectWithinAbsoluteError(notes[1].startBeat, 2.0, 0.0001,
                                  "Second copied note should start at its step beat");
        expectWithinAbsoluteError(notes[1].lengthBeats, 0.5, 0.0001,
                                  "Second copied note should apply gate length");
    }

    void testPolyStepSequencerCopyPatternToClipboard() {
        beginTest("Poly step sequencer copy pattern writes chord notes to clipboard");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::PolyStepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::PolyStepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Poly step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        seq->numSteps = 3;
        seq->rate = static_cast<int>(magda::daw::audio::StepClock::Rate::Quarter);
        seq->gateLength = 0.5f;
        seq->setStepGate(0, true);
        seq->setStepTie(0, false);
        seq->setStepVelocity(0, 90);
        seq->addStepNote(0, 60);
        seq->addStepNote(0, 64, 80);
        seq->setStepGate(1, true);
        seq->setStepTie(1, true);
        seq->setStepGate(2, true);
        seq->setStepTie(2, false);
        seq->setStepVelocity(2, 75);
        seq->addStepNote(2, 67);

        magda::TrackManager::ScopedListenerMuteForTests muteTrackListeners;
        magda::ClipManager::ScopedListenerMuteForTests muteClipListeners;

        auto& clipManager = magda::ClipManager::getInstance();
        clipManager.setNoteClipboard({});
        clipManager.clearClipboard();
        magda::daw::ui::copyPolyStepSequencerPatternToClipboard(*seq);

        expect(!clipManager.hasNotesInClipboard(),
               "Copying a poly sequence should not seed note-paste clipboard");
        expect(clipManager.clipboardRequiresTargetTrack(),
               "Copied poly pattern clips should require an explicit paste target track");
        expect(clipManager.hasClipsInClipboard(),
               "Copying a poly sequence should seed the clip clipboard");
        expect(clipManager.pasteFromClipboard(0.0, magda::INVALID_TRACK_ID).empty(),
               "Copied poly pattern must not paste a ghost clip without a target track");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto targetTrackId = trackManager.createTrack("Poly Sequencer Pattern Paste Target",
                                                            magda::TrackType::Audio);
        const auto targetColour = juce::Colour(0xff8844aa);
        trackManager.setTrackColour(targetTrackId, targetColour);

        const auto pastedIds = clipManager.pasteFromClipboard(0.0, targetTrackId);
        expectEquals(static_cast<int>(pastedIds.size()), 1,
                     "Copied poly pattern should paste as one MIDI clip with a target track");
        if (pastedIds.empty())
            return;

        const auto* pastedClip = clipManager.getClip(pastedIds.front());
        expect(pastedClip != nullptr && pastedClip->isMidi(), "Pasted pattern clip should be MIDI");
        if (pastedClip == nullptr || !pastedClip->isMidi())
            return;

        expectWithinAbsoluteError(pastedClip->placement.lengthBeats, 3.0, 0.0001,
                                  "Copied poly pattern clip should keep the full step-sequencer "
                                  "pattern length");
        expect(pastedClip->colour == targetColour,
               "Copied poly pattern clip should inherit the destination track colour");

        const auto& notes = pastedClip->midiNotes;
        expectEquals(static_cast<int>(notes.size()), 3,
                     "Copying a poly sequence should copy each note in non-tied gated steps");
        if (notes.size() < 3)
            return;

        expectEquals(notes[0].noteNumber, 60, "First chord note should keep its pitch");
        expectEquals(notes[0].velocity, 90, "First chord note should use the step velocity");
        expectWithinAbsoluteError(notes[0].startBeat, 0.0, 0.0001,
                                  "First chord should start at beat 0");
        expectWithinAbsoluteError(notes[0].lengthBeats, 1.5, 0.0001,
                                  "Tied following step should extend chord note length");

        expectEquals(notes[1].noteNumber, 64, "Second chord note should keep its pitch");
        expectEquals(notes[1].velocity, 80, "Per-note velocity override should be copied");
        expectWithinAbsoluteError(notes[1].startBeat, 0.0, 0.0001,
                                  "Second chord note should share the chord start");
        expectWithinAbsoluteError(notes[1].lengthBeats, 1.5, 0.0001,
                                  "Second chord note should share the tied length");

        expectEquals(notes[2].noteNumber, 67, "Later step note should keep its pitch");
        expectEquals(notes[2].velocity, 75, "Later note should use its step velocity");
        expectWithinAbsoluteError(notes[2].startBeat, 2.0, 0.0001,
                                  "Later note should start at its step beat");
        expectWithinAbsoluteError(notes[2].lengthBeats, 0.5, 0.0001,
                                  "Later note should use gate length without tie extension");
    }
};

static MidiSignalRoutingTest midiSignalRoutingTest;
