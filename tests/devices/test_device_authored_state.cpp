#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../magda/daw/core/DeviceState.hpp"
#include "../../magda/daw/core/DeviceStateCommands.hpp"
#include "../../magda/daw/core/TrackManager.hpp"
#include "../../magda/daw/core/UndoManager.hpp"

using namespace magda;
namespace ds = magda::device_state;

// ============================================================================
// #2317 — authored-state edits on the model. No live engine in this binary:
// TrackManager has no audio engine, so the projection is skipped and what is
// under test is exactly the model-side contract.
// ============================================================================

namespace {

ChainNodePath addInternalDevice(const juce::String& pluginId, const juce::String& pluginState) {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    const auto trackId = tracks.createTrack("Authored", TrackType::Media);

    DeviceInfo device;
    device.name = pluginId;
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;
    device.pluginState = pluginState;
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

}  // namespace

TEST_CASE("An authored-state edit never re-encodes the retired parameter record",
          "[device-authored-state]") {
    // A pre-#2317 document still carrying its duplicate `params`. The edit is
    // the moment the document goes canonical: hydration consumed the record at
    // load, and writing it back would leave a second persisted authority alive
    // in every path that never passes through a Tracktion capture.
    ds::Doc oldDoc;
    oldDoc.deviceType = "arpeggiator";
    oldDoc.paramsAreDisplayDomain = true;
    oldDoc.params.push_back({0, "pattern", 2.0f});
    oldDoc.params.push_back({3, "gate", 0.8f});
    oldDoc.root.props.set(juce::Identifier("arpQuantizeSub"), 16);

    const auto path = addInternalDevice("arpeggiator", ds::encode(oldDoc));

    static const juce::Identifier quantizeSub("arpQuantizeSub");
    REQUIRE(TrackManager::getInstance().updateDeviceAuthoredState(
        path, [](ds::Doc& doc) { doc.root.props.set(quantizeSub, 8); }));

    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    const auto written = ds::decode(device->pluginState);
    REQUIRE(written.has_value());
    CHECK(written->params.empty());
    CHECK_FALSE(written->paramsAreDisplayDomain);
    CHECK(static_cast<int>(written->root.props[quantizeSub]) == 8);
    CHECK(written->deviceType == "arpeggiator");

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Authored-state edits refuse what they cannot read", "[device-authored-state]") {
    const juce::String futureDoc =
        "{\"schema\": 99, \"device\": \"arpeggiator\", \"somethingNewer\": true}";

    SECTION("updateDeviceAuthoredState leaves a future-schema document untouched") {
        const auto path = addInternalDevice("arpeggiator", futureDoc);
        CHECK_FALSE(TrackManager::getInstance().updateDeviceAuthoredState(
            path, [](ds::Doc& doc) { doc.root.props.set(juce::Identifier("x"), 1); }));
        const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
        REQUIRE(device != nullptr);
        CHECK(device->pluginState == futureDoc);
        TrackManager::getInstance().clearAllTracks();
    }

    SECTION("LoadImpulseResponseCommand::canExecute rejects future-schema state") {
        const auto path = addInternalDevice("magda_convolution", futureDoc);
        juce::MemoryBlock blob;
        blob.append("x", 1);
        LoadImpulseResponseCommand command(path, "Some IR", std::move(blob));
        CHECK_FALSE(command.canExecute());
        TrackManager::getInstance().clearAllTracks();
    }

    SECTION("LoadImpulseResponseCommand::canExecute rejects a non-convolution device") {
        const auto path = addInternalDevice("arpeggiator", {});
        juce::MemoryBlock blob;
        blob.append("x", 1);
        LoadImpulseResponseCommand command(path, "Some IR", std::move(blob));
        CHECK_FALSE(command.canExecute());
        TrackManager::getInstance().clearAllTracks();
    }
}
