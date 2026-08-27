#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/core/ChainNodePath.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/MacroInfo.hpp"
#include "magda/daw/core/PadPathMigration.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

// Typed pad path steps (#2219).
//
// A pad device's address names its owning Drum Grid by that device's own id.
// Spelled as an ordinary `Rack > Chain` pair it was indistinguishable from a
// route through an allocated rack that happened to share the number, so every
// resolver and remapper recognised pads by the shape of the path and by trying
// the routes in a fixed order. `PadRack` and `PadChain` say which id is which,
// and these pin that the answer no longer depends on either.

namespace {

void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    SelectionManager::getInstance().clearSelection();
    UndoManager::getInstance().clearHistory();
}

DeviceInfo drumGridDevice() {
    DeviceInfo device;
    device.name = "Drum Grid";
    device.pluginId = "drumgrid";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

DeviceInfo padVoice(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "magdasampler";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

/// The untyped spelling a project saved before the pad step types existed.
ChainNodePath asLegacySpelling(ChainNodePath path) {
    REQUIRE(path.isPadOwned());
    path.steps[0].type = ChainStepType::Rack;
    path.steps[1].type = ChainStepType::Chain;
    return path;
}

}  // namespace

TEST_CASE("A pad path says which of its ids is a device and which is a chain",
          "[drumgrid][pads][path]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);

    const auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);

    const auto padPath = TrackManager::padChainPath(gridPath, pad->id);
    REQUIRE(padPath.steps.size() == 2);
    CHECK(padPath.steps[0].type == ChainStepType::PadRack);
    CHECK(padPath.steps[1].type == ChainStepType::PadChain);

    CHECK(padPath.isPadOwned());
    CHECK(padPath.getPadOwnerDeviceId() == gridId);
    CHECK(padPath.getPadChainId() == pad->id);

    // The owner id is a DeviceId, so the rack accessor must not hand it back.
    CHECK(padPath.getRackIdAt(0) == INVALID_RACK_ID);
    // The pad chain id is an ordinary ChainId and the chain accessor does.
    CHECK(padPath.getChainIdAt(1) == pad->id);

    // An ordinary rack path is not pad-owned whatever its numbers.
    const auto rackPath = ChainNodePath::chain(trackId, 1, 1);
    CHECK_FALSE(rackPath.isPadOwned());
    CHECK(rackPath.getPadOwnerDeviceId() == INVALID_DEVICE_ID);
    CHECK(rackPath.getPadChainId() == INVALID_CHAIN_ID);
}

TEST_CASE("A pad and a rack sharing both numbers resolve to their own devices",
          "[drumgrid][pads][path]") {
    // The collision the untyped spelling invited, made total: the rack's id
    // equals the grid's DeviceId AND the rack chain's id equals the pad chain's,
    // so the two addresses were the same sequence of numbers. Resolution used to
    // depend on which route was tried first; now the step types decide.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    REQUIRE(rackId == gridId);  // both counters start at 1

    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto padVoiceId = tm.setPadDevice(gridPath, 0, padVoice("Kick"));
    REQUIRE(padVoiceId != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);

    // Force the chain ids together too, so nothing but the types tells them
    // apart. The pad chain id is rack-local, so this is a legal value for it.
    tm.getPadChain(gridPath, pad->id)->id = rackChainId;
    const auto padChainId = rackChainId;

    const auto rackDeviceId = tm.addDeviceToChainByPath(
        ChainNodePath::chain(trackId, rackId, rackChainId), padVoice("Rack Device"));
    REQUIRE(rackDeviceId != INVALID_DEVICE_ID);

    const auto padDevicePath =
        TrackManager::padChainPath(gridPath, padChainId).withDevice(padVoiceId);
    const auto rackDevicePath =
        ChainNodePath::chainDevice(trackId, rackId, rackChainId, rackDeviceId);

    // Identical id sequences, different step types.
    REQUIRE(padDevicePath.steps[0].id == rackDevicePath.steps[0].id);
    REQUIRE(padDevicePath.steps[1].id == rackDevicePath.steps[1].id);

    const auto* onPad = tm.getDeviceInChainByPath(padDevicePath);
    REQUIRE(onPad != nullptr);
    CHECK(onPad->id == padVoiceId);
    CHECK(onPad->name == "Kick");

    const auto* onRack = tm.getDeviceInChainByPath(rackDevicePath);
    REQUIRE(onRack != nullptr);
    CHECK(onRack->id == rackDeviceId);
    CHECK(onRack->name == "Rack Device");
}

TEST_CASE("A rack nested inside a pad chain resolves through the typed prefix",
          "[drumgrid][pads][path]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    REQUIRE(tm.setPadDevice(gridPath, 1, padVoice("Tom")) != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, 1);
    REQUIRE(pad != nullptr);
    const auto padChainId = pad->id;

    // Built on the model directly: no pad command makes one today.
    constexpr DeviceId kNested = 4242;
    RackInfo inner;
    inner.id = 77;
    inner.name = "Pad Rack";
    ChainInfo innerChain;
    innerChain.id = 3;
    auto nested = padVoice("Nested");
    nested.id = kNested;
    innerChain.elements.push_back(makeDeviceElement(nested));
    inner.chains.push_back(std::move(innerChain));
    tm.getPadChain(gridPath, padChainId)->elements.push_back(makeRackElement(std::move(inner)));

    const auto path = tm.findDevicePath(kNested);
    REQUIRE(path.isValid());

    // The pad pair leads and stays typed; the tail is an ordinary route.
    REQUIRE(path.steps.size() == 5);
    CHECK(path.steps[0].type == ChainStepType::PadRack);
    CHECK(path.steps[1].type == ChainStepType::PadChain);
    CHECK(path.steps[2].type == ChainStepType::Rack);
    CHECK(path.steps[3].type == ChainStepType::Chain);
    CHECK(path.steps[4].type == ChainStepType::Device);
    CHECK(path.getRackIdAt(2) == 77);

    const auto* resolved = tm.getDeviceInChainByPath(path);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->id == kNested);
}

TEST_CASE("A chain inside a rack nested under a pad is addressable", "[drumgrid][pads][path]") {
    // `getElementContainerForChainPath()` delegates to the pad chain lookup, so
    // stopping at the pad chain would leave copy, move, remove, wrap, insert and
    // paste unable to name anything inside a rack nested under a pad, even
    // though the device lookup resolves the same route.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);
    const auto padChainId = pad->id;

    constexpr DeviceId kNested = 5150;
    RackInfo inner;
    inner.id = 91;
    inner.name = "Pad Rack";
    ChainInfo innerChain;
    innerChain.id = 6;
    innerChain.name = "Inner";
    auto nested = padVoice("Nested");
    nested.id = kNested;
    innerChain.elements.push_back(makeDeviceElement(nested));
    inner.chains.push_back(std::move(innerChain));
    tm.getPadChain(gridPath, padChainId)->elements.push_back(makeRackElement(std::move(inner)));

    const auto padChainPath = TrackManager::padChainPath(gridPath, padChainId);

    // The pad chain itself.
    const auto* root = tm.getChainByPath(padChainPath);
    REQUIRE(root != nullptr);
    CHECK(root->id == padChainId);

    // And the chain of the rack it holds, through the same generic call.
    const auto nestedChainPath = padChainPath.withRack(91).withChain(6);
    const auto* nestedChain = tm.getChainByPath(nestedChainPath);
    REQUIRE(nestedChain != nullptr);
    CHECK(nestedChain->id == 6);
    CHECK(nestedChain->name == "Inner");
    REQUIRE(nestedChain->elements.size() == 1);
    CHECK(getDevice(nestedChain->elements[0]).id == kNested);

    // A rack id that names nothing under the pad resolves to nothing.
    CHECK(tm.getChainByPath(padChainPath.withRack(92).withChain(6)) == nullptr);
    // And so does a half pair.
    CHECK(tm.getChainByPath(padChainPath.withRack(91)) == nullptr);
}

TEST_CASE("A pad address is answered only by the track it names",
          "[drumgrid][pads][path][master]") {
    // The pad route reaches the model through `getTrack()`, and
    // `getTrack(MASTER_TRACK_ID)` returns a real track rather than null, so the
    // scoping has to come from the lookup rather than from the track missing.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto voiceId = tm.setPadDevice(gridPath, 0, padVoice("Kick"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);

    const auto padDevicePath = TrackManager::padChainPath(gridPath, pad->id).withDevice(voiceId);
    REQUIRE(tm.getDeviceInChainByPath(padDevicePath) != nullptr);

    // The master track exists and owns no such grid, so the same steps under it
    // name nothing.
    REQUIRE(tm.getTrack(MASTER_TRACK_ID) != nullptr);
    auto onMaster = padDevicePath;
    onMaster.trackId = MASTER_TRACK_ID;
    CHECK(tm.getDeviceInChainByPath(onMaster) == nullptr);

    // And so does a second media track that has no grid of its own.
    const auto otherTrackId = tm.createTrack("Other");
    auto onOther = padDevicePath;
    onOther.trackId = otherTrackId;
    CHECK(tm.getDeviceInChainByPath(onOther) == nullptr);
}

TEST_CASE("Duplicating a track re-points a pad link at the copy's own pad device",
          "[drumgrid][pads][path][duplicate]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto voiceId = tm.setPadDevice(gridPath, 0, padVoice("Kick"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);
    const auto padChainId = pad->id;

    // A track macro pointing into the pad: the link a duplication has to move.
    const auto padDevicePath = TrackManager::padChainPath(gridPath, padChainId).withDevice(voiceId);
    {
        auto* track = tm.getTrack(trackId);
        REQUIRE(track != nullptr);
        MacroLink link;
        link.target = ControlTarget::pluginParam(padDevicePath, 0);
        track->macros[0].links.push_back(link);
    }

    const auto cloneTrackId = tm.duplicateTrack(trackId);
    REQUIRE(cloneTrackId != INVALID_TRACK_ID);

    const auto* clone = tm.getTrack(cloneTrackId);
    REQUIRE(clone != nullptr);
    REQUIRE(clone->macros[0].links.size() == 1);
    const auto& moved = clone->macros[0].links[0].target.devicePath;

    // Still a pad address, and now the copy's own.
    CHECK(moved.isPadOwned());
    CHECK(moved.trackId == cloneTrackId);
    CHECK(moved.getPadOwnerDeviceId() != gridId);
    CHECK(moved.getPadChainId() == padChainId);  // rack-local, unchanged by the copy
    CHECK(moved.getDeviceId() != voiceId);

    const auto* resolved = tm.getDeviceInChainByPath(moved);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->name == "Kick");

    // And the original still names its own.
    const auto* original = tm.getDeviceInChainByPath(padDevicePath);
    REQUIRE(original != nullptr);
    CHECK(original->id == voiceId);
}

TEST_CASE("A pad path survives a serialization round trip",
          "[drumgrid][pads][path][serialization]") {
    auto path = ChainNodePath::padChain(3, 9, 5).withRack(77).withChain(2).withDevice(41);

    ChainNodePath restored;
    REQUIRE(fromVar(toVar(path), restored));
    CHECK(restored == path);
    CHECK(restored.isPadOwned());
    CHECK(restored.getPadOwnerDeviceId() == 9);
    CHECK(restored.getPadChainId() == 5);
    CHECK(restored.getDeviceId() == 41);
}

TEST_CASE("Misplaced pad steps are rejected rather than loaded",
          "[drumgrid][pads][path][serialization]") {
    const auto encode = [](const std::vector<ChainPathStep>& steps) {
        ChainNodePath path;
        path.trackId = 1;
        path.steps = steps;
        return toVar(path);
    };

    ChainNodePath out;

    // PadRack is only ever the leading step.
    CHECK_FALSE(fromVar(
        encode({{ChainStepType::Rack, 1}, {ChainStepType::Chain, 1}, {ChainStepType::PadRack, 2}}),
        out));

    // PadChain only ever follows a PadRack.
    CHECK_FALSE(fromVar(encode({{ChainStepType::Rack, 1}, {ChainStepType::PadChain, 1}}), out));

    // And a PadRack is always followed by one.
    CHECK_FALSE(fromVar(encode({{ChainStepType::PadRack, 1}, {ChainStepType::Chain, 1}}), out));

    // The well-formed pair loads.
    CHECK(fromVar(encode({{ChainStepType::PadRack, 1}, {ChainStepType::PadChain, 1}}), out));
    CHECK(out.isPadOwned());
}

TEST_CASE("A project saved before the pad step types still resolves and is retyped on load",
          "[drumgrid][pads][path][migration]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto voiceId = tm.setPadDevice(gridPath, 0, padVoice("Kick"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);

    const auto typed = TrackManager::padChainPath(gridPath, pad->id).withDevice(voiceId);
    const auto legacy = asLegacySpelling(typed);

    // Untyped, it still resolves: the pad route is tried after the ordinary one.
    const auto* resolved = tm.getDeviceInChainByPath(legacy);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->id == voiceId);

    // And a load retypes it.
    auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    MacroLink link;
    link.target = ControlTarget::pluginParam(legacy, 0);
    track->macros[0].links.push_back(link);

    std::vector<TrackInfo> tracks{*track};
    std::vector<AutomationLaneInfo> lanes;
    pad_paths::migrateLegacyPadPaths(tracks, nullptr, lanes);

    const auto& migrated = tracks[0].macros[0].links[0].target.devicePath;
    CHECK(migrated == typed);
    CHECK(migrated.isPadOwned());
}

TEST_CASE("A load retypes an address a rack answers only the prefix of",
          "[drumgrid][pads][path][migration]") {
    // The tie-break is the whole route, not the leading pair. A rack and a grid
    // can share both leading numbers while the leaf device exists only on the
    // pad, and the old resolver walked the rack tree, failed to find the leaf in
    // the chain it landed in, and fell through to the pad route. Left untyped,
    // duplication would move the owner id through the racks map and break the
    // link.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    REQUIRE(rackId == gridId);

    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto voiceId = tm.setPadDevice(gridPath, 0, padVoice("Kick"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);
    tm.getPadChain(gridPath, pad->id)->id = rackChainId;

    // The rack chain exists and shares both numbers, but holds nothing, so the
    // leaf is answered only by the pad.
    const auto* rackChain = tm.getChain(trackId, rackId, rackChainId);
    REQUIRE(rackChain != nullptr);
    REQUIRE(rackChain->elements.empty());

    const auto typed = TrackManager::padChainPath(gridPath, rackChainId).withDevice(voiceId);
    const auto legacy = asLegacySpelling(typed);

    // Untyped, it already resolves through the pad, which is what the migration
    // has to agree with.
    const auto* resolved = tm.getDeviceInChainByPath(legacy);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->id == voiceId);

    auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    MacroLink link;
    link.target = ControlTarget::pluginParam(legacy, 0);
    track->macros[0].links.push_back(link);

    std::vector<TrackInfo> tracks{*track};
    std::vector<AutomationLaneInfo> lanes;
    pad_paths::migrateLegacyPadPaths(tracks, nullptr, lanes);

    const auto& migrated = tracks[0].macros[0].links[0].target.devicePath;
    CHECK(migrated == typed);
    CHECK(migrated.isPadOwned());
}

TEST_CASE("A load leaves an address an allocated rack can account for alone",
          "[drumgrid][pads][path][migration]") {
    // The tie-break the untyped spelling had: the ordinary rack route was tried
    // first, so a path a real rack can answer stays a rack path even when a pad
    // could answer it too.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    REQUIRE(rackId == gridId);

    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    auto* pad = tm.getPad(gridPath, 0);
    REQUIRE(pad != nullptr);
    tm.getPadChain(gridPath, pad->id)->id = rackChainId;

    const auto rackDeviceId = tm.addDeviceToChainByPath(
        ChainNodePath::chain(trackId, rackId, rackChainId), padVoice("Rack Device"));
    REQUIRE(rackDeviceId != INVALID_DEVICE_ID);

    const auto rackDevicePath =
        ChainNodePath::chainDevice(trackId, rackId, rackChainId, rackDeviceId);

    auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    MacroLink link;
    link.target = ControlTarget::pluginParam(rackDevicePath, 0);
    track->macros[0].links.push_back(link);

    std::vector<TrackInfo> tracks{*track};
    std::vector<AutomationLaneInfo> lanes;
    pad_paths::migrateLegacyPadPaths(tracks, nullptr, lanes);

    const auto& untouched = tracks[0].macros[0].links[0].target.devicePath;
    CHECK(untouched == rackDevicePath);
    CHECK_FALSE(untouched.isPadOwned());
}
