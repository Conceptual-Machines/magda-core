#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/plugins/AudioSidechainMonitorPlugin.hpp"
#include "magda/daw/audio/plugins/FaustPlugin.hpp"
#include "magda/daw/audio/plugins/MidiReceivePlugin.hpp"
#include "magda/daw/audio/processors/internal/NativeDeviceProcessors.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

constexpr const char* kSidechainDsp = R"FAUST(
// Self-contained test DSP. The literal "stdfaust.lib" in this comment is
// load-bearing: compile() only skips its automatic import when the source
// already mentions the library, and the test binary has no faustlibraries dir.
process(mainL, mainR, sideL, sideR) = mainL + sideL, mainR + sideR;
)FAUST";

constexpr const char* kStereoDsp = R"FAUST(
// Self-contained test DSP; see the load-bearing "stdfaust.lib" note above.
process = _, _;
)FAUST";

constexpr const char* kMonoSidechainDsp = R"FAUST(
// Self-contained test DSP; see the load-bearing "stdfaust.lib" note above.
process(mainL, mainR, sidechain) = mainL + sidechain, mainR + sidechain;
)FAUST";

constexpr const char* kNineInputDsp = R"FAUST(
// Self-contained test DSP; see the load-bearing "stdfaust.lib" note above.
process(a, b, c, d, e, f, g, h, i) = a, b;
)FAUST";

constexpr const char* kInvalidDsp = R"FAUST(
// Self-contained test DSP; see the load-bearing "stdfaust.lib" note above.
process = deliberatelyUndefinedProcessor;
)FAUST";

te::Plugin::Ptr createFaustEffect(te::Edit& edit) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, audio::FaustPlugin::xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

magda::DeviceInfo makeSavedFaust(magda::TrackId sourceTrackId, const juce::String& source) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, audio::FaustPlugin::xmlTypeName, nullptr);
    state.setProperty("dspSource", source, nullptr);
    state.setProperty("dspName", "Saved test", nullptr);

    magda::DeviceInfo device;
    device.name = "Faust";
    device.format = magda::PluginFormat::Internal;
    device.pluginId = audio::FaustPlugin::xmlTypeName;
    device.canSidechain = true;
    device.sidechain.type = magda::SidechainConfig::Type::Audio;
    device.sidechain.sourceTrackId = sourceTrackId;
    if (auto xml = state.createXml())
        device.pluginState = xml->toString();
    return device;
}

class FaustSidechainTest final : public juce::UnitTest {
  public:
    FaustSidechainTest() : juce::UnitTest("Faust Audio Sidechain Tests", "magda") {}

    void runTest() override {
        beginTest("Extra DSP inputs advertise and render a stereo audio sidechain");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 2);
        expect(edit != nullptr, "Test edit should be created");
        if (!edit)
            return;

        auto plugin = createFaustEffect(*edit);
        auto* faust = dynamic_cast<audio::FaustPlugin*>(plugin.get());
        expect(faust != nullptr, "Runtime Faust effect should be created");
        if (!faust)
            return;

        juce::String error;
        const bool loaded = faust->loadDspSource("Sidechain test", kSidechainDsp, error);
        expect(loaded, "Four-input DSP should compile: " + error);
        if (!loaded)
            return;

        expect(faust->canSidechain(), "A four-input Faust effect should accept a sidechain");

        juce::StringArray inputs;
        juce::StringArray outputs;
        faust->getChannelNames(&inputs, &outputs);
        expectEquals(inputs.size(), 4);
        expectEquals(outputs.size(), 2);
        expectEquals(inputs[2], juce::String("Sidechain Left"));
        expectEquals(inputs[3], juce::String("Sidechain Right"));

        magda::DeviceInfo device;
        device.canSidechain = false;
        magda::FaustProcessor processor(1929, plugin);
        processor.populateParameters(device);
        expect(device.canSidechain, "DeviceInfo should expose the live DSP capability");

        constexpr int kBlockSize = 64;
        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = 44100.0;
        initInfo.blockSizeSamples = kBlockSize;
        faust->baseClassInitialise(initInfo);

        juce::AudioBuffer<float> buffer(4, kBlockSize);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            std::fill_n(buffer.getWritePointer(channel), kBlockSize, 0.1f * (channel + 1));

        te::MidiMessageArray midi;
        te::PluginRenderContext context(
            &buffer, juce::AudioChannelSet::quadraphonic(), 0, kBlockSize, &midi, 0.0,
            tracktion::TimeRange(tracktion::TimePosition(),
                                 tracktion::TimePosition::fromSeconds(kBlockSize / 44100.0)),
            true, false, false, false);
        faust->applyToBuffer(context);

        expectWithinAbsoluteError(buffer.getSample(0, 0), 0.4f, 0.0001f,
                                  "Left output should include sidechain left");
        expectWithinAbsoluteError(buffer.getSample(1, 0), 0.6f, 0.0001f,
                                  "Right output should include sidechain right");

        beginTest("A three-input DSP advertises a mono sidechain");

        error.clear();
        expect(faust->loadDspSource("Mono sidechain test", kMonoSidechainDsp, error),
               "Three-input DSP should compile: " + error);
        inputs.clear();
        faust->getChannelNames(&inputs, nullptr);
        expectEquals(inputs.size(), 3);
        expectEquals(inputs[2], juce::String("Sidechain"));

        beginTest("A runtime patch wider than scratch capacity fails safely");

        error.clear();
        expect(faust->loadDspSource("Wide input test", kNineInputDsp, error),
               "Nine-input DSP should compile: " + error);
        inputs.clear();
        faust->getChannelNames(&inputs, nullptr);
        expectEquals(inputs.size(), 9);
        const auto& diagnostics = faust->getLastRebindDiagnostics();
        expect(!diagnostics.empty(), "Wide runtime patch should report its scratch requirement");
        if (!diagnostics.empty())
            expect(diagnostics.front().containsIgnoreCase("reload"),
                   "Wide runtime patch diagnostic should explain how to activate it");
        const float beforeWideRender = buffer.getSample(0, 0);
        faust->applyToBuffer(context);
        expectWithinAbsoluteError(buffer.getSample(0, 0), beforeWideRender, 0.0001f,
                                  "An undersized scratch buffer should leave audio untouched");

        beginTest("A stereo patch swap removes capability and the live source");

        const auto tracks = te::getAudioTracks(*edit);
        expect(tracks.size() >= 2, "Test edit should contain a source track");
        if (tracks.size() < 2)
            return;
        faust->setSidechainSourceID(tracks[1]->itemID);
        expect(faust->getSidechainSourceID().isValid(), "Sidechain source should be assigned");

        error.clear();
        expect(faust->loadDspSource("Stereo test", kStereoDsp, error),
               "Stereo DSP should compile: " + error);
        expect(!faust->canSidechain(), "A stereo Faust effect should not accept a sidechain");
        expect(!faust->getSidechainSourceID().isValid(),
               "Dropping extra inputs should clear the live sidechain source");

        inputs.clear();
        faust->getChannelNames(&inputs, nullptr);
        expectEquals(inputs.size(), 2);

        device.canSidechain = true;
        processor.populateParameters(device);
        expect(!device.canSidechain, "DeviceInfo should remove the stale capability");

        beginTest("Project load clears a serialized sidechain from a stereo Faust DSP");

        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge should be available");
        if (!bridge)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto sourceTrackId = trackManager.createTrack("Saved sidechain source");
        const auto destinationTrackId = trackManager.createTrack("Saved sidechain destination");
        const auto deviceId = trackManager.addDeviceToTrack(
            destinationTrackId, makeSavedFaust(sourceTrackId, kStereoDsp));
        expect(deviceId != magda::INVALID_DEVICE_ID, "Saved Faust device should be added");

        if (deviceId != magda::INVALID_DEVICE_ID) {
            const auto path = magda::ChainNodePath::topLevelDevice(destinationTrackId, deviceId);
            bridge->syncTrackPlugins(sourceTrackId);
            bridge->syncTrackPlugins(destinationTrackId);

            auto* loadedDevice = trackManager.getDeviceInChainByPath(path);
            expect(loadedDevice != nullptr, "Loaded Faust DeviceInfo should resolve");
            if (loadedDevice) {
                expect(!loadedDevice->canSidechain,
                       "Stereo saved DSP should clear the serialized capability");
                expect(!loadedDevice->sidechain.isActive(),
                       "Stereo saved DSP should clear the serialized sidechain");
            }

            bool hasMidiReceive = false;
            if (auto* destinationTrack = bridge->getAudioTrack(destinationTrackId)) {
                for (int i = 0; i < destinationTrack->pluginList.size(); ++i)
                    hasMidiReceive =
                        hasMidiReceive || dynamic_cast<magda::MidiReceivePlugin*>(
                                              destinationTrack->pluginList[i]) != nullptr;
            }
            expect(!hasMidiReceive, "Cleared project route should not inject MidiReceive");

            bool hasAudioMonitor = false;
            if (auto* sourceTrack = bridge->getAudioTrack(sourceTrackId)) {
                for (int i = 0; i < sourceTrack->pluginList.size(); ++i)
                    hasAudioMonitor =
                        hasAudioMonitor || dynamic_cast<magda::AudioSidechainMonitorPlugin*>(
                                               sourceTrack->pluginList[i]) != nullptr;
            }
            expect(!hasAudioMonitor,
                   "Cleared project route should not retain an audio sidechain monitor");
        }

        trackManager.clearAllTracks();

        beginTest("Project load preserves a sidechain when the saved Faust source fails");

        const auto failedSourceTrackId = trackManager.createTrack("Failed sidechain source");
        const auto failedDestinationTrackId =
            trackManager.createTrack("Failed sidechain destination");
        const auto failedDeviceId = trackManager.addDeviceToTrack(
            failedDestinationTrackId, makeSavedFaust(failedSourceTrackId, kInvalidDsp));
        expect(failedDeviceId != magda::INVALID_DEVICE_ID,
               "Faust device with an invalid saved source should still be added");

        if (failedDeviceId != magda::INVALID_DEVICE_ID) {
            const auto failedPath =
                magda::ChainNodePath::topLevelDevice(failedDestinationTrackId, failedDeviceId);
            bridge->syncTrackPlugins(failedSourceTrackId);
            bridge->syncTrackPlugins(failedDestinationTrackId);

            auto failedPlugin = bridge->getPlugin(failedPath);
            auto* failedFaust = dynamic_cast<audio::FaustPlugin*>(failedPlugin.get());
            expect(failedFaust != nullptr, "Failed saved source should retain its Faust plugin");
            if (failedFaust)
                expect(!failedFaust->activeDspMatchesSource(),
                       "Fallback DSP should not be treated as the saved source");

            auto* failedDevice = trackManager.getDeviceInChainByPath(failedPath);
            expect(failedDevice != nullptr, "Failed-source DeviceInfo should resolve");
            if (failedDevice)
                expect(failedDevice->sidechain.isActive(),
                       "Compile failure should preserve the serialized sidechain");
        }

        trackManager.clearAllTracks();
        trackManager.setAudioEngine(nullptr);
    }
};

FaustSidechainTest faustSidechainTest;

}  // namespace
