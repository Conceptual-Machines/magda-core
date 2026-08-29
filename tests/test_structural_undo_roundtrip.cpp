#include <catch2/catch_test_macros.hpp>

#include "StructuralRoundTrip.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;
using magda::structural_test::resetState;
using magda::structural_test::snapshot;

// Undo of a structural edit restores the serialized model exactly (#2221).
//
// Move, paste, wrap and remove each save and restore their own idea of what
// they changed. The property that matters is not that they save something, but
// that what comes back serializes to the same bytes: an id allocated on the way
// out, a link retargeted and not put back, a notification without a matching
// mutation are all invisible to a test that only checks the device is present
// again. Comparing the serialized track is what catches them.
//
// Each case here is one bug that was found this way. The generated sweep over
// every operation, container and subject is in test_structural_matrix.cpp
// (#2229), against this same oracle.

namespace {

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

// The five below are what the generated matrix found (#2229). Each is kept as
// its own case because a sweep says a cell failed and a case says what broke.

TEST_CASE("Undoing a move towards the front of a chain puts it back where it stood",
          "[structural][undo][roundtrip]") {
    // The index a move records is where the element stood, counted with itself
    // still in the list. `moveChainElement()` takes a drop position and drops
    // one when the element travels up a list it is already in, so handing it the
    // recorded index straight back landed the element one slot early. Only
    // visible undoing a move that went *towards the front*: the other direction
    // needs no correction, which is why the existing case above passed.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("First"));
    tm.addDeviceToTrack(trackId, effect("Second"));
    const auto third = tm.addDeviceToTrack(trackId, effect("Third"));

    requireUndoRoundTrip(std::make_unique<MoveChainElementCommand>(
        ChainNodePath::topLevelDevice(trackId, third), ChainNodePath::trackLevel(trackId), 0));
}

TEST_CASE("Undoing a paste of a rack into a track's own list removes it",
          "[structural][undo][roundtrip]") {
    // The paste recorded the rack under `trackLevel(track).withRack(id)`, a path
    // claiming to be both the track and a rack in it, which reads back as the
    // track. The undo asked what kind of node it was, got "track", and removed
    // nothing.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Resident"));

    RackInfo rack;
    rack.name = "Pasted";
    ChainInfo chain;
    chain.name = "Chain 1";
    chain.elements.push_back(makeDeviceElement(effect("Inside")));
    rack.chains.push_back(std::move(chain));

    std::vector<ChainElement> pasted;
    pasted.push_back(makeRackElement(std::move(rack)));

    requireUndoRoundTrip(std::make_unique<PasteChainElementsCommand>(
        ChainNodePath::trackLevel(trackId), std::move(pasted), 0));
}

TEST_CASE("Undoing a wrap inside a pad chain restores the serialized model",
          "[structural][undo][roundtrip][pads]") {
    // The wrap read its new rack's chain id back through the rack walk, which no
    // pad address answers to, so it never learned the id and its undo bailed
    // before touching anything: the rack stayed.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGrid());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    REQUIRE(padChainId != INVALID_CHAIN_ID);

    const auto padChain = ChainNodePath::padChain(trackId, gridId, padChainId);
    const auto deviceId = tm.addDeviceToPad(gridPath, padChainId, effect("Pad FX"));
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    UndoManager::getInstance().clearHistory();
    requireUndoRoundTrip(std::make_unique<WrapChainElementsInRackCommand>(
        std::vector<ChainNodePath>{padChain.withDevice(deviceId)}, "Wrapper"));
}

TEST_CASE("Redoing a track duplication reproduces the track it made",
          "[structural][undo][roundtrip]") {
    // A redo used to duplicate again, which allocates a fresh TrackId and fresh
    // device, rack and chain ids: undo followed by redo left a different project
    // than the one the undo took away, and every link made against the first
    // duplicate pointed at nothing.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Delay"));
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto chainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    tm.addDeviceToChainByPath(ChainNodePath::chain(trackId, rackId, chainId), effect("Nested"));
    tm.createTrack("After");

    requireUndoRoundTrip(std::make_unique<DuplicateTrackCommand>(trackId));
}

TEST_CASE("Undoing a track deletion puts the track back in its place",
          "[structural][undo][roundtrip]") {
    // The restore appended, so undoing the deletion of a track from the middle
    // of a project moved it to the end. Same defect as the duplicate's redo,
    // through the same restore.
    resetState();
    auto& tm = TrackManager::getInstance();

    tm.createTrack("Before");
    const auto doomed = tm.createTrack("Doomed");
    tm.createTrack("After");

    requireUndoRoundTrip(std::make_unique<DeleteTrackCommand>(doomed));
}

TEST_CASE("Undoing the deletion of a middle child puts it back among its siblings",
          "[structural][undo][roundtrip]") {
    // A track belongs to two orders, and the restore only put back the first.
    // A group's order is its `childIds` order, so a middle child restored at the
    // end of that list has moved within the group even though its place in the
    // project is right.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto groupId = tm.createGroupTrack("Group");
    const auto first = tm.createTrack("First");
    const auto middle = tm.createTrack("Middle");
    const auto last = tm.createTrack("Last");
    tm.addTrackToGroup(first, groupId);
    tm.addTrackToGroup(middle, groupId);
    tm.addTrackToGroup(last, groupId);

    const auto* group = tm.getTrack(groupId);
    REQUIRE(group != nullptr);
    REQUIRE(group->childIds == std::vector<TrackId>{first, middle, last});

    requireUndoRoundTrip(std::make_unique<DeleteTrackCommand>(middle));
}

TEST_CASE("Undoing the deletion of a group brings its children back with it",
          "[structural][undo][roundtrip]") {
    // Deleting a group deletes its children, and the command stored only the
    // track the user named. Undo restored the group alone: its tracks, their
    // devices and their clips were gone for good, and the restored group listed
    // children that no longer existed.
    resetState();
    auto& tm = TrackManager::getInstance();

    tm.createTrack("Before");
    const auto groupId = tm.createGroupTrack("Group");
    const auto first = tm.createTrack("First");
    const auto second = tm.createTrack("Second");
    tm.addTrackToGroup(first, groupId);
    tm.addTrackToGroup(second, groupId);
    tm.addDeviceToTrack(first, effect("Child FX"));
    const auto rackId = tm.addRackToTrack(second, "Child Rack");
    tm.addChainToRack(ChainNodePath::rack(second, rackId));
    tm.createTrack("After");

    requireUndoRoundTrip(std::make_unique<DeleteTrackCommand>(groupId));
}

TEST_CASE("Undoing a track deletion puts back the routing it swept up",
          "[structural][undo][roundtrip]") {
    // The deletion reaches outside the track it removes: every send aimed at it
    // goes, so does the input of anything listening to it, and so does every
    // sidechain on it. None of that is inside the deleted subtree, so an undo
    // that restored only the subtree gave back a project that had permanently
    // lost them.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto auxId = tm.createTrack("Aux", TrackType::Aux);
    const auto sender = tm.createTrack("Sender");
    const auto listener = tm.createTrack("Listener");
    const auto ducked = tm.createTrack("Ducked");

    tm.addSend(sender, auxId);
    tm.setTrackAudioInput(listener, "track:" + juce::String(auxId));
    tm.setTrackMidiInput(listener, "track:" + juce::String(auxId));

    const auto compressor = tm.addDeviceToTrack(ducked, effect("Compressor"));
    tm.setSidechainSource(compressor, auxId, SidechainConfig::Type::Audio);

    const auto* senderTrack = tm.getTrack(sender);
    REQUIRE(senderTrack != nullptr);
    REQUIRE(senderTrack->sends.size() == 1);

    UndoManager::getInstance().clearHistory();
    requireUndoRoundTrip(std::make_unique<DeleteTrackCommand>(auxId));
}

TEST_CASE("Undoing a rack removal brings back everything that was inside it",
          "[structural][undo][roundtrip]") {
    // Nothing undid a rack removal at all: the chain view called
    // removeRackFromChainByPath() straight off the model, so deleting a rack
    // took its chains, its devices and their links with it for good. It was the
    // one operation the transition matrix had to exclude by name (#2232).
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Before"));
    const auto rackId = tm.addRackToTrack(trackId, "Doomed");
    tm.addDeviceToTrack(trackId, effect("After"));

    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto firstChain = tm.addChainToRack(rackPath);
    const auto secondChain = tm.addChainToRack(rackPath);
    tm.addDeviceToChainByPath(rackPath.withChain(firstChain), effect("In Chain One"));
    tm.addDeviceToChainByPath(rackPath.withChain(secondChain), effect("In Chain Two"));

    // A rack inside the rack, so the restore has to carry a subtree rather than
    // a flat list of devices.
    const auto nestedId = tm.addRackToChainByPath(rackPath.withChain(firstChain), "Nested");
    const auto nestedPath = rackPath.withChain(firstChain).withRack(nestedId);
    tm.addDeviceToChainByPath(nestedPath.withChain(tm.addChainToRack(nestedPath)), effect("Deep"));

    requireUndoRoundTrip(std::make_unique<RemoveRackByPathCommand>(rackPath));
}

TEST_CASE("Undoing a chain removal puts the chain back among its siblings",
          "[structural][undo][roundtrip]") {
    // Same gap one level down, and with the same trap the track restores had:
    // an append puts a middle chain back last, which reorders the rack.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto rackPath = ChainNodePath::rack(trackId, rackId);

    tm.addDeviceToChainByPath(rackPath.withChain(tm.addChainToRack(rackPath)), effect("First"));
    const auto middle = tm.addChainToRack(rackPath);
    tm.addDeviceToChainByPath(rackPath.withChain(middle), effect("Middle"));
    tm.addDeviceToChainByPath(rackPath.withChain(tm.addChainToRack(rackPath)), effect("Last"));

    requireUndoRoundTrip(std::make_unique<RemoveChainByPathCommand>(rackPath.withChain(middle)));
}

TEST_CASE("Undoing a post-fader device removal puts it back at its index",
          "[structural][undo][roundtrip]") {
    // The flat sections hold bare devices rather than chain elements, so the
    // index lookup the removal records had no answer for them and the command
    // stopped before mutating: deleting a post-fader device from the UI worked
    // only because the UI was not going through the command (#2232).
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToPostFx(trackId, effect("First"));
    const auto middle = tm.addDeviceToPostFx(trackId, effect("Middle"));
    tm.addDeviceToPostFx(trackId, effect("Last"));

    requireUndoRoundTrip(
        std::make_unique<RemoveDeviceByPathCommand>(ChainNodePath::postFxDevice(trackId, middle)));
}

TEST_CASE("Removing a rack prunes a multi-node selection rather than clearing it",
          "[structural][selection]") {
    // `clearSelectionForDeletedChainNode()` had no MultiChainNode case at all,
    // so a Cmd-selected set holding one node inside a removed subtree kept that
    // path and every multi-node operation ran against freed model. The members
    // that did not go have to survive: dropping the whole set is a different
    // and equally wrong answer (#2232).
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& selection = SelectionManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto keptId = tm.addDeviceToTrack(trackId, effect("Kept"));
    const auto rackId = tm.addRackToTrack(trackId, "Doomed");
    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto chainId = tm.addChainToRack(rackPath);
    const auto doomedId = tm.addDeviceToChainByPath(rackPath.withChain(chainId), effect("Doomed"));

    const auto keptPath = ChainNodePath::topLevelDevice(trackId, keptId);
    const auto doomedPath = rackPath.withChain(chainId).withDevice(doomedId);
    selection.selectChainNodes({keptPath, doomedPath});
    REQUIRE(selection.getSelectedChainNodes().size() == 2);

    UndoManager::getInstance().executeCommand(std::make_unique<RemoveRackByPathCommand>(rackPath));

    const auto remaining = selection.getSelectedChainNodes();
    REQUIRE(remaining.size() == 1);
    CHECK(remaining.front() == keptPath);
    // One member left, so it is an ordinary single selection again, and the
    // primary has moved off the node that went.
    CHECK(selection.getSelectedChainNode() == keptPath);
}

TEST_CASE("Removing a rack clears a multi-node selection that was entirely inside it",
          "[structural][selection]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& selection = SelectionManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto rackId = tm.addRackToTrack(trackId, "Doomed");
    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto chainId = tm.addChainToRack(rackPath);
    const auto chainPath = rackPath.withChain(chainId);
    const auto firstId = tm.addDeviceToChainByPath(chainPath, effect("First"));
    const auto secondId = tm.addDeviceToChainByPath(chainPath, effect("Second"));

    selection.selectChainNodes({chainPath.withDevice(firstId), chainPath.withDevice(secondId)});
    REQUIRE(selection.getSelectedChainNodes().size() == 2);

    UndoManager::getInstance().executeCommand(std::make_unique<RemoveRackByPathCommand>(rackPath));

    CHECK(selection.getSelectedChainNodes().empty());
    CHECK_FALSE(selection.getSelectedChainNode().isValid());
}

TEST_CASE("Removing a rack drops a selection standing on a pad inside it",
          "[structural][selection]") {
    // A device is not always a leaf. A Drum Grid's pads are chains on the
    // device (#2207), addressed off the track by the grid's own DeviceId --
    // `PadRack(grid) > PadChain(pad)` -- so neither the grid's path nor its
    // DeviceId matches anything under it, and the subtree walk stopped at the
    // grid. A pad selected when the rack around it went kept pointing into the
    // erased subtree (#2232).
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& selection = SelectionManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto chainId = tm.addChainToRack(rackPath);
    const auto gridId = tm.addDeviceToChainByPath(rackPath.withChain(chainId), drumGrid());

    const auto gridPath = rackPath.withChain(chainId).withDevice(gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    REQUIRE(padChainId != INVALID_CHAIN_ID);

    const auto padPath = TrackManager::padChainPath(gridPath, padChainId);
    selection.selectChainNode(padPath);
    REQUIRE(selection.getSelectedChainNode() == padPath);

    UndoManager::getInstance().executeCommand(std::make_unique<RemoveRackByPathCommand>(rackPath));

    CHECK_FALSE(selection.getSelectedChainNode().isValid());
}

TEST_CASE("Removing a Drum Grid drops a selection standing on one of its pads",
          "[structural][selection]") {
    // The same leaf assumption on the direct route: deleting the grid itself
    // cleared only the grid's own path.
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& selection = SelectionManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGrid());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    const auto padChainId = tm.ensurePad(gridPath, 0);
    REQUIRE(padChainId != INVALID_CHAIN_ID);
    const auto padDeviceId = tm.addDeviceToPad(gridPath, padChainId, effect("Pad FX"));
    REQUIRE(padDeviceId != INVALID_DEVICE_ID);

    // The device on the pad, not the pad itself: it is a step deeper again, and
    // its DeviceId is not the grid's, so nothing about the grid's own path
    // reaches it.
    const auto padDevicePath =
        TrackManager::padChainPath(gridPath, padChainId).withDevice(padDeviceId);
    selection.selectChainNode(padDevicePath);
    REQUIRE(selection.getSelectedChainNode() == padDevicePath);

    UndoManager::getInstance().executeCommand(
        std::make_unique<RemoveDeviceByPathCommand>(gridPath));

    CHECK_FALSE(selection.getSelectedChainNode().isValid());
}

TEST_CASE("Removing a rack drops a selection standing inside it", "[structural][selection]") {
    // `clearSelectionForDeletedChainNode()` matches an exact path or a device
    // id, never an ancestor, so removing a container left a selection below it
    // pointing at model that had just been freed. The pad removals walked the
    // subtree for this reason; the rack and chain removals did not (#2232).
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& selection = SelectionManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto chainId = tm.addChainToRack(rackPath);
    const auto deviceId = tm.addDeviceToChainByPath(rackPath.withChain(chainId), effect("Buried"));

    const auto devicePath = rackPath.withChain(chainId).withDevice(deviceId);
    selection.selectChainNode(devicePath);
    REQUIRE(selection.getSelectedChainNode() == devicePath);

    UndoManager::getInstance().executeCommand(std::make_unique<RemoveRackByPathCommand>(rackPath));

    CHECK_FALSE(selection.getSelectedChainNode().isValid());
}
