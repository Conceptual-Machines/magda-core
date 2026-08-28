#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

// Undo of a structural edit restores the serialized model exactly (#2221).
//
// Move, paste, wrap and remove each save and restore their own idea of what
// they changed. The property that matters is not that they save something, but
// that what comes back serializes to the same bytes: an id allocated on the way
// out, a link retargeted and not put back, a notification without a matching
// mutation are all invisible to a test that only checks the device is present
// again. Comparing the serialized track is what catches them.

namespace {

void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    SelectionManager::getInstance().clearSelection();
    UndoManager::getInstance().clearHistory();
}

/// Every track, serialized, in order. The comparison subject.
juce::String snapshot() {
    juce::Array<juce::var> tracks;
    for (const auto& track : TrackManager::getInstance().getTracks())
        tracks.add(ProjectSerializer::serializeTrackInfo(track));
    return juce::JSON::toString(juce::var(tracks), false);
}

DeviceInfo effect(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "delay";
    device.format = PluginFormat::Internal;
    return device;
}

DeviceInfo drumGrid() {
    DeviceInfo device;
    device.name = "Drum Grid";
    device.pluginId = "drumgrid";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

/// Run @p command through the undo stack and assert the round trip.
///
/// Undo must restore the bytes the model had before, and redo must reproduce
/// the bytes it had after, so the operation is a single reversible step rather
/// than one that merely ends up somewhere plausible.
void requireUndoRoundTrip(std::unique_ptr<UndoableCommand> command) {
    auto& undo = UndoManager::getInstance();

    const auto before = snapshot();
    undo.executeCommand(std::move(command));
    const auto after = snapshot();
    REQUIRE(before != after);  // The operation did something to compare.

    REQUIRE(undo.undo());
    CHECK(snapshot() == before);

    REQUIRE(undo.redo());
    CHECK(snapshot() == after);
}

}  // namespace

TEST_CASE("Undoing a move restores the serialized model", "[structural][undo][roundtrip]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto first = tm.addDeviceToTrack(trackId, effect("First"));
    tm.addDeviceToTrack(trackId, effect("Second"));
    tm.addDeviceToTrack(trackId, effect("Third"));

    requireUndoRoundTrip(std::make_unique<MoveChainElementCommand>(
        ChainNodePath::topLevelDevice(trackId, first), ChainNodePath::trackLevel(trackId), 3));
}

TEST_CASE("Undoing a move into a rack chain restores the serialized model",
          "[structural][undo][roundtrip]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto deviceId = tm.addDeviceToTrack(trackId, effect("Mover"));
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto chainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));

    requireUndoRoundTrip(std::make_unique<MoveChainElementCommand>(
        ChainNodePath::topLevelDevice(trackId, deviceId),
        ChainNodePath::chain(trackId, rackId, chainId), 0));
}

TEST_CASE("Undoing a cross-track move restores the serialized model",
          "[structural][undo][roundtrip]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto source = tm.createTrack("Source");
    const auto destination = tm.createTrack("Destination");
    const auto deviceId = tm.addDeviceToTrack(source, effect("Mover"));
    tm.addDeviceToTrack(destination, effect("Resident"));

    requireUndoRoundTrip(
        std::make_unique<MoveChainElementCommand>(ChainNodePath::topLevelDevice(source, deviceId),
                                                  ChainNodePath::trackLevel(destination), 0));
}

TEST_CASE("Undoing a paste restores the serialized model", "[structural][undo][roundtrip]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Resident"));

    std::vector<ChainElement> pasted;
    pasted.push_back(makeDeviceElement(effect("Pasted")));

    requireUndoRoundTrip(std::make_unique<PasteChainElementsCommand>(
        ChainNodePath::trackLevel(trackId), std::move(pasted), 1));
}

TEST_CASE("Undoing a paste at an out-of-range index restores the serialized model",
          "[structural][undo][roundtrip]") {
    // The insert clamps the requested index against the destination as it was.
    // Bookkeeping that read the index back against the larger list started past
    // what had been inserted, so undo left pasted elements behind and redo
    // pasted another copy on top.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Resident"));

    std::vector<ChainElement> pasted;
    pasted.push_back(makeDeviceElement(effect("First")));
    pasted.push_back(makeDeviceElement(effect("Second")));

    requireUndoRoundTrip(std::make_unique<PasteChainElementsCommand>(
        ChainNodePath::trackLevel(trackId), std::move(pasted), 99));
}

TEST_CASE("Undoing a wrap restores the serialized model", "[structural][undo][roundtrip]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto first = tm.addDeviceToTrack(trackId, effect("First"));
    const auto second = tm.addDeviceToTrack(trackId, effect("Second"));

    requireUndoRoundTrip(std::make_unique<WrapChainElementsInRackCommand>(
        std::vector<ChainNodePath>{ChainNodePath::topLevelDevice(trackId, first),
                                   ChainNodePath::topLevelDevice(trackId, second)},
        "Wrapper"));
}

TEST_CASE("Undoing a wrap of a noncontiguous selection restores the serialized model",
          "[structural][undo][roundtrip]") {
    // The context menu accepts an arbitrary multi-selection. Reinserting every
    // wrapped element in one run from the lowest index closes the gaps between
    // them, which reorders the chain and so changes what it sounds like.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto first = tm.addDeviceToTrack(trackId, effect("First"));
    tm.addDeviceToTrack(trackId, effect("Gap"));
    const auto third = tm.addDeviceToTrack(trackId, effect("Third"));
    tm.addDeviceToTrack(trackId, effect("Fourth"));

    requireUndoRoundTrip(std::make_unique<WrapChainElementsInRackCommand>(
        std::vector<ChainNodePath>{ChainNodePath::topLevelDevice(trackId, first),
                                   ChainNodePath::topLevelDevice(trackId, third)},
        "Wrapper"));
}

TEST_CASE("Undoing a device removal restores the serialized model",
          "[structural][undo][roundtrip]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Keeper"));
    const auto doomed = tm.addDeviceToTrack(trackId, effect("Doomed"));
    tm.addDeviceToTrack(trackId, effect("Other"));

    requireUndoRoundTrip(std::make_unique<RemoveDeviceByPathCommand>(
        ChainNodePath::topLevelDevice(trackId, doomed)));
}

TEST_CASE("Undoing the removal of a routed grid restores the serialized model",
          "[structural][undo][roundtrip][pads]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGrid());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    DeviceInfo voice;
    voice.name = "Kick";
    voice.pluginId = "magdasampler";
    voice.format = PluginFormat::Internal;
    voice.isInstrument = true;
    voice.deviceType = DeviceType::Instrument;
    REQUIRE(tm.setPadDevice(gridPath, 0, voice) != INVALID_DEVICE_ID);
    REQUIRE(tm.setPadOutput(gridPath, 0, 1));

    UndoManager::getInstance().clearHistory();
    requireUndoRoundTrip(std::make_unique<RemoveDeviceByPathCommand>(gridPath));
}
