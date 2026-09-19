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
#include "magda/daw/audio/io/HardwareChannels.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "magda/daw/ui/components/mixer/RoutingSyncHelper.hpp"

using namespace magda;

namespace {

/** @brief @p count outputs named "Out N", with @p open of them open under @p routeNames. */
HardwareChannels::Direction outputsOf(int count, juce::BigInteger open,
                                      std::map<int, juce::String> routeNames = {}) {
    HardwareChannels::Direction outputs{.open = std::move(open),
                                        .routeNames = std::move(routeNames)};
    for (int i = 0; i < count; ++i)
        outputs.channelNames.add("Out " + juce::String(i + 1));
    return outputs;
}

/** @brief An interface whose outputs are fixed, standing in for either engine's. */
class FixedHardware final : public HardwareChannels {
  public:
    explicit FixedHardware(Direction outputs, Direction inputs = {})
        : outputs_(std::move(outputs)), inputs_(std::move(inputs)) {}

    bool isOpen() const override {
        return true;
    }
    Direction inputs() const override {
        return inputs_;
    }
    Direction outputs() const override {
        return outputs_;
    }

  private:
    Direction outputs_;
    Direction inputs_;
};

}  // namespace

class HardwareOutputRoutingTest final : public juce::UnitTest {
  public:
    HardwareOutputRoutingTest() : juce::UnitTest("Hardware Output Routing Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] {
            testOptionToDeviceMapping();
            testOutputChannelMaskPresence();
            testSelectorRoundTrip();
            testMissingRoutes();
            testControllerResolvesStereoMarker();
        });
    }

  private:
    void testOptionToDeviceMapping() {
        beginTest("populateAudioOutputOptions maps option IDs to wave output devices");

        RoutingSelector selector(RoutingSelector::Type::AudioOut);
        std::map<int, TrackId> trackMapping;
        std::map<int, juce::String> channelMapping;
        juce::BigInteger enabled;
        enabled.setRange(0, 5, true);
        // Two stereo-pair devices plus a trailing mono device
        const std::map<int, juce::String> teNames = {
            {0, "Out 1 + 2"}, {1, "Out 1 + 2"}, {2, "Out 3 + 4"}, {3, "Out 3 + 4"}, {4, "Out 5"}};

        RoutingSyncHelper::populateAudioOutputOptions(&selector, INVALID_TRACK_ID,
                                                      outputsOf(5, enabled, teNames), trackMapping,
                                                      &channelMapping);

        // Stereo pairs (ID 10+) carry the pair marker; the mono device (ID
        // 100+) the bare name. Channels inside a pair get no mono option — TE
        // routes a track output to a whole wave device only.
        expectEquals(channelMapping[10], juce::String("stereo:Out 1 + 2"));
        expectEquals(channelMapping[11], juce::String("stereo:Out 3 + 4"));
        expectEquals(channelMapping[100], juce::String("Out 5"));
        expectEquals(static_cast<int>(channelMapping.size()), 3);

        // Distinct mono devices must stay distinguishable in the model
        std::map<int, juce::String> monoMapping;
        juce::BigInteger twoChannels;
        twoChannels.setRange(0, 2, true);
        const std::map<int, juce::String> monoNames = {{0, "Main L"}, {1, "Main R"}};
        RoutingSyncHelper::populateAudioOutputOptions(&selector, INVALID_TRACK_ID,
                                                      outputsOf(5, twoChannels, monoNames),
                                                      trackMapping, &monoMapping);
        expectEquals(monoMapping[100], juce::String("Main L"));
        expectEquals(monoMapping[101], juce::String("Main R"));
        expectEquals(static_cast<int>(monoMapping.size()), 2);

        // Without TE device names the mapping falls back to positional pairing
        std::map<int, juce::String> fallbackMapping;
        RoutingSyncHelper::populateAudioOutputOptions(
            &selector, INVALID_TRACK_ID, outputsOf(5, enabled), trackMapping, &fallbackMapping);
        expectEquals(fallbackMapping[10], juce::String("stereo:Out 1"));
        expectEquals(fallbackMapping[11], juce::String("stereo:Out 3"));
        expectEquals(fallbackMapping[100], juce::String("Out 5"));
    }

    void testOutputChannelMaskPresence() {
        beginTest("Only open outputs are offered");

        RoutingSelector selector(RoutingSelector::Type::AudioOut);
        std::map<int, TrackId> trackMapping;
        std::map<int, juce::String> channelMapping;

        RoutingSyncHelper::populateAudioOutputOptions(&selector, INVALID_TRACK_ID,
                                                      outputsOf(4, juce::BigInteger{}),
                                                      trackMapping, &channelMapping);
        expect(channelMapping.empty(), "An interface with nothing open offers no outputs");

        RoutingSyncHelper::populateAudioOutputOptions(&selector, INVALID_TRACK_ID, std::nullopt,
                                                      trackMapping, &channelMapping);
        expect(channelMapping.empty(), "No interface offers no outputs");

        juce::BigInteger firstPair;
        firstPair.setRange(0, 2, true);
        RoutingSyncHelper::populateAudioOutputOptions(
            &selector, INVALID_TRACK_ID, outputsOf(4, firstPair), trackMapping, &channelMapping);
        expectEquals(selector.getFirstChannelOptionId(), 10);
        expectEquals(static_cast<int>(channelMapping.size()), 1,
                     "Channels 3-4 are not offered while closed");
    }

    void testSelectorRoundTrip() {
        beginTest("syncSelectorsFromTrack re-selects a stored hardware destination");

        RoutingSelector selector(RoutingSelector::Type::AudioOut);
        std::map<int, TrackId> outputTrackMapping;
        std::map<int, TrackId> midiOutputTrackMapping;
        std::map<int, juce::String> channelMapping;
        juce::BigInteger enabled;
        enabled.setRange(0, 5, true);
        const FixedHardware hardware(outputsOf(5, enabled,
                                               {{0, "Out 1 + 2"},
                                                {1, "Out 1 + 2"},
                                                {2, "Out 3 + 4"},
                                                {3, "Out 3 + 4"},
                                                {4, "Out 5"}}));

        TrackInfo track;
        track.audioOutputDevice = "stereo:Out 3 + 4";

        RoutingSyncHelper::syncSelectorsFromTrack(
            track, nullptr, nullptr, &selector, nullptr, nullptr, &hardware, INVALID_TRACK_ID,
            outputTrackMapping, midiOutputTrackMapping, nullptr, nullptr, nullptr, &channelMapping);

        // The dropdown must land on the second stereo pair, not snap back to Master
        expectEquals(selector.getSelectedId(), 11);

        track.audioOutputDevice = "Out 3 + 4";  // legacy bare pair name
        selector.setSelectedId(1);
        RoutingSyncHelper::syncSelectorsFromTrack(
            track, nullptr, nullptr, &selector, nullptr, nullptr, &hardware, INVALID_TRACK_ID,
            outputTrackMapping, midiOutputTrackMapping, nullptr, nullptr, nullptr, &channelMapping);
        expectEquals(selector.getSelectedId(), 11);

        track.audioOutputDevice = "stereo:Out 3";  // old native physical alias
        selector.setSelectedId(1);
        RoutingSyncHelper::syncSelectorsFromTrack(
            track, nullptr, nullptr, &selector, nullptr, nullptr, &hardware, INVALID_TRACK_ID,
            outputTrackMapping, midiOutputTrackMapping, nullptr, nullptr, nullptr, &channelMapping);
        expectEquals(selector.getSelectedId(), 11);

        track.audioOutputDevice = "Out 5";  // mono device selection
        selector.setSelectedId(1);
        RoutingSyncHelper::syncSelectorsFromTrack(
            track, nullptr, nullptr, &selector, nullptr, nullptr, &hardware, INVALID_TRACK_ID,
            outputTrackMapping, midiOutputTrackMapping, nullptr, nullptr, nullptr, &channelMapping);
        expectEquals(selector.getSelectedId(), 100);
    }

    void testMissingRoutes() {
        beginTest("A saved route no open channel carries shows as missing");

        juce::BigInteger firstPair;
        firstPair.setRange(0, 2, true);
        HardwareChannels::Direction inputs{.open = firstPair,
                                           .channelNames = {"In 1", "In 2"},
                                           .routeNames = {{0, "In 1"}, {1, "In 2"}}};
        const FixedHardware hardware(outputsOf(4, firstPair, {{0, "Out 1 + 2"}, {1, "Out 1 + 2"}}),
                                     inputs);

        RoutingSelector output(RoutingSelector::Type::AudioOut);
        RoutingSelector input(RoutingSelector::Type::AudioIn);
        std::map<int, TrackId> outputTracks, midiOutputTracks, inputTracks;
        std::map<int, juce::String> outputChannels, inputChannels;
        const auto sync = [&](const TrackInfo& track) {
            RoutingSyncHelper::syncSelectorsFromTrack(track, &input, nullptr, &output, nullptr,
                                                      nullptr, &hardware, INVALID_TRACK_ID,
                                                      outputTracks, midiOutputTracks, &inputTracks,
                                                      &inputChannels, nullptr, &outputChannels);
        };

        TrackInfo track;
        track.audioOutputDevice = "stereo:Out 3 + 4";
        track.audioInputDevice = "In 3";
        sync(track);

        expectEquals(output.getSelectedId(), RoutingSyncHelper::kMissingRouteId);
        expectEquals(output.getSelectedName(), juce::String("Out 3 + 4 (missing)"));
        expectEquals(outputChannels[RoutingSyncHelper::kMissingRouteId],
                     juce::String("stereo:Out 3 + 4"));
        expectEquals(input.getSelectedId(), RoutingSyncHelper::kMissingRouteId);
        expectEquals(input.getSelectedName(), juce::String("In 3 (missing)"));

        // "default" is the first channel wherever the interface is, never missing.
        track.audioInputDevice = "default";
        sync(track);
        expectEquals(input.getSelectedId(), input.getFirstChannelOptionId());

        track.audioOutputDevice = "stereo:Out 1 + 2";
        sync(track);
        expectEquals(output.getSelectedId(), 10);
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
