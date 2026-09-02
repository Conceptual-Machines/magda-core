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

TEST_CASE("Undoing an authored-state command cannot resurrect the retired record",
          "[device-authored-state]") {
    // The command snapshots DeviceInfo::pluginState verbatim, and a device
    // that has not been saved since #2317 still holds a document WITH the
    // retired `params`. The canonicalization therefore lives at the write
    // boundary (setDeviceAuthoredState), not in the forward edit alone:
    // execute -> undo must leave a canonical document, not the pre-#2317 one.
    ds::Doc oldDoc;
    oldDoc.deviceType = "magda_convolution";
    oldDoc.params.push_back({0, "gain", 0.5f});
    oldDoc.root.props.set(juce::Identifier("normalise"), true);

    const auto path = addInternalDevice("magda_convolution", ds::encode(oldDoc));

    juce::MemoryBlock blob;
    blob.append("notrealaudio", 12);
    LoadImpulseResponseCommand command(path, "Test IR", std::move(blob));
    REQUIRE(command.canExecute());
    command.execute();
    REQUIRE(command.wasExecuted());

    auto& tracks = TrackManager::getInstance();
    const auto* device = tracks.getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    {
        const auto written = ds::decode(device->pluginState);
        REQUIRE(written.has_value());
        CHECK(written->params.empty());
        CHECK(written->root.props.contains(juce::Identifier("irFileData")));
    }

    command.undo();
    {
        const auto restored = ds::decode(device->pluginState);
        REQUIRE(restored.has_value());
        CHECK(restored->params.empty());
        CHECK_FALSE(restored->root.props.contains(juce::Identifier("irFileData")));
        // The authored state itself came back with the snapshot.
        CHECK(static_cast<bool>(restored->root.props[juce::Identifier("normalise")]));
    }

    tracks.clearAllTracks();
}

TEST_CASE("Snapshot replacement validates the incoming state too", "[device-authored-state]") {
    auto& tracks = TrackManager::getInstance();

    SECTION("a future-schema snapshot is refused, not stored unreadable") {
        const auto path = addInternalDevice("arpeggiator", {});
        const juce::String futureDoc =
            "{\"schema\": 99, \"device\": \"arpeggiator\", \"somethingNewer\": true}";
        CHECK_FALSE(tracks.setDeviceAuthoredState(path, futureDoc));
        const auto* device = tracks.getDeviceInChainByPath(path);
        REQUIRE(device != nullptr);
        CHECK(device->pluginState.isEmpty());
        tracks.clearAllTracks();
    }

    SECTION("another device's document is refused") {
        const auto path = addInternalDevice("arpeggiator", {});
        ds::Doc wrongDevice;
        wrongDevice.deviceType = "magda_convolution";
        wrongDevice.root.props.set(juce::Identifier("normalise"), true);
        CHECK_FALSE(tracks.setDeviceAuthoredState(path, ds::encode(wrongDevice)));
        const auto* device = tracks.getDeviceInChainByPath(path);
        REQUIRE(device != nullptr);
        CHECK(device->pluginState.isEmpty());
        tracks.clearAllTracks();
    }
}

TEST_CASE("Snapshot replacement passes through what decode refuses", "[device-authored-state]") {
    // Legacy engine XML is not a v2 document; a snapshot of it restores
    // verbatim rather than being mangled into one.
    const juce::String legacyXml = "<PLUGIN type=\"arpeggiator\" arpQuantizeSub=\"8\"/>";
    const auto path = addInternalDevice("arpeggiator", {});
    REQUIRE(TrackManager::getInstance().setDeviceAuthoredState(path, legacyXml));
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    CHECK(device->pluginState == legacyXml);
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
