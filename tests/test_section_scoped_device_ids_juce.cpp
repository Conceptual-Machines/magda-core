#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
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

}  // namespace

class SectionScopedDeviceIdsTest final : public juce::UnitTest {
  public:
    SectionScopedDeviceIdsTest() : juce::UnitTest("Section-Scoped Device IDs", "magda") {}

    void runTest() override {
        testOverlappingIdsResolveByPath();
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
