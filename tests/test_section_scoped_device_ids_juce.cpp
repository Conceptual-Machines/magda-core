#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/DeviceMeteringManager.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"

namespace {

magda::DeviceInfo makeInternalDevice(const juce::String& name, const juce::String& pluginId) {
    magda::DeviceInfo device;
    device.name = name;
    device.format = magda::PluginFormat::Internal;
    device.pluginId = pluginId;
    return device;
}

juce::String makePluginStateXml(const juce::String& pluginType) {
    juce::ValueTree state(tracktion::engine::IDs::PLUGIN);
    state.setProperty(tracktion::engine::IDs::type, pluginType, nullptr);
    if (auto xml = state.createXml())
        return xml->toString();
    return {};
}

bool containsPath(const std::vector<magda::ChainNodePath>& paths,
                  const magda::ChainNodePath& path) {
    return std::find(paths.begin(), paths.end(), path) != paths.end();
}

class DevicePropertyRecordingListener final : public magda::TrackManagerListener {
  public:
    void tracksChanged() override {}

    void devicePropertyChanged(const magda::ChainNodePath& devicePath) override {
        devicePropertyPaths.push_back(devicePath);
    }

    std::vector<magda::ChainNodePath> devicePropertyPaths;
};

}  // namespace

class SectionScopedDeviceIdsTest final : public juce::UnitTest {
  public:
    SectionScopedDeviceIdsTest() : juce::UnitTest("Section-Scoped Device IDs", "magda") {}

    void runTest() override {
        testOverlappingIdsResolveByPath();
        testRemovingPostFxDeviceDoesNotRemoveSameIdTopLevelDevice();
        testPostFxPropertyChangeDoesNotBypassSameIdTopLevelDevice();
        testParameterConfigWritesArePathScoped();
        testTrackChainBypassNotifiesNestedDevicePaths();
        testDeviceMetersArePathKeyed();
        testTopLevelReorderMovesLivePlugins();
        testMismatchedCompiledPluginStateDoesNotOverridePluginId();
    }

  private:
    void testOverlappingIdsResolveByPath() {
        beginTest("Devices in different sections can share DeviceId");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Section IDs");
        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto postFxId =
            trackManager.addDeviceToPostFx(trackId, makeInternalDevice("Post Delay", "delay"));
        const auto analysisId = trackManager.addDeviceToMixerAnalysis(
            trackId, makeInternalDevice("Mini Scope", "oscilloscope"));

        expectEquals(fxId, 1, "First FX device should use id 1");
        expectEquals(postFxId, 1, "First post-FX device should use id 1");
        expectEquals(analysisId, 1, "First mixer-analysis device should use id 1");

        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);
        const auto postFxPath = magda::ChainNodePath::postFxDevice(trackId, postFxId);
        const auto analysisPath = magda::ChainNodePath::mixerAnalysisDevice(trackId, analysisId);

        bridge->syncTrackPlugins(trackId);

        const auto fx = bridge->getPlugin(fxPath);
        const auto postFx = bridge->getPlugin(postFxPath);
        const auto analysis = bridge->getPlugin(analysisPath);

        expect(fx != nullptr, "FX path should resolve to a plugin");
        expect(postFx != nullptr, "Post-FX path should resolve to a plugin");
        expect(analysis != nullptr, "Mixer-analysis path should resolve to a plugin");
        expect(fx != postFx, "FX and post-FX paths must not alias the same plugin");
        expect(fx != analysis, "FX and mixer-analysis paths must not alias the same plugin");
        expect(postFx != analysis,
               "Post-FX and mixer-analysis paths must not alias the same plugin");

        const auto resolvedFx = bridge->resolveDevice(fxPath);
        const auto resolvedPostFx = bridge->resolveDevice(postFxPath);
        const auto resolvedAnalysis = bridge->resolveDevice(analysisPath);

        expect(resolvedFx.info != nullptr && resolvedFx.info->name == "FX Filter",
               "resolveDevice should return the FX DeviceInfo");
        expect(resolvedPostFx.info != nullptr && resolvedPostFx.info->name == "Post Delay",
               "resolveDevice should return the post-FX DeviceInfo");
        expect(resolvedAnalysis.info != nullptr && resolvedAnalysis.info->name == "Mini Scope",
               "resolveDevice should return the mixer-analysis DeviceInfo");
        expect(resolvedFx.plugin == fx, "resolveDevice should return the FX plugin");
        expect(resolvedPostFx.plugin == postFx, "resolveDevice should return the post-FX plugin");
        expect(resolvedAnalysis.plugin == analysis,
               "resolveDevice should return the mixer-analysis plugin");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testRemovingPostFxDeviceDoesNotRemoveSameIdTopLevelDevice() {
        beginTest("Removing post-FX device does not remove same-id top-level device");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("FX plus analyzer");

        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto analyzerId =
            trackManager.addDeviceToPostFx(trackId, makeInternalDevice("Scope", "oscilloscope"));

        expectEquals(fxId, 1, "First top-level device should use id 1");
        expectEquals(analyzerId, 1, "First post-FX analyzer should use id 1");

        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);
        const auto analyzerPath = magda::ChainNodePath::postFxDevice(trackId, analyzerId);

        bridge->syncTrackPlugins(trackId);

        auto fxPlugin = bridge->getPlugin(fxPath);
        auto analyzerPlugin = bridge->getPlugin(analyzerPath);
        auto* teTrack = bridge->getAudioTrack(trackId);

        expect(fxPlugin != nullptr, "Top-level FX plugin should be created");
        expect(analyzerPlugin != nullptr, "Post-FX analyzer plugin should be created");
        expect(teTrack != nullptr, "Tracktion track should exist");
        if (!fxPlugin || !analyzerPlugin || teTrack == nullptr) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        trackManager.removeDeviceFromChainByPath(analyzerPath);
        bridge->syncTrackPlugins(trackId);

        expect(bridge->getPlugin(analyzerPath) == nullptr,
               "Removed post-FX analyzer should not remain synced");
        expect(bridge->getPlugin(fxPath) == fxPlugin,
               "Removing same-id post-FX analyzer must not remove the top-level FX plugin");
        expect(teTrack->pluginList.indexOf(fxPlugin.get()) >= 0,
               "Top-level FX plugin should remain on the Tracktion plugin list");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testPostFxPropertyChangeDoesNotBypassSameIdTopLevelDevice() {
        beginTest("Post-FX property changes do not toggle same-id top-level device");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("FX plus bypassed analyzer");

        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto analyzerId =
            trackManager.addDeviceToPostFx(trackId, makeInternalDevice("Scope", "oscilloscope"));

        expectEquals(fxId, 1, "First top-level device should use id 1");
        expectEquals(analyzerId, 1, "First post-FX analyzer should use id 1");

        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);
        const auto analyzerPath = magda::ChainNodePath::postFxDevice(trackId, analyzerId);

        bridge->syncTrackPlugins(trackId);

        auto fxPlugin = bridge->getPlugin(fxPath);
        expect(fxPlugin != nullptr, "Top-level FX plugin should be created");
        if (!fxPlugin) {
            trackManager.clearAllTracks();
            trackManager.setAudioEngine(nullptr);
            return;
        }

        fxPlugin->setEnabled(true);
        if (auto* analyzer = trackManager.getDeviceInChainByPath(analyzerPath)) {
            analyzer->bypassed = true;
            bridge->devicePropertyChanged(analyzerPath);
        } else {
            expect(false, "Post-FX analyzer should resolve by path");
        }

        expect(fxPlugin->isEnabled(),
               "Bypassing same-id post-FX analyzer must not disable the top-level FX plugin");

        if (auto* fxDevice = trackManager.getDeviceInChainByPath(fxPath)) {
            fxDevice->bypassed = true;
            bridge->devicePropertyChanged(fxPath);
        }

        expect(!fxPlugin->isEnabled(),
               "Bypassing the top-level device itself should still disable its plugin");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testParameterConfigWritesArePathScoped() {
        beginTest("Visible and mini-mixer parameter config writes are path scoped");

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();

        const auto trackId = trackManager.createTrack("Parameter config sections");
        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto postFxId =
            trackManager.addDeviceToPostFx(trackId, makeInternalDevice("Post Delay", "delay"));
        const auto analysisId = trackManager.addDeviceToMixerAnalysis(
            trackId, makeInternalDevice("Mini Scope", "oscilloscope"));

        expectEquals(fxId, 1, "First FX device should use id 1");
        expectEquals(postFxId, 1, "First post-FX device should use id 1");
        expectEquals(analysisId, 1, "First mixer-analysis device should use id 1");

        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);
        const auto postFxPath = magda::ChainNodePath::postFxDevice(trackId, postFxId);
        const auto analysisPath = magda::ChainNodePath::mixerAnalysisDevice(trackId, analysisId);

        trackManager.setDeviceVisibleParameters(postFxPath, {4, 5});
        trackManager.setDeviceMiniMixerParameters(postFxPath, {6});
        trackManager.setDeviceVisibleParameters(analysisPath, {7});
        trackManager.setDeviceMiniMixerParameters(analysisPath, {8, 9});

        auto* fx = trackManager.getDeviceInChainByPath(fxPath);
        auto* postFx = trackManager.getDeviceInChainByPath(postFxPath);
        auto* analysis = trackManager.getDeviceInChainByPath(analysisPath);

        expect(fx != nullptr && fx->visibleParameters.empty() && fx->miniMixerParameters.empty(),
               "Path-scoped writes must not alter the same-id FX device");
        expect(postFx != nullptr && postFx->visibleParameters == std::vector<int>{4, 5},
               "Post-FX visible parameters should be written to the post-FX device");
        expect(postFx != nullptr && postFx->miniMixerParameters == std::vector<int>{6},
               "Post-FX mini-mixer parameters should be written to the post-FX device");
        expect(analysis != nullptr && analysis->visibleParameters == std::vector<int>{7},
               "Mixer-analysis visible parameters should be written to the analysis device");
        expect(analysis != nullptr && analysis->miniMixerParameters == std::vector<int>{8, 9},
               "Mixer-analysis mini-mixer parameters should be written to the analysis device");

        trackManager.clearAllTracks();
    }

    void testTrackChainBypassNotifiesNestedDevicePaths() {
        beginTest("Track chain bypass notifies nested devices with full paths");

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();

        const auto trackId = trackManager.createTrack("Nested bypass");
        const auto topLevelId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("Top", "magda_filter"));
        const auto rackId = trackManager.addRackToTrack(trackId, "Rack");

        auto* rack = trackManager.getRack(trackId, rackId);
        expect(rack != nullptr && !rack->chains.empty(), "Rack should have a default chain");
        if (!rack || rack->chains.empty()) {
            trackManager.clearAllTracks();
            return;
        }

        const auto chainId = rack->chains.front().id;
        const auto nestedId = trackManager.addDeviceToChain(trackId, rackId, chainId,
                                                            makeInternalDevice("Nested", "delay"));

        const auto topLevelPath = magda::ChainNodePath::topLevelDevice(trackId, topLevelId);
        const auto nestedPath =
            magda::ChainNodePath::chainDevice(trackId, rackId, chainId, nestedId);

        DevicePropertyRecordingListener listener;
        trackManager.addListener(&listener);
        trackManager.setChainBypassed(trackId, true);
        trackManager.removeListener(&listener);

        expect(containsPath(listener.devicePropertyPaths, topLevelPath),
               "Top-level device should still receive a property notification");
        expect(containsPath(listener.devicePropertyPaths, nestedPath),
               "Nested rack device should receive a property notification with its full path");
        expect(!containsPath(listener.devicePropertyPaths,
                             magda::ChainNodePath::topLevelDevice(trackId, nestedId)),
               "Nested device must not be reported as a fabricated top-level path");

        auto* nestedDevice = trackManager.getDeviceInChainByPath(nestedPath);
        expect(nestedDevice != nullptr && nestedDevice->bypassed,
               "Nested device model state should be bypassed");

        trackManager.clearAllTracks();
    }

    void testDeviceMetersArePathKeyed() {
        beginTest("Device meters with overlapping ids are keyed by path");

        magda::DeviceMeteringManager metering;
        const auto fxPath = magda::ChainNodePath::topLevelDevice(7, 1);
        const auto postFxPath = magda::ChainNodePath::postFxDevice(7, 1);

        metering.ensureEntry(fxPath);
        metering.ensureEntry(postFxPath);
        metering.setDirectLevels(fxPath, 0.25f, 0.5f);
        metering.setDirectLevels(postFxPath, 0.75f, 0.875f);

        magda::DeviceMeteringManager::DeviceMeterData fxLevels;
        magda::DeviceMeteringManager::DeviceMeterData postFxLevels;
        expect(metering.getLatestLevels(fxPath, fxLevels), "FX meter should exist");
        expect(metering.getLatestLevels(postFxPath, postFxLevels), "Post-FX meter should exist");
        expectWithinAbsoluteError(fxLevels.peakL, 0.25f, 0.0001f,
                                  "FX left meter must not alias post-FX");
        expectWithinAbsoluteError(fxLevels.peakR, 0.5f, 0.0001f,
                                  "FX right meter must not alias post-FX");
        expectWithinAbsoluteError(postFxLevels.peakL, 0.75f, 0.0001f,
                                  "Post-FX left meter must not alias FX");
        expectWithinAbsoluteError(postFxLevels.peakR, 0.875f, 0.0001f,
                                  "Post-FX right meter must not alias FX");
    }

    void testTopLevelReorderMovesLivePlugins() {
        beginTest("Top-level chain reorder moves live plugins and restarts the graph");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Reorder");
        const auto delayId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("A", "delay"));
        const auto reverbId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("B", "reverb"));
        const auto delayPath = magda::ChainNodePath::topLevelDevice(trackId, delayId);
        const auto reverbPath = magda::ChainNodePath::topLevelDevice(trackId, reverbId);

        bridge->syncTrackPlugins(trackId);

        auto delayPlugin = bridge->getPlugin(delayPath);
        auto reverbPlugin = bridge->getPlugin(reverbPath);
        auto* teTrack = bridge->getAudioTrack(trackId);

        expect(delayPlugin != nullptr, "Delay plugin should be created");
        expect(reverbPlugin != nullptr, "Reverb plugin should be created");
        expect(teTrack != nullptr, "Tracktion track should exist");
        if (!delayPlugin || !reverbPlugin || teTrack == nullptr)
            return;

        const auto initialDelayIndex = teTrack->pluginList.indexOf(delayPlugin.get());
        const auto initialReverbIndex = teTrack->pluginList.indexOf(reverbPlugin.get());
        expect(initialDelayIndex >= 0, "Delay should be in the Tracktion plugin list");
        expect(initialReverbIndex >= 0, "Reverb should be in the Tracktion plugin list");
        expect(initialDelayIndex < initialReverbIndex, "Initial TE order should match model order");

        int restartRequests = 0;
        juce::StringArray restartReasons;
        bridge->getPluginManager().onPluginOrderGraphRestartRequested =
            [&](magda::TrackId changedTrackId, const juce::String& reason) {
                if (changedTrackId == trackId) {
                    ++restartRequests;
                    restartReasons.add(reason);
                }
            };

        magda::ChainNodePath trackChainPath;
        trackChainPath.trackId = trackId;
        magda::MoveChainElementsCommand moveCommand({reverbPath}, trackChainPath, 0);
        moveCommand.execute();
        expect(moveCommand.didMove(), "UI reorder command should move the element");

        const auto delayAfterMove = bridge->getPlugin(delayPath);
        const auto reverbAfterMove = bridge->getPlugin(reverbPath);
        expect(delayAfterMove == delayPlugin, "Reorder must not recreate the delay plugin");
        expect(reverbAfterMove == reverbPlugin, "Reorder must not recreate the reverb plugin");

        const auto movedDelayIndex = teTrack->pluginList.indexOf(delayPlugin.get());
        const auto movedReverbIndex = teTrack->pluginList.indexOf(reverbPlugin.get());
        expect(movedReverbIndex >= 0, "Moved reverb should still be in the TE plugin list");
        expect(movedDelayIndex >= 0, "Moved delay should still be in the TE plugin list");
        expect(movedReverbIndex < movedDelayIndex, "TE order should follow the reordered model");
        expectEquals(restartRequests, 1, "Reorder must request a graph restart");
        expect(restartReasons.contains("track-plugin-order"),
               "Reorder restart should be tagged with the track-plugin-order reason");

        magda::MoveChainElementsCommand moveBackCommand({reverbPath}, trackChainPath, 2);
        moveBackCommand.execute();
        expect(moveBackCommand.didMove(), "Moving the first element to the end should work");

        const auto restoredDelayIndex = teTrack->pluginList.indexOf(delayPlugin.get());
        const auto restoredReverbIndex = teTrack->pluginList.indexOf(reverbPlugin.get());
        expect(restoredDelayIndex >= 0, "Delay should remain in the TE plugin list");
        expect(restoredReverbIndex >= 0, "Reverb should remain in the TE plugin list");
        expect(restoredDelayIndex < restoredReverbIndex,
               "TE order should follow a forward same-container move");
        expectEquals(restartRequests, 2, "Moving back should request another graph restart");

        const auto filterId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("C", "magda_filter"), 1);
        const auto filterPath = magda::ChainNodePath::topLevelDevice(trackId, filterId);
        const auto filterPlugin = bridge->getPlugin(filterPath);
        expect(filterPlugin != nullptr, "Inserted filter should be created");

        const auto filterIndex =
            filterPlugin ? teTrack->pluginList.indexOf(filterPlugin.get()) : -1;
        const auto delayAfterInsertIndex = teTrack->pluginList.indexOf(delayPlugin.get());
        const auto reverbAfterInsertIndex = teTrack->pluginList.indexOf(reverbPlugin.get());
        expect(delayAfterInsertIndex < filterIndex && filterIndex < reverbAfterInsertIndex,
               "TE order should follow indexed insertion into an existing chain");

        bridge->getPluginManager().onPluginOrderGraphRestartRequested = nullptr;
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }

    void testMismatchedCompiledPluginStateDoesNotOverridePluginId() {
        beginTest("Compiled plugin restore rejects mismatched saved type");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Mismatched Compiled State");

        auto freqShift = makeInternalDevice("Freq Shift", "magda_freq_shift");
        freqShift.pluginState = makePluginStateXml("magda_ring_mod");
        auto pitch = makeInternalDevice("Pitch", "magda_pitch");
        pitch.pluginState = makePluginStateXml("magda_delay");

        const auto freqShiftId = trackManager.addDeviceToTrack(trackId, freqShift);
        const auto pitchId = trackManager.addDeviceToTrack(trackId, pitch);
        const auto freqShiftPath = magda::ChainNodePath::topLevelDevice(trackId, freqShiftId);
        const auto pitchPath = magda::ChainNodePath::topLevelDevice(trackId, pitchId);

        bridge->syncTrackPlugins(trackId);

        const auto freqShiftPlugin = bridge->getPlugin(freqShiftPath);
        const auto pitchPlugin = bridge->getPlugin(pitchPath);

        expect(freqShiftPlugin != nullptr, "Freq Shift plugin should be created");
        expect(pitchPlugin != nullptr, "Pitch plugin should be created");
        if (freqShiftPlugin)
            expect(freqShiftPlugin->getPluginType().equalsIgnoreCase("magda_freq_shift"),
                   "Freq Shift must not instantiate the stale Ring Mod state");
        if (pitchPlugin)
            expect(pitchPlugin->getPluginType().equalsIgnoreCase("magda_pitch"),
                   "Pitch must not instantiate the stale Delay state");

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }
};

static SectionScopedDeviceIdsTest sectionScopedDeviceIdsTest;
