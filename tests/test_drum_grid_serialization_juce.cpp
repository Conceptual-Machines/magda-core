#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/TracktionHelpers.hpp"
#include "magda/daw/audio/plugins/DrumGridPlugin.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace te = tracktion;

namespace {

te::Plugin::Ptr createCustomPlugin(te::Edit& edit, const juce::String& type) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, type, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

// Mirror of PluginManager::captureAllPluginStates for an internal (non-external)
// plugin: flush, copy, strip TE ids + modifier assignments, serialize to XML.
juce::String capturePluginStateXml(te::Plugin& plugin) {
    plugin.flushPluginStateToValueTree();
    auto stateCopy = plugin.state.createCopy();
    magda::stripTracktionIdsRecursive(stateCopy);
    magda::stripModifierAssignmentsRecursive(stateCopy);
    if (auto xml = stateCopy.createXml())
        return xml->toString();
    return {};
}

}  // namespace

class DrumGridPadChainSerializationTest final : public juce::UnitTest {
  public:
    DrumGridPadChainSerializationTest()
        : juce::UnitTest("DrumGrid Pad Chain Serialization", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testCompiledDrumVoiceRoundTrip(); });
        magda::test::runWithCleanJuceState([this] { testSamplerPadRoundTrip(); });
        magda::test::runWithCleanJuceState([this] { testCompiledVoiceWithFxRoundTrip(); });
        magda::test::runWithCleanJuceState([this] { testModelReloadRestoresCompiledDrumPads(); });
        magda::test::runWithCleanJuceState(
            [this] { testProjectFileReloadRestoresCompiledDrumPads(); });
    }

  private:
    using DrumGridPlugin = magda::daw::audio::DrumGridPlugin;

    // Save/reload cycle at the DrumGrid level: capture the plugin state the way
    // PluginManager::captureAllPluginStates does, then restore it into a fresh
    // DrumGrid the way the deferred restore in loadDeviceAsPlugin does.
    te::Plugin::Ptr roundTrip(te::Edit& edit, DrumGridPlugin& source) {
        auto savedXml = capturePluginStateXml(source);
        expect(savedXml.isNotEmpty(), "Captured DrumGrid state must not be empty");

        auto xml = juce::parseXML(savedXml);
        expect(xml != nullptr, "Captured state must parse as XML");
        if (xml == nullptr)
            return nullptr;

        auto savedState = juce::ValueTree::fromXml(*xml);
        expect(savedState.isValid(), "Captured state must produce a valid ValueTree");
        if (!savedState.isValid())
            return nullptr;

        auto restoredPlugin = createCustomPlugin(edit, DrumGridPlugin::xmlTypeName);
        auto* restored = dynamic_cast<DrumGridPlugin*>(restoredPlugin.get());
        expect(restored != nullptr, "Fresh DrumGrid must be created for restore");
        if (restored == nullptr)
            return nullptr;

        restored->restorePluginStateFromValueTree(savedState);
        return restoredPlugin;
    }

    void testCompiledDrumVoiceRoundTrip() {
        beginTest("Compiled drum voice (Kick) round-trips through pad-chain save/restore");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin = createCustomPlugin(*edit, DrumGridPlugin::xmlTypeName);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "DrumGrid plugin must be created");
        if (drumGrid == nullptr)
            return;

        constexpr int padIndex = 0;
        drumGrid->loadInternalPluginToPad(padIndex, "magda_kick");

        const int midiNote = DrumGridPlugin::baseNote + padIndex;
        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Kick pad chain must exist");
        if (chain == nullptr || chain->plugins.empty() || chain->plugins[0] == nullptr)
            return;

        auto kick = chain->plugins[0];
        expectEquals(kick->getPluginType(), juce::String("magda_kick"),
                     "Pad voice must be the compiled kick");

        // Tweak a knob so we can verify parameter state survives the round-trip.
        auto* bodyPitch = kick->getAutomatableParameterByID("magda_kick_body_pitch").get();
        expect(bodyPitch != nullptr, "Kick must expose the body_pitch host parameter");
        if (bodyPitch == nullptr)
            return;
        bodyPitch->setParameterFromHost(0.85f, juce::sendNotificationSync);

        const int savedDeviceId = drumGrid->getPluginDeviceId(chain->index, 0);
        expect(savedDeviceId >= 0, "Pad voice must have a MAGDA device id before save");

        auto restoredPtr = roundTrip(*edit, *drumGrid);
        auto* restored = dynamic_cast<DrumGridPlugin*>(restoredPtr.get());
        if (restored == nullptr)
            return;

        auto* restoredChain = restored->getChainForNote(midiNote);
        expect(restoredChain != nullptr, "Restored DrumGrid must have the kick pad chain");
        if (restoredChain == nullptr)
            return;

        expectEquals(static_cast<int>(restoredChain->plugins.size()), 1,
                     "Restored pad chain must have exactly one plugin");
        if (restoredChain->plugins.empty() || restoredChain->plugins[0] == nullptr) {
            expect(false, "Restored pad voice must exist");
            return;
        }

        auto restoredKick = restoredChain->plugins[0];
        expectEquals(restoredKick->getPluginType(), juce::String("magda_kick"),
                     "Restored pad voice must still be the compiled kick");

        const float restoredValue =
            static_cast<float>(restoredKick->state.getProperty("magda_kick_body_pitch", -1.0f));
        expectWithinAbsoluteError(restoredValue, 0.85f, 1.0e-4f,
                                  "Tweaked kick parameter must survive the round-trip");

        auto* restoredParam =
            restoredKick->getAutomatableParameterByID("magda_kick_body_pitch").get();
        expect(restoredParam != nullptr, "Restored kick must expose the body_pitch parameter");
        if (restoredParam != nullptr)
            expectWithinAbsoluteError(restoredParam->getCurrentBaseValue(), 0.85f, 1.0e-4f,
                                      "Restored parameter must report the saved value");

        expectEquals(restored->getPluginDeviceId(restoredChain->index, 0), savedDeviceId,
                     "Pad voice device id must survive the round-trip");
    }

    void testSamplerPadRoundTrip() {
        beginTest("Sampler pad chain round-trips (control case)");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin = createCustomPlugin(*edit, DrumGridPlugin::xmlTypeName);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "DrumGrid plugin must be created");
        if (drumGrid == nullptr)
            return;

        constexpr int padIndex = 3;
        drumGrid->loadSampleToPad(padIndex, juce::File());

        const int midiNote = DrumGridPlugin::baseNote + padIndex;
        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Sampler pad chain must exist");
        if (chain == nullptr || chain->plugins.empty())
            return;

        auto restoredPtr = roundTrip(*edit, *drumGrid);
        auto* restored = dynamic_cast<DrumGridPlugin*>(restoredPtr.get());
        if (restored == nullptr)
            return;

        auto* restoredChain = restored->getChainForNote(midiNote);
        expect(restoredChain != nullptr, "Restored DrumGrid must have the sampler pad chain");
        if (restoredChain == nullptr)
            return;

        expectEquals(static_cast<int>(restoredChain->plugins.size()), 1,
                     "Restored sampler pad chain must have exactly one plugin");
        if (!restoredChain->plugins.empty() && restoredChain->plugins[0] != nullptr)
            expectEquals(restoredChain->plugins[0]->getPluginType(),
                         juce::String(magda::daw::audio::MagdaSamplerPlugin::xmlTypeName),
                         "Restored pad voice must still be the sampler");
    }

    void testCompiledVoiceWithFxRoundTrip() {
        beginTest("Compiled voice plus FX keeps chain order through save/restore");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin = createCustomPlugin(*edit, DrumGridPlugin::xmlTypeName);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "DrumGrid plugin must be created");
        if (drumGrid == nullptr)
            return;

        constexpr int padIndex = 1;
        drumGrid->loadInternalPluginToPad(padIndex, "magda_hat");

        const int midiNote = DrumGridPlugin::baseNote + padIndex;
        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Hat pad chain must exist");
        if (chain == nullptr || chain->plugins.empty())
            return;

        drumGrid->addInternalPluginToChain(chain->index, "magda_filter");
        expectEquals(static_cast<int>(chain->plugins.size()), 2,
                     "FX must append after the pad voice");

        auto restoredPtr = roundTrip(*edit, *drumGrid);
        auto* restored = dynamic_cast<DrumGridPlugin*>(restoredPtr.get());
        if (restored == nullptr)
            return;

        auto* restoredChain = restored->getChainForNote(midiNote);
        expect(restoredChain != nullptr, "Restored DrumGrid must have the hat pad chain");
        if (restoredChain == nullptr)
            return;

        expectEquals(static_cast<int>(restoredChain->plugins.size()), 2,
                     "Restored pad chain must keep both the voice and the FX");
        if (restoredChain->plugins.size() == 2 && restoredChain->plugins[0] != nullptr &&
            restoredChain->plugins[1] != nullptr) {
            expectEquals(restoredChain->plugins[0]->getPluginType(), juce::String("magda_hat"),
                         "Voice must stay first in the restored chain");
            expectEquals(restoredChain->plugins[1]->getPluginType(), juce::String("magda_filter"),
                         "FX must stay second in the restored chain");
        }
    }

    // Full model-level cycle through TrackManager + AudioBridge: the same path a
    // real project save/reload takes (loadDeviceAsPlugin -> instrument wrapping ->
    // deferred DrumGrid restore).
    void testModelReloadRestoresCompiledDrumPads() {
        beginTest("Model-level reload restores compiled drum pads");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "Audio bridge must be available");
        if (bridge == nullptr)
            return;

        auto& tm = magda::TrackManager::getInstance();
        tm.clearAllTracks();
        tm.setAudioEngine(&wrapper);

        const auto trackId = tm.createTrack("Drums");

        magda::DeviceInfo dgDevice;
        dgDevice.name = "Drum Grid";
        dgDevice.format = magda::PluginFormat::Internal;
        dgDevice.pluginId = DrumGridPlugin::xmlTypeName;
        dgDevice.isInstrument = true;
        dgDevice.deviceType = magda::DeviceType::Instrument;
        dgDevice.canReceiveMidi = true;

        const auto deviceId = tm.addDeviceToTrack(trackId, dgDevice);
        expect(deviceId != magda::INVALID_DEVICE_ID, "Drum Grid device must be added");
        if (deviceId == magda::INVALID_DEVICE_ID) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        const auto devicePath = magda::ChainNodePath::topLevelDevice(trackId, deviceId);
        bridge->syncTrackPlugins(trackId);

        auto plugin = bridge->getPlugin(devicePath);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "Drum Grid runtime plugin must be created");
        if (drumGrid == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        constexpr int padIndex = 0;
        drumGrid->loadInternalPluginToPad(padIndex, "magda_kick");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        const int midiNote = DrumGridPlugin::baseNote + padIndex;
        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Kick pad chain must exist before save");
        if (chain == nullptr || chain->plugins.empty() || chain->plugins[0] == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        auto* bodyPitch =
            chain->plugins[0]->getAutomatableParameterByID("magda_kick_body_pitch").get();
        expect(bodyPitch != nullptr, "Kick must expose the body_pitch host parameter");
        if (bodyPitch != nullptr)
            bodyPitch->setParameterFromHost(0.85f, juce::sendNotificationSync);

        // Project save: capture plugin state into the model.
        bridge->captureAllPluginStates();

        auto* devInfo = tm.getDeviceInChainByPath(devicePath);
        expect(devInfo != nullptr, "Drum Grid DeviceInfo must exist in the model");
        if (devInfo == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        expect(devInfo->pluginState.contains("magda_kick"),
               "Captured Drum Grid state must contain the compiled kick voice");
        expect(devInfo->pluginState.contains("magda_kick_body_pitch"),
               "Captured Drum Grid state must contain the tweaked kick parameter");

        const magda::DeviceInfo savedDevice = *devInfo;

        // Project reload: tear the model down, rebuild the track from the saved
        // DeviceInfo, and let the bridge recreate the runtime plugins.
        tm.clearAllTracks();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        const auto trackId2 = tm.createTrack("Drums");
        const auto deviceId2 = tm.addDeviceToTrack(trackId2, savedDevice);
        expect(deviceId2 != magda::INVALID_DEVICE_ID, "Reloaded Drum Grid device must be added");
        if (deviceId2 == magda::INVALID_DEVICE_ID) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        bridge->syncTrackPlugins(trackId2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        auto plugin2 = bridge->getPlugin(magda::ChainNodePath::topLevelDevice(trackId2, deviceId2));
        auto* reloaded = dynamic_cast<DrumGridPlugin*>(plugin2.get());
        expect(reloaded != nullptr, "Reloaded Drum Grid runtime plugin must be created");
        if (reloaded == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        auto* reloadedChain = reloaded->getChainForNote(midiNote);
        expect(reloadedChain != nullptr, "Reloaded Drum Grid must have the kick pad chain");
        if (reloadedChain != nullptr) {
            expectEquals(static_cast<int>(reloadedChain->plugins.size()), 1,
                         "Reloaded pad chain must have exactly one plugin");
            if (!reloadedChain->plugins.empty() && reloadedChain->plugins[0] != nullptr) {
                expectEquals(reloadedChain->plugins[0]->getPluginType(), juce::String("magda_kick"),
                             "Reloaded pad voice must still be the compiled kick");
                const float restoredValue = static_cast<float>(
                    reloadedChain->plugins[0]->state.getProperty("magda_kick_body_pitch", -1.0f));
                expectWithinAbsoluteError(restoredValue, 0.85f, 1.0e-4f,
                                          "Tweaked kick parameter must survive the reload");
            }
        }

        tm.clearAllTracks();
        tm.setAudioEngine(nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    // Full project-file cycle: capture, serialize to a .mgd on disk, clear the
    // session, reload through the real loadAndStage -> commitStaged path with
    // the engine wired, and verify the compiled kick pad comes back.
    void testProjectFileReloadRestoresCompiledDrumPads() {
        beginTest("Project-file reload restores compiled drum pads");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "Audio bridge must be available");
        if (bridge == nullptr)
            return;

        auto& tm = magda::TrackManager::getInstance();
        auto& cm = magda::ClipManager::getInstance();
        cm.clearAllClips();
        tm.clearAllTracks();
        tm.setAudioEngine(&wrapper);

        const auto trackId = tm.createTrack("Drums");

        magda::DeviceInfo dgDevice;
        dgDevice.name = "Drum Grid";
        dgDevice.format = magda::PluginFormat::Internal;
        dgDevice.pluginId = DrumGridPlugin::xmlTypeName;
        dgDevice.isInstrument = true;
        dgDevice.deviceType = magda::DeviceType::Instrument;
        dgDevice.canReceiveMidi = true;

        const auto deviceId = tm.addDeviceToTrack(trackId, dgDevice);
        const auto devicePath = magda::ChainNodePath::topLevelDevice(trackId, deviceId);
        bridge->syncTrackPlugins(trackId);

        auto plugin = bridge->getPlugin(devicePath);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "Drum Grid runtime plugin must be created");
        if (drumGrid == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        constexpr int padIndex = 0;
        drumGrid->loadInternalPluginToPad(padIndex, "magda_kick");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        const int midiNote = DrumGridPlugin::baseNote + padIndex;
        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Kick pad chain must exist before save");
        if (chain != nullptr && !chain->plugins.empty() && chain->plugins[0] != nullptr) {
            if (auto* bodyPitch =
                    chain->plugins[0]->getAutomatableParameterByID("magda_kick_body_pitch").get())
                bodyPitch->setParameterFromHost(0.85f, juce::sendNotificationSync);
        }

        // Save the way ProjectManager does: capture states, then serialize.
        bridge->captureAllPluginStates();

        auto scratchDir =
            juce::File(
                juce::SystemStats::getEnvironmentVariable(
                    "TMPDIR",
                    juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
                .getChildFile("magda_juce_tests");
        scratchDir.createDirectory();
        auto target = scratchDir.getNonexistentChildFile("drum_grid_project", ".mgd");
        juce::TemporaryFile projectFile(target);

        const auto& info = magda::ProjectManager::getInstance().getCurrentProjectInfo();
        expect(magda::ProjectSerializer::saveToFile(projectFile.getFile(), info),
               "Project must save to file");

        // Simulate close, then reload through the real staged-load path.
        cm.clearAllClips();
        tm.clearAllTracks();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        magda::StagedProjectData staged;
        expect(magda::ProjectSerializer::loadAndStage(projectFile.getFile(), staged),
               "Project must load and stage");
        magda::ProjectSerializer::commitStaged(staged);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        // Find the reloaded Drum Grid via the model.
        auto* reloadedTrack = tm.getTrack(trackId);
        expect(reloadedTrack != nullptr, "Reloaded track must exist");
        DrumGridPlugin* reloaded = nullptr;
        if (reloadedTrack != nullptr) {
            for (const auto& element : reloadedTrack->chain.fxChainElements) {
                if (!magda::isDevice(element))
                    continue;
                const auto& dev = magda::getDevice(element);
                auto p = bridge->getPlugin(
                    magda::ChainNodePath::topLevelDevice(reloadedTrack->id, dev.id));
                reloaded = dynamic_cast<DrumGridPlugin*>(p.get());
                if (reloaded != nullptr)
                    break;
            }
        }

        expect(reloaded != nullptr, "Reloaded Drum Grid runtime plugin must exist");
        if (reloaded != nullptr) {
            auto* reloadedChain = reloaded->getChainForNote(midiNote);
            expect(reloadedChain != nullptr, "Reloaded Drum Grid must have the kick pad chain");
            if (reloadedChain != nullptr) {
                expectEquals(static_cast<int>(reloadedChain->plugins.size()), 1,
                             "Reloaded pad chain must have exactly one plugin");
                if (!reloadedChain->plugins.empty() && reloadedChain->plugins[0] != nullptr) {
                    expectEquals(reloadedChain->plugins[0]->getPluginType(),
                                 juce::String("magda_kick"),
                                 "Reloaded pad voice must still be the compiled kick");
                    const float restoredValue =
                        static_cast<float>(reloadedChain->plugins[0]->state.getProperty(
                            "magda_kick_body_pitch", -1.0f));
                    expectWithinAbsoluteError(restoredValue, 0.85f, 1.0e-4f,
                                              "Tweaked kick parameter must survive the reload");
                }
            }
        }

        cm.clearAllClips();
        tm.clearAllTracks();
        tm.setAudioEngine(nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }
};

static DrumGridPadChainSerializationTest drumGridPadChainSerializationTest;
