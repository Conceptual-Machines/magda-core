#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <vector>

#include "JuceTestStateGuard.hpp"
#include "PadSyncTestSupport.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/TracktionHelpers.hpp"
#include "magda/daw/audio/plugins/DrumGridPlugin.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionInternalPluginAdapter.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace te = tracktion;

// A Drum Grid's pads are model state, and the plugin is filled from them
// (#2207). These are the engine-side halves of that: what the grid builds when
// it is handed a rack of pads, what a pad plugin's patch does across a save,
// and what a project reload puts back.

namespace {

using DrumGridPlugin = magda::daw::audio::DrumGridPlugin;

te::Plugin::Ptr createCustomPlugin(te::Edit& edit, const juce::String& type) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, type, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

/// The live pad plugin the model device with this id was built for.
te::Plugin::Ptr padPluginFor(DrumGridPlugin& grid, magda::DeviceId deviceId) {
    for (const auto& chain : grid.getChains()) {
        if (chain == nullptr)
            continue;
        for (int i = 0; i < static_cast<int>(chain->plugins.size()); ++i)
            if (grid.getPluginDeviceId(chain->index, i) == deviceId)
                return chain->plugins[static_cast<std::size_t>(i)];
    }
    return {};
}

/// What a project save does to a pad device: its patch comes back off its own
/// plugin, exactly as a track device's does. Which pads exist and what sits on
/// them travels the other way and is never read back.
void capturePadDevice(DrumGridPlugin& grid, magda::DeviceInfo& padDevice) {
    if (auto plugin = padPluginFor(grid, padDevice.id))
        padDevice.pluginState = magda::daw::audio::tracktion_adapter::captureInternalDeviceState(
            *plugin, padDevice.pluginState);
}

// Short sine burst on disk, so a sampler pad has a real file to point at.
std::unique_ptr<juce::TemporaryFile> createTestWavFile() {
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples = 4410;
    juce::AudioBuffer<float> buffer(1, numSamples);
    const auto phaseInc =
        static_cast<float>(440.0 * juce::MathConstants<double>::twoPi / sampleRate);
    float phase = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, 0.5f * std::sin(phase));
        phase += phaseInc;
    }

    auto scratchDir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_juce_tests");
    scratchDir.createDirectory();
    auto temporaryFile = std::make_unique<juce::TemporaryFile>(
        scratchDir.getNonexistentChildFile("drum_grid_pad_sample", ".wav"));

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(temporaryFile->getFile()), sampleRate, 1, 16, {}, 0));
    if (writer == nullptr)
        return nullptr;
    writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    writer.reset();
    return temporaryFile;
}

juce::File projectScratchFile(const juce::String& name) {
    auto scratchDir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_juce_tests");
    scratchDir.createDirectory();
    return scratchDir.getNonexistentChildFile(name, ".mgd");
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
        magda::test::runWithCleanJuceState([this] { testPadRemovedFromModelLeavesTheGrid(); });
        magda::test::runWithCleanJuceState([this] { testModelReloadRestoresCompiledDrumPads(); });
        magda::test::runWithCleanJuceState(
            [this] { testProjectFileReloadRestoresCompiledDrumPads(); });
        magda::test::runWithCleanJuceState(
            [this] { testTrackMoveKeepsSamplerPadsThroughProjectReload(); });
        magda::test::runWithCleanJuceState([this] { testRetiredPadFxRestoresAsSuccessor(); });
        magda::test::runWithCleanJuceState([this] { testPadDeviceCarriesItsPluginsParameters(); });
    }

  private:
    // A grid built from a rack of pads, and the rack that built it.
    struct Grid {
        std::unique_ptr<te::Edit> edit;
        te::Plugin::Ptr plugin;
        DrumGridPlugin* grid = nullptr;
        magda::DeviceInfo device;
    };

    Grid makeGrid(magda::DeviceId deviceId = 1) {
        Grid built;

        auto& wrapper = magda::test::getSharedEngine();
        built.edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(built.edit != nullptr, "Test edit must be created");
        if (!built.edit)
            return built;

        built.plugin = createCustomPlugin(*built.edit, DrumGridPlugin::xmlTypeName);
        built.grid = dynamic_cast<DrumGridPlugin*>(built.plugin.get());
        expect(built.grid != nullptr, "DrumGrid plugin must be created");

        built.device.id = deviceId;
        built.device.pluginId = DrumGridPlugin::xmlTypeName;
        built.device.format = magda::PluginFormat::Internal;
        built.device.isInstrument = true;
        magda::ensurePads(built.device);

        return built;
    }

    /// Put a device on a pad in the model, the way TrackManager::setPadDevice
    /// does, and fill the grid from it.
    void putOnPad(Grid& built, int padIndex, const magda::DeviceInfo& device) {
        auto& pad = magda::ensurePadChain(*built.device.pads.get(), padIndex);
        pad.elements.push_back(device);
        pad.name = device.name;
        built.grid->syncFromModel(*built.device.pads.get(),
                                  magda::test::padPluginFactory(*built.edit));
    }

    /// The pads, into a grid that has never seen them, which is what a reload
    /// does. Nothing is read out of the first grid's state: the model is what
    /// crosses.
    te::Plugin::Ptr rebuildFromModel(Grid& built, DrumGridPlugin** out) {
        auto restoredPlugin = createCustomPlugin(*built.edit, DrumGridPlugin::xmlTypeName);
        auto* restored = dynamic_cast<DrumGridPlugin*>(restoredPlugin.get());
        expect(restored != nullptr, "Fresh DrumGrid must be created for the reload");
        if (restored != nullptr)
            restored->syncFromModel(*built.device.pads.get(),
                                    magda::test::padPluginFactory(*built.edit));
        *out = restored;
        return restoredPlugin;
    }

    magda::DeviceInfo* padDeviceIn(magda::DeviceInfo& gridDevice, magda::DeviceId id) {
        for (auto& pad : gridDevice.pads->chains)
            for (auto& element : pad.elements)
                if (magda::isDevice(element) && magda::getDevice(element).id == id)
                    return &magda::getDevice(element);
        return nullptr;
    }

    // ------------------------------------------------------------------------

    void testCompiledDrumVoiceRoundTrip() {
        beginTest("A compiled drum voice keeps its patch across a save and reload");

        auto built = makeGrid();
        if (built.grid == nullptr)
            return;

        constexpr int padIndex = 0;
        constexpr magda::DeviceId voiceId = 100;
        putOnPad(built, padIndex, magda::test::padDeviceFor("magda_kick", voiceId));

        const int midiNote = magda::padNoteFor(padIndex);
        auto* chain = built.grid->getChainForNote(midiNote);
        expect(chain != nullptr, "Kick pad chain must exist");
        if (chain == nullptr || chain->plugins.empty() || chain->plugins[0] == nullptr)
            return;

        auto kick = chain->plugins[0];
        expectEquals(kick->getPluginType(), juce::String("magda_kick"),
                     "Pad voice must be the compiled kick");
        expectEquals(built.grid->getPluginDeviceId(chain->index, 0), voiceId,
                     "The pad plugin must carry the model device's id");

        // Tweak a knob, then save the way a project save does.
        auto* bodyPitch = kick->getAutomatableParameterByID("magda_kick_body_pitch").get();
        expect(bodyPitch != nullptr, "Kick must expose the body_pitch host parameter");
        if (bodyPitch == nullptr)
            return;
        bodyPitch->setParameterFromHost(0.85f, juce::sendNotificationSync);

        auto* voiceDevice = padDeviceIn(built.device, voiceId);
        expect(voiceDevice != nullptr, "The pad's device must be in the model");
        if (voiceDevice == nullptr)
            return;
        capturePadDevice(*built.grid, *voiceDevice);

        DrumGridPlugin* restored = nullptr;
        auto restoredPlugin = rebuildFromModel(built, &restored);
        if (restored == nullptr)
            return;

        auto* restoredChain = restored->getChainForNote(midiNote);
        expect(restoredChain != nullptr, "Reloaded DrumGrid must have the kick pad chain");
        if (restoredChain == nullptr)
            return;

        expectEquals(static_cast<int>(restoredChain->plugins.size()), 1,
                     "Reloaded pad chain must have exactly one plugin");
        if (restoredChain->plugins.empty() || restoredChain->plugins[0] == nullptr) {
            expect(false, "Reloaded pad voice must exist");
            return;
        }

        auto restoredKick = restoredChain->plugins[0];
        expectEquals(restoredKick->getPluginType(), juce::String("magda_kick"),
                     "Reloaded pad voice must still be the compiled kick");

        auto* restoredParam =
            restoredKick->getAutomatableParameterByID("magda_kick_body_pitch").get();
        expect(restoredParam != nullptr, "Reloaded kick must expose the body_pitch parameter");
        if (restoredParam != nullptr)
            expectWithinAbsoluteError(restoredParam->getCurrentBaseValue(), 0.85f, 1.0e-4f,
                                      "Tweaked kick parameter must survive the reload");

        expectEquals(restored->getPluginDeviceId(restoredChain->index, 0), voiceId,
                     "Pad voice device id must survive the reload");
    }

    void testSamplerPadRoundTrip() {
        beginTest("A sampler pad keeps its sample across a reload");

        auto sampleFile = createTestWavFile();
        expect(sampleFile != nullptr, "Test sample must be written to disk");
        if (sampleFile == nullptr)
            return;

        auto built = makeGrid();
        if (built.grid == nullptr)
            return;

        constexpr int padIndex = 3;
        const int midiNote = magda::padNoteFor(padIndex);

        auto sampler = magda::padSamplerDevice(sampleFile->getFile().getFullPathName(), midiNote);
        sampler.id = 200;
        putOnPad(built, padIndex, sampler);

        auto* chain = built.grid->getChainForNote(midiNote);
        expect(chain != nullptr, "Sampler pad chain must exist");
        if (chain == nullptr || chain->plugins.empty())
            return;

        // The sample path and root note are model state, so the plugin comes up
        // pointing at the file with nothing captured first.
        auto* sampled =
            dynamic_cast<magda::daw::audio::MagdaSamplerPlugin*>(chain->plugins[0].get());
        expect(sampled != nullptr, "Pad voice must be the sampler");
        if (sampled != nullptr) {
            expectEquals(sampled->getSampleFile().getFullPathName(),
                         sampleFile->getFile().getFullPathName(),
                         "The pad's sampler must point at the file the model names");
            expectEquals(sampled->getRootNote(), midiNote,
                         "The pad's sampler must be rooted on the pad's note");
        }

        DrumGridPlugin* restored = nullptr;
        auto restoredPlugin = rebuildFromModel(built, &restored);
        if (restored == nullptr)
            return;

        auto* restoredChain = restored->getChainForNote(midiNote);
        expect(restoredChain != nullptr, "Reloaded DrumGrid must have the sampler pad chain");
        if (restoredChain == nullptr)
            return;

        expectEquals(static_cast<int>(restoredChain->plugins.size()), 1,
                     "Reloaded sampler pad chain must have exactly one plugin");
        if (!restoredChain->plugins.empty() && restoredChain->plugins[0] != nullptr)
            expectEquals(restoredChain->plugins[0]->getPluginType(),
                         juce::String(magda::daw::audio::MagdaSamplerPlugin::xmlTypeName),
                         "Reloaded pad voice must still be the sampler");
    }

    void testCompiledVoiceWithFxRoundTrip() {
        beginTest("A pad's voice and FX keep their order across a reload");

        auto built = makeGrid();
        if (built.grid == nullptr)
            return;

        constexpr int padIndex = 1;
        putOnPad(built, padIndex, magda::test::padDeviceFor("magda_hat", 300));
        putOnPad(built, padIndex, magda::test::padDeviceFor("magda_filter", 301));

        const int midiNote = magda::padNoteFor(padIndex);
        auto* chain = built.grid->getChainForNote(midiNote);
        expect(chain != nullptr, "Hat pad chain must exist");
        if (chain == nullptr)
            return;
        expectEquals(static_cast<int>(chain->plugins.size()), 2, "FX must sit after the pad voice");

        DrumGridPlugin* restored = nullptr;
        auto restoredPlugin = rebuildFromModel(built, &restored);
        if (restored == nullptr)
            return;

        auto* restoredChain = restored->getChainForNote(midiNote);
        expect(restoredChain != nullptr, "Reloaded DrumGrid must have the hat pad chain");
        if (restoredChain == nullptr)
            return;

        expectEquals(static_cast<int>(restoredChain->plugins.size()), 2,
                     "Reloaded pad chain must keep both the voice and the FX");
        if (restoredChain->plugins.size() == 2 && restoredChain->plugins[0] != nullptr &&
            restoredChain->plugins[1] != nullptr) {
            expectEquals(restoredChain->plugins[0]->getPluginType(), juce::String("magda_hat"),
                         "Voice must stay first in the reloaded chain");
            expectEquals(restoredChain->plugins[1]->getPluginType(), juce::String("magda_filter"),
                         "FX must stay second in the reloaded chain");
        }
    }

    /// The failure the ownership flip exists to end: an edit the model makes has
    /// to reach the engine on its own, with nothing else happening in between.
    void testPadRemovedFromModelLeavesTheGrid() {
        beginTest("A pad the model drops leaves the grid on the next sync");

        auto built = makeGrid();
        if (built.grid == nullptr)
            return;

        constexpr int padIndex = 2;
        putOnPad(built, padIndex, magda::test::padDeviceFor("magda_kick", 400));

        const int midiNote = magda::padNoteFor(padIndex);
        expect(built.grid->getChainForNote(midiNote) != nullptr, "The pad must be on the grid");

        // Dropping a pad is a model edit and nothing else.
        built.device.pads->chains.clear();
        built.grid->syncFromModel(*built.device.pads.get(),
                                  magda::test::padPluginFactory(*built.edit));

        expect(built.grid->getChainForNote(midiNote) == nullptr,
               "The pad must be gone from the grid");
        expect(built.grid->getChains().empty(), "The grid must hold no chains");

        // And adding one back reaches it the same way.
        putOnPad(built, padIndex, magda::test::padDeviceFor("magda_hat", 401));
        auto* chain = built.grid->getChainForNote(midiNote);
        expect(chain != nullptr, "The new pad must be on the grid");
        if (chain != nullptr && !chain->plugins.empty() && chain->plugins[0] != nullptr)
            expectEquals(chain->plugins[0]->getPluginType(), juce::String("magda_hat"),
                         "The new pad must hold what the model put on it");
    }

    /// A pad FX saved before its device was retired names the retired Tracktion
    /// type and stores its values as that plugin's own properties. The
    /// conversion now happens on the way into the model, which is where a pad
    /// device is built (#2207).
    void testRetiredPadFxRestoresAsSuccessor() {
        beginTest("A retired Tracktion pad FX restores as its compiled successor");

        // The pre-0.18 shape, spelled out rather than taken from the class, so
        // the test pins what is actually on disk in old projects.
        const juce::String legacy = R"(<PLUGIN type="drumgrid" id="1234">
  <CHAIN index="0" name="Kick" lowNote="24" highNote="24" rootNote="24">
    <PLUGIN type="delay" feedback="-6.0" mix="0.4" length="375"/>
  </CHAIN>
</PLUGIN>)";

        magda::DeviceInfo gridDevice;
        gridDevice.id = 1;
        gridDevice.pluginId = DrumGridPlugin::xmlTypeName;
        gridDevice.format = magda::PluginFormat::Internal;
        gridDevice.pluginState = legacy;
        magda::migrateLegacyPads(gridDevice);

        expect(static_cast<bool>(gridDevice.pads), "The legacy pads must reach the model");
        if (!gridDevice.pads || gridDevice.pads->chains.empty())
            return;

        auto& padElements = gridDevice.pads->chains[0].elements;
        expect(!padElements.empty(), "The pad must hold its FX");
        if (padElements.empty() || !magda::isDevice(padElements[0]))
            return;

        auto& fx = magda::getDevice(padElements[0]);
        fx.id = 500;
        expectEquals(fx.pluginId, juce::String("magda_delay"),
                     "A retired pad FX must become its compiled successor in the model");

        // Values carried across, not defaults: 375 ms over 1..2000, -6 dB as a
        // linear 0.5012 over 0..0.95, and mix straight through. In real units,
        // which is what the model holds and the processor converts.
        const auto value = [&fx](const char* name) {
            for (const auto& param : fx.parameters)
                if (param.name == name)
                    return param.currentValue;
            return -1.0f;
        };
        expectWithinAbsoluteError(value("Time"), 375.0f, 1.0f, "Delay length must survive as Time");
        expectWithinAbsoluteError(value("Feedback"), 0.5012f, 0.002f,
                                  "Delay feedback must convert from dB");
        expectWithinAbsoluteError(value("Mix"), 0.4f, 0.002f, "Delay mix must survive");

        // The retired plugin's own properties are consumed, so nothing hands
        // them to a device that has no idea what they mean.
        expect(!fx.pluginState.contains("feedback"),
               "The retired device's properties must not survive into the successor's state");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin = createCustomPlugin(*edit, DrumGridPlugin::xmlTypeName);
        auto* grid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(grid != nullptr, "DrumGrid plugin must be created");
        if (grid == nullptr)
            return;

        grid->syncFromModel(*gridDevice.pads.get(), magda::test::padPluginFactory(*edit));

        auto* chain = grid->getChainForNote(magda::kPadBaseNote);
        expect(chain != nullptr, "The migrated pad must be on the grid");
        if (chain == nullptr || chain->plugins.empty() || chain->plugins[0] == nullptr) {
            expect(false, "The migrated pad must hold its FX");
            return;
        }

        expectEquals(chain->plugins[0]->getPluginType(), juce::String("magda_delay"),
                     "Retired pad FX must be built as the compiled successor");
    }

    // Full model-level cycle through TrackManager + AudioBridge: the same path a
    // real project save/reload takes.
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
        const auto deviceId = tm.addDeviceToTrack(trackId, drumGridDevice());
        expect(deviceId != magda::INVALID_DEVICE_ID, "Drum Grid device must be added");
        if (deviceId == magda::INVALID_DEVICE_ID) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        const auto devicePath = magda::ChainNodePath::topLevelDevice(trackId, deviceId);

        constexpr int padIndex = 0;
        const int midiNote = magda::padNoteFor(padIndex);
        tm.setPadDevice(devicePath, padIndex, magda::test::padDeviceFor("magda_kick", 0));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        auto plugin = bridge->getPlugin(devicePath);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "Drum Grid runtime plugin must be created");
        if (drumGrid == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Kick pad chain must exist before save");
        if (chain == nullptr || chain->plugins.empty() || chain->plugins[0] == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        if (auto* bodyPitch =
                chain->plugins[0]->getAutomatableParameterByID("magda_kick_body_pitch").get())
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

        // The pads are the model's, not the plugin state's. A copy left inside
        // the device's own state is the drift this ownership exists to end.
        expect(static_cast<bool>(devInfo->pads), "The Drum Grid must carry its pads in the model");
        expect(!devInfo->pluginState.contains("magda_kick"),
               "The Drum Grid's own saved state must not carry a second copy of its pads");

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
        const auto deviceId = tm.addDeviceToTrack(trackId, drumGridDevice());
        const auto devicePath = magda::ChainNodePath::topLevelDevice(trackId, deviceId);

        constexpr int padIndex = 0;
        const int midiNote = magda::padNoteFor(padIndex);
        tm.setPadDevice(devicePath, padIndex, magda::test::padDeviceFor("magda_kick", 0));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        auto plugin = bridge->getPlugin(devicePath);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "Drum Grid runtime plugin must be created");
        if (drumGrid == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        auto* chain = drumGrid->getChainForNote(midiNote);
        expect(chain != nullptr, "Kick pad chain must exist before save");
        if (chain != nullptr && !chain->plugins.empty() && chain->plugins[0] != nullptr) {
            if (auto* bodyPitch =
                    chain->plugins[0]->getAutomatableParameterByID("magda_kick_body_pitch").get())
                bodyPitch->setParameterFromHost(0.85f, juce::sendNotificationSync);
        }

        // Save the way ProjectManager does: capture states, then serialize.
        bridge->captureAllPluginStates();

        juce::TemporaryFile projectFile(projectScratchFile("drum_grid_project"));

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
                expect(static_cast<bool>(dev.pads),
                       "The reloaded Drum Grid must carry its pads in the model");
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

    // #1920: a track-level plugin resync (fired by anything that moves or adds a
    // track) must not strip a Drum Grid's pads. The runtime chain keeps playing
    // either way, so the loss only shows on reload.
    void testTrackMoveKeepsSamplerPadsThroughProjectReload() {
        beginTest("Track move keeps DrumGrid sampler pads through a project reload");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "Audio bridge must be available");
        if (bridge == nullptr)
            return;

        auto sampleFile = createTestWavFile();
        expect(sampleFile != nullptr, "Test sample must be written to disk");
        if (sampleFile == nullptr)
            return;
        const auto samplePath = sampleFile->getFile().getFullPathName();

        auto& tm = magda::TrackManager::getInstance();
        auto& cm = magda::ClipManager::getInstance();
        cm.clearAllClips();
        tm.clearAllTracks();
        tm.setAudioEngine(&wrapper);

        const auto trackId = tm.createTrack("Drums");
        tm.createTrack("Other");

        const auto deviceId = tm.addDeviceToTrack(trackId, drumGridDevice());
        const auto devicePath = magda::ChainNodePath::topLevelDevice(trackId, deviceId);

        constexpr int padIndex = 0;
        const int midiNote = magda::padNoteFor(padIndex);
        tm.setPadDevice(devicePath, padIndex, magda::padSamplerDevice(samplePath, midiNote));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        auto plugin = bridge->getPlugin(devicePath);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        expect(drumGrid != nullptr, "Drum Grid runtime plugin must be created");
        if (drumGrid == nullptr) {
            cm.clearAllClips();
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        // Reorder the track: what the user does before saving. It fires
        // tracksChanged -> syncAll -> per-track plugin sync.
        tm.moveTrack(trackId, 1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        auto* postMoveChain = drumGrid->getChainForNote(midiNote);
        expect(postMoveChain != nullptr, "Sampler pad chain must survive the track move");
        if (postMoveChain != nullptr)
            expectEquals(static_cast<int>(postMoveChain->plugins.size()), 1,
                         "Sampler pad chain must still hold its sampler after the track move");

        // Save the way ProjectManager does: capture states, then serialize.
        bridge->captureAllPluginStates();

        juce::TemporaryFile projectFile(projectScratchFile("drum_grid_moved_track"));

        const auto& info = magda::ProjectManager::getInstance().getCurrentProjectInfo();
        expect(magda::ProjectSerializer::saveToFile(projectFile.getFile(), info),
               "Project must save to file");

        cm.clearAllClips();
        tm.clearAllTracks();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        magda::StagedProjectData staged;
        expect(magda::ProjectSerializer::loadAndStage(projectFile.getFile(), staged),
               "Project must load and stage");
        magda::ProjectSerializer::commitStaged(staged);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        DrumGridPlugin* reloaded = nullptr;
        if (auto* reloadedTrack = tm.getTrack(trackId)) {
            for (const auto& element : reloadedTrack->chain.fxChainElements) {
                if (!magda::isDevice(element))
                    continue;
                auto p = bridge->getPlugin(magda::ChainNodePath::topLevelDevice(
                    reloadedTrack->id, magda::getDevice(element).id));
                reloaded = dynamic_cast<DrumGridPlugin*>(p.get());
                if (reloaded != nullptr)
                    break;
            }
        }

        expect(reloaded != nullptr, "Reloaded Drum Grid runtime plugin must exist");
        if (reloaded != nullptr) {
            auto* reloadedChain = reloaded->getChainForNote(midiNote);
            expect(reloadedChain != nullptr, "Reloaded Drum Grid must have the sampler pad chain");
            if (reloadedChain != nullptr && !reloadedChain->plugins.empty()) {
                auto* sampler = dynamic_cast<magda::daw::audio::MagdaSamplerPlugin*>(
                    reloadedChain->plugins[0].get());
                expect(sampler != nullptr, "Reloaded pad plugin must be the sampler");
                if (sampler != nullptr)
                    expectEquals(sampler->getSampleFile().getFullPathName(), samplePath,
                                 "Reloaded pad must still point at its sample file");
            } else {
                expect(false, "Reloaded pad chain must still hold its sampler");
            }
        }

        cm.clearAllClips();
        tm.clearAllTracks();
        tm.setAudioEngine(nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    /// A pad's device carries what only its plugin can answer (#2200, #2205).
    ///
    /// The model owns which pads exist and what sits on them, but not a
    /// parameter's name and range, nor how many channels a plugin writes. The
    /// plan compiler needs both: without the parameters the table allocates no
    /// window for a pad's device, and a mono pad voice left at DeviceInfo's
    /// stereo default is compiled as though it were stereo.
    void testPadDeviceCarriesItsPluginsParameters() {
        beginTest("A pad's device carries its plugin's parameters and channel counts");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "Audio bridge must be available");
        if (bridge == nullptr)
            return;

        auto& tm = magda::TrackManager::getInstance();
        tm.clearAllTracks();
        tm.setAudioEngine(&wrapper);

        const auto trackId = tm.createTrack("Drums");
        const auto deviceId = tm.addDeviceToTrack(trackId, drumGridDevice());
        const auto devicePath = magda::ChainNodePath::topLevelDevice(trackId, deviceId);

        constexpr int padIndex = 3;
        tm.setPadDevice(devicePath, padIndex, magda::test::padDeviceFor("magda_kick", 0));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        auto plugin = bridge->getPlugin(devicePath);
        auto* drumGrid = dynamic_cast<DrumGridPlugin*>(plugin.get());
        auto* devInfo = tm.getDeviceInChainByPath(devicePath);
        expect(drumGrid != nullptr && devInfo != nullptr && static_cast<bool>(devInfo->pads),
               "The Drum Grid and its pads must exist");
        if (drumGrid == nullptr || devInfo == nullptr || !devInfo->pads) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        int padDevices = 0;
        int withParameters = 0;
        bool sawRealRange = false;
        int checkedWidths = 0;

        for (const auto& pad : devInfo->pads->chains) {
            for (const auto& element : pad.elements) {
                if (!magda::isDevice(element))
                    continue;

                const auto& padDevice = magda::getDevice(element);
                ++padDevices;
                if (!padDevice.parameters.empty())
                    ++withParameters;

                // Real units, not the normalized values Tracktion exposes: the
                // native engine takes this metadata at face value.
                for (const auto& param : padDevice.parameters) {
                    if (param.maxValue > 1.0f) {
                        sawRealRange = true;
                        expect(param.currentValue >= param.minValue &&
                                   param.currentValue <= param.maxValue,
                               "A parameter's value must sit inside its own range");
                    }
                }

                auto live = padPluginFor(*drumGrid, padDevice.id);
                expect(live != nullptr, "Every pad device must have its plugin");
                if (live == nullptr)
                    continue;

                juce::StringArray inputs, outputs;
                live->getChannelNames(&inputs, &outputs);
                ++checkedWidths;
                expectEquals(padDevice.audioInputChannels, inputs.size(),
                             "A pad device's input width must be its plugin's");
                expectEquals(padDevice.audioOutputChannels, outputs.size(),
                             "A pad device's output width must be its plugin's");
            }
        }

        expect(padDevices > 0, "The pad must hold a device");
        expectEquals(withParameters, padDevices, "Every pad device must carry its parameters");
        expect(sawRealRange, "A compiled pad voice must report at least one parameter in real "
                             "units");
        expectEquals(checkedWidths, padDevices, "Every pad device's width must be checked");

        tm.clearAllTracks();
        tm.setAudioEngine(nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    static magda::DeviceInfo drumGridDevice() {
        magda::DeviceInfo device;
        device.name = "Drum Grid";
        device.format = magda::PluginFormat::Internal;
        device.pluginId = DrumGridPlugin::xmlTypeName;
        device.isInstrument = true;
        device.deviceType = magda::DeviceType::Instrument;
        device.canReceiveMidi = true;
        return device;
    }
};

static DrumGridPadChainSerializationTest drumGridPadChainSerializationTest;
