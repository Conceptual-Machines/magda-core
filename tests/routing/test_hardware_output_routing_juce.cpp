// Regression tests for #2264: every hardware output selection in the track
// output dropdowns routed straight back to master because the UI had no
// option-ID -> wave-output-device mapping. These cover the three pieces of the
// fix: populateAudioOutputOptions emitting the mapping, syncSelectorsFromTrack
// re-selecting a stored hardware destination (no snap-back to Master), and
// TrackController resolving the "stereo:" pair marker to the TE device.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "magda/daw/ui/components/mixer/RoutingSyncHelper.hpp"

using namespace magda;

namespace {

// Minimal stand-in so populateAudioOutputOptions sees "a device is active";
// channel layout comes from the enabledOutputChannels mask the tests pass.
class StubAudioIODevice final : public juce::AudioIODevice {
  public:
    explicit StubAudioIODevice(int numOutputs)
        : juce::AudioIODevice("Stub Device", "Stub"), numOutputs_(numOutputs) {}

    juce::StringArray getOutputChannelNames() override {
        juce::StringArray names;
        for (int i = 0; i < numOutputs_; ++i)
            names.add("Out " + juce::String(i + 1));
        return names;
    }
    juce::StringArray getInputChannelNames() override {
        return {};
    }
    juce::Array<double> getAvailableSampleRates() override {
        return {44100.0};
    }
    juce::Array<int> getAvailableBufferSizes() override {
        return {512};
    }
    int getDefaultBufferSize() override {
        return 512;
    }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override {
        return {};
    }
    void close() override {}
    bool isOpen() override {
        return false;
    }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override {
        return false;
    }
    juce::String getLastError() override {
        return {};
    }
    int getCurrentBufferSizeSamples() override {
        return 512;
    }
    double getCurrentSampleRate() override {
        return 44100.0;
    }
    int getCurrentBitDepth() override {
        return 24;
    }
    juce::BigInteger getActiveOutputChannels() const override {
        juce::BigInteger b;
        b.setRange(0, numOutputs_, true);
        return b;
    }
    juce::BigInteger getActiveInputChannels() const override {
        return {};
    }
    int getOutputLatencyInSamples() override {
        return 0;
    }
    int getInputLatencyInSamples() override {
        return 0;
    }

  private:
    int numOutputs_;
};

}  // namespace

class HardwareOutputRoutingTest final : public juce::UnitTest {
  public:
    HardwareOutputRoutingTest() : juce::UnitTest("Hardware Output Routing Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] {
            testOptionToDeviceMapping();
            testSelectorRoundTrip();
            testControllerResolvesStereoMarker();
        });
    }

  private:
    void testOptionToDeviceMapping() {
        beginTest("populateAudioOutputOptions maps option IDs to wave output devices");

        StubAudioIODevice device(4);
        RoutingSelector selector(RoutingSelector::Type::AudioOut);
        std::map<int, TrackId> trackMapping;
        std::map<int, juce::String> channelMapping;
        juce::BigInteger enabled;
        enabled.setRange(0, 4, true);
        const std::map<int, juce::String> teNames = {
            {0, "Out 1 + 2"}, {1, "Out 1 + 2"}, {2, "Out 3 + 4"}, {3, "Out 3 + 4"}};

        RoutingSyncHelper::populateAudioOutputOptions(
            &selector, INVALID_TRACK_ID, &device, trackMapping, enabled, &channelMapping, teNames);

        // Stereo pairs (ID 10+) carry the pair marker; monos (ID 100+) the bare name
        expectEquals(channelMapping[10], juce::String("stereo:Out 1 + 2"));
        expectEquals(channelMapping[11], juce::String("stereo:Out 3 + 4"));
        expectEquals(channelMapping[100], juce::String("Out 1 + 2"));
        expectEquals(channelMapping[102], juce::String("Out 3 + 4"));

        // Without TE device names the mapping falls back to positional names
        std::map<int, juce::String> fallbackMapping;
        RoutingSyncHelper::populateAudioOutputOptions(&selector, INVALID_TRACK_ID, &device,
                                                      trackMapping, enabled, &fallbackMapping);
        expectEquals(fallbackMapping[10], juce::String("stereo:Out 1"));
        expectEquals(fallbackMapping[101], juce::String("Out 2"));
    }

    void testSelectorRoundTrip() {
        beginTest("syncSelectorsFromTrack re-selects a stored hardware destination");

        StubAudioIODevice device(4);
        RoutingSelector selector(RoutingSelector::Type::AudioOut);
        std::map<int, TrackId> outputTrackMapping;
        std::map<int, TrackId> midiOutputTrackMapping;
        std::map<int, juce::String> channelMapping;
        juce::BigInteger enabled;
        enabled.setRange(0, 4, true);
        const std::map<int, juce::String> teNames = {
            {0, "Out 1 + 2"}, {1, "Out 1 + 2"}, {2, "Out 3 + 4"}, {3, "Out 3 + 4"}};

        TrackInfo track;
        track.audioOutputDevice = "stereo:Out 3 + 4";

        RoutingSyncHelper::syncSelectorsFromTrack(
            track, nullptr, nullptr, &selector, nullptr, nullptr, &device, INVALID_TRACK_ID,
            outputTrackMapping, midiOutputTrackMapping, nullptr, {}, enabled, nullptr, {}, nullptr,
            &channelMapping, teNames);

        // The dropdown must land on the second stereo pair, not snap back to Master
        expectEquals(selector.getSelectedId(), 11);

        track.audioOutputDevice = "Out 1 + 2";  // mono/bare device selection
        RoutingSyncHelper::syncSelectorsFromTrack(
            track, nullptr, nullptr, &selector, nullptr, nullptr, &device, INVALID_TRACK_ID,
            outputTrackMapping, midiOutputTrackMapping, nullptr, {}, enabled, nullptr, {}, nullptr,
            &channelMapping, teNames);
        expectEquals(selector.getSelectedId(), 100);
    }

    void testControllerResolvesStereoMarker() {
        beginTest("TrackController strips the stereo: marker before routing to TE");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        const auto trackId = tm.createTrack("HW Out");
        bridge->createAudioTrack(trackId, "HW Out");

        bridge->setTrackAudioOutput(trackId, "stereo:Speakers L + R");
        expectEquals(bridge->getTrackAudioOutput(trackId), juce::String("Speakers L + R"));

        // If real wave output devices exist, a pair selection must resolve to one
        const auto outputNames = bridge->getOutputDeviceNamesByChannel();
        if (!outputNames.empty()) {
            const auto deviceName = outputNames.begin()->second;
            bridge->setTrackAudioOutput(trackId, "stereo:" + deviceName);
            expectEquals(bridge->getTrackAudioOutput(trackId), deviceName);
        }

        // And master must still round-trip as the default output
        bridge->setTrackAudioOutput(trackId, "master");
        expectEquals(bridge->getTrackAudioOutput(trackId), juce::String("master"));
    }
};

static HardwareOutputRoutingTest hardwareOutputRoutingTest;
