// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/PadChannels.hpp"
#include "magda/daw/core/PadCommands.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

// Each Drum Grid pad that holds something is a mixer channel under the grid's
// track (#3010).

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

struct Grid {
    TrackId trackId = INVALID_TRACK_ID;
    DeviceId gridId = INVALID_DEVICE_ID;
    ChainNodePath path;
};

Grid makeGrid(const juce::String& trackName = "Drums") {
    auto& tm = TrackManager::getInstance();
    Grid grid;
    grid.trackId = tm.createTrack(trackName);
    grid.gridId = tm.addDeviceToTrack(grid.trackId, drumGridDevice());
    grid.path = ChainNodePath::topLevelDevice(grid.trackId, grid.gridId);
    return grid;
}

std::vector<TrackId> trackOrder() {
    std::vector<TrackId> ids;
    for (const auto& track : TrackManager::getInstance().getTracks())
        ids.push_back(track.id);
    return ids;
}

std::size_t channelCount() {
    return static_cast<std::size_t>(std::ranges::count(TrackManager::getInstance().getTracks(),
                                                       TrackType::Pad, &TrackInfo::type));
}

}  // namespace

TEST_CASE("A pad that holds a device is a channel under its grid's track",
          "[drumgrid][pads][channels]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto grid = makeGrid();
    const auto after = tm.createTrack("After");

    CHECK(tm.getPadChannel(grid.path, 5) == nullptr);
    REQUIRE(tm.ensurePad(grid.path, 5) != INVALID_CHAIN_ID);
    CHECK(tm.getPadChannel(grid.path, 5) == nullptr);  // an empty pad has none

    REQUIRE(tm.setPadDevice(grid.path, 5, padVoice("Clap")) != INVALID_DEVICE_ID);
    REQUIRE(tm.setPadDevice(grid.path, 2, padVoice("Snare")) != INVALID_DEVICE_ID);

    const auto* clap = tm.getPadChannel(grid.path, 5);
    const auto* snare = tm.getPadChannel(grid.path, 2);
    REQUIRE(clap != nullptr);
    REQUIRE(snare != nullptr);
    CHECK(clap->type == TrackType::Pad);
    CHECK(clap->parentId == grid.trackId);
    CHECK(clap->audioOutputDevice == pad_channels::gridRoute(grid.trackId));
    CHECK(clap->name.isNotEmpty());

    // Right under the grid's track, in pad order, as its children.
    CHECK(trackOrder() == std::vector<TrackId>{grid.trackId, snare->id, clap->id, after});
    CHECK(tm.getTrack(grid.trackId)->childIds == std::vector<TrackId>{snare->id, clap->id});
}

TEST_CASE("A pad's fader and switches are its channel's", "[drumgrid][pads][channels]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto grid = makeGrid();

    // Set before the pad has a channel, the value waits on the pad...
    REQUIRE(tm.ensurePad(grid.path, 0) != INVALID_CHAIN_ID);
    tm.setPadVolume(grid.path, 0, -6.0f);
    tm.setPadMuted(grid.path, 0, true);
    CHECK(tm.getPad(grid.path, 0)->volume == -6.0f);

    // ...and moves onto the channel it gets.
    REQUIRE(tm.setPadDevice(grid.path, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    const auto* channel = tm.getPadChannel(grid.path, 0);
    REQUIRE(channel != nullptr);
    CHECK(channel->volume == Catch::Approx(0.501187f));
    CHECK(channel->muted);
    CHECK(tm.getPad(grid.path, 0)->volume == 0.0f);
    CHECK_FALSE(tm.getPad(grid.path, 0)->muted);

    tm.setPadPan(grid.path, 0, -0.5f);
    tm.setPadSolo(grid.path, 0, true);
    CHECK(tm.getPadChannel(grid.path, 0)->pan == -0.5f);
    CHECK(tm.getPadChannel(grid.path, 0)->soloed);
    CHECK(tm.getPad(grid.path, 0)->pan == 0.0f);
}

TEST_CASE("Clearing a pad takes its channel, and undo brings it back as it was",
          "[drumgrid][pads][channels][undo]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto grid = makeGrid();
    REQUIRE(tm.setPadDevice(grid.path, 0, padVoice("Kick")) != INVALID_DEVICE_ID);

    const auto channelId = tm.getPadChannel(grid.path, 0)->id;
    tm.setTrackVolume(channelId, 0.25f);

    editPads(grid.path, "Clear Pad", [&] { tm.clearPad(grid.path, 0); });
    CHECK(tm.getTrack(channelId) == nullptr);
    CHECK(tm.getTrack(grid.trackId)->childIds.empty());

    REQUIRE(UndoManager::getInstance().undo());
    const auto* restored = tm.getPadChannel(grid.path, 0);
    REQUIRE(restored != nullptr);
    CHECK(restored->id == channelId);
    CHECK(restored->volume == 0.25f);

    REQUIRE(UndoManager::getInstance().redo());
    CHECK(tm.getTrack(channelId) == nullptr);
}

TEST_CASE("A grid's channels follow it to another track", "[drumgrid][pads][channels]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto grid = makeGrid();
    const auto other = tm.createTrack("Other");
    REQUIRE(tm.setPadDevice(grid.path, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    const auto channelId = tm.getPadChannel(grid.path, 0)->id;

    REQUIRE(tm.moveChainElement(grid.path, ChainNodePath::trackLevel(other), 0));

    const auto* channel = tm.getTrack(channelId);
    REQUIRE(channel != nullptr);
    CHECK(channel->parentId == other);
    CHECK(channel->audioOutputDevice == pad_channels::gridRoute(other));
    CHECK(tm.getTrack(grid.trackId)->childIds.empty());
    CHECK(trackOrder() == std::vector<TrackId>{grid.trackId, other, channelId});
}

TEST_CASE("A grid inside a rack has channels too", "[drumgrid][pads][channels]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Drums");
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto chainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);
    const auto gridId = tm.addDeviceToChainByPath(chainPath, drumGridDevice());
    const auto gridPath = chainPath.withDevice(gridId);

    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    const auto* channel = tm.getPadChannel(gridPath, 0);
    REQUIRE(channel != nullptr);
    CHECK(channel->parentId == trackId);

    // And wrapping a top-level grid keeps them.
    const auto grid = makeGrid("More Drums");
    REQUIRE(tm.setPadDevice(grid.path, 0, padVoice("Snare")) != INVALID_DEVICE_ID);
    const auto snareId = tm.getPadChannel(grid.path, 0)->id;
    REQUIRE(tm.wrapDeviceInRack(grid.trackId, grid.gridId) != INVALID_RACK_ID);
    CHECK(tm.getTrack(snareId) != nullptr);
}

TEST_CASE("Deleting the grid's track takes its channels, and undo restores them once",
          "[drumgrid][pads][channels][undo]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto grid = makeGrid();
    REQUIRE(tm.setPadDevice(grid.path, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    REQUIRE(tm.setPadDevice(grid.path, 1, padVoice("Snare")) != INVALID_DEVICE_ID);
    const auto before = trackOrder();

    auto& undo = UndoManager::getInstance();
    undo.executeCommand(std::make_unique<DeleteTrackCommand>(grid.trackId));
    CHECK(tm.getTracks().empty());

    REQUIRE(undo.undo());
    CHECK(trackOrder() == before);
    CHECK(channelCount() == 2);
}

TEST_CASE("Removing the grid takes its channels, and undo restores the same ones",
          "[drumgrid][pads][channels][undo]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto grid = makeGrid();
    REQUIRE(tm.setPadDevice(grid.path, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    const auto channelId = tm.getPadChannel(grid.path, 0)->id;
    tm.setTrackPan(channelId, 0.75f);

    auto& undo = UndoManager::getInstance();
    undo.executeCommand(std::make_unique<RemoveDeviceByPathCommand>(grid.path));
    CHECK(channelCount() == 0);

    REQUIRE(undo.undo());
    const auto* channel = tm.getPadChannel(grid.path, 0);
    REQUIRE(channel != nullptr);
    CHECK(channel->id == channelId);
    CHECK(channel->pan == 0.75f);
}

TEST_CASE("A pad channel survives a save and reload", "[drumgrid][pads][channels]") {
    TrackInfo channel;
    channel.id = 7;
    channel.type = TrackType::Pad;
    channel.parentId = 3;
    channel.padLink = PadChannelLink{12, 4};
    channel.audioOutputDevice = pad_channels::gridRoute(3);

    TrackInfo loaded;
    REQUIRE(ProjectSerializer::deserializeTrackInfo(ProjectSerializer::serializeTrackInfo(channel),
                                                    loaded));
    CHECK(loaded.type == TrackType::Pad);
    CHECK(loaded.padLink == channel.padLink);
    CHECK(loaded.parentId == 3);
    CHECK(loaded.audioOutputDevice == channel.audioOutputDevice);
}

// Projects saved with pads on buses (#2211).

namespace {

TrackInfo legacyGridTrack(TrackId id, DeviceId gridId,
                          const std::vector<std::pair<int, int>>& padBuses) {
    TrackInfo track;
    track.id = id;
    track.name = "Drums";
    track.audioOutputDevice = "master";

    auto grid = drumGridDevice();
    grid.id = gridId;
    grid.multiOut.isMultiOut = true;
    grid.multiOut.outputPairs.resize(4);
    auto& pads = ensurePads(grid);
    for (const auto& [padIndex, bus] : padBuses) {
        auto& pad = ensurePadChain(pads, padIndex);
        pad.outputIndex = bus;
        auto voice = padVoice("Voice");
        voice.id = 100 + padIndex;
        pad.elements.push_back(makeDeviceElement(voice));
    }
    stampPadRackId(grid);
    track.chain.fxChainElements.push_back(makeDeviceElement(grid));
    return track;
}

TrackInfo legacyBusTrack(TrackId id, TrackId gridTrackId, DeviceId gridId, int bus) {
    TrackInfo track;
    track.id = id;
    track.type = TrackType::MultiOut;
    track.name = "Drum Grid: Bus";
    track.audioOutputDevice = "track:9";
    track.multiOutLink = MultiOutTrackLink{gridTrackId, gridId, bus};
    return track;
}

const DeviceInfo& gridOf(const std::vector<TrackInfo>& tracks) {
    return getDevice(tracks.front().chain.fxChainElements.front());
}

}  // namespace

TEST_CASE("A bus carrying one pad becomes that pad's channel", "[drumgrid][pads][channels]") {
    std::vector<TrackInfo> tracks{legacyGridTrack(1, 10, {{0, 0}, {1, 1}}),
                                  legacyBusTrack(2, 1, 10, 1)};
    tracks[1].volume = 0.5f;
    TrackId nextTrackId = 3;

    REQUIRE(pad_channels::sync(tracks, nextTrackId));

    const auto& grid = gridOf(tracks);
    const auto padOne = findPadChain(*grid.pads.get(), 1)->id;
    const auto padZero = findPadChain(*grid.pads.get(), 0)->id;
    const auto* busChannel = pad_channels::findChannel(tracks, 10, padOne);
    REQUIRE(busChannel != nullptr);
    CHECK(busChannel->id == 2);
    CHECK(busChannel->audioOutputDevice == "track:9");  // where the bus went
    CHECK(busChannel->volume == 0.5f);
    CHECK_FALSE(busChannel->multiOutLink.has_value());

    const auto* mainChannel = pad_channels::findChannel(tracks, 10, padZero);
    REQUIRE(mainChannel != nullptr);
    CHECK(mainChannel->id == 3);
    CHECK(mainChannel->audioOutputDevice == pad_channels::gridRoute(1));

    CHECK_FALSE(grid.multiOut.isMultiOut);
    CHECK(std::ranges::all_of(grid.pads->chains,
                              [](const ChainInfo& pad) { return pad.outputIndex == 0; }));
    CHECK(tracks.front().childIds == std::vector<TrackId>{3, 2});

    // A second sync finds nothing to do.
    CHECK_FALSE(pad_channels::sync(tracks, nextTrackId));
}

TEST_CASE("A bus carrying several pads becomes a group their channels route into",
          "[drumgrid][pads][channels]") {
    std::vector<TrackInfo> tracks{legacyGridTrack(1, 10, {{0, 1}, {1, 1}, {2, 0}}),
                                  legacyBusTrack(2, 1, 10, 1), legacyBusTrack(3, 1, 10, 2)};
    TrackId nextTrackId = 4;

    REQUIRE(pad_channels::sync(tracks, nextTrackId));

    const auto bus = std::ranges::find(tracks, 2, &TrackInfo::id);
    REQUIRE(bus != tracks.end());
    CHECK(bus->type == TrackType::Group);
    CHECK(bus->audioOutputDevice == "track:9");
    // A bus nothing played through goes.
    CHECK(std::ranges::find(tracks, 3, &TrackInfo::id) == tracks.end());

    const auto& pads = *gridOf(tracks).pads.get();
    for (const int padIndex : {0, 1}) {
        const auto* channel =
            pad_channels::findChannel(tracks, 10, findPadChain(pads, padIndex)->id);
        REQUIRE(channel != nullptr);
        CHECK(channel->audioOutputDevice == pad_channels::gridRoute(2));
        CHECK(channel->parentId == 1);
    }
    const auto* mainChannel = pad_channels::findChannel(tracks, 10, findPadChain(pads, 2)->id);
    REQUIRE(mainChannel != nullptr);
    CHECK(mainChannel->audioOutputDevice == pad_channels::gridRoute(1));
}

TEST_CASE("A pad channel's chain is its pad's chain", "[drumgrid][pads][channels][3144]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto grid = makeGrid();
    REQUIRE(tm.setPadDevice(grid.path, 3, padVoice("Hat")) != INVALID_DEVICE_ID);

    const auto* channel = tm.getPadChannel(grid.path, 3);
    REQUIRE(channel != nullptr);
    const auto padPath = TrackManager::padChainPath(grid.path, tm.getPad(grid.path, 3)->id);
    CHECK(tm.getPadChannelChainPath(channel->id) == padPath);
    CHECK_FALSE(tm.getPadChannelChainPath(grid.trackId).isValid());

    // What the channel's chain view adds lands on the pad.
    REQUIRE(tm.addDeviceToChainByPath(padPath, padVoice("Shaker")) != INVALID_DEVICE_ID);
    CHECK(tm.getPad(grid.path, 3)->elements.size() == 2);
    CHECK(tm.getTrack(channel->id)->chain.fxChainElements.empty());
}

TEST_CASE("Selecting a device in a pad channel's chain keeps the channel selected",
          "[drumgrid][pads][channels][3144]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& selection = SelectionManager::getInstance();
    const auto grid = makeGrid();
    const auto voiceId = tm.setPadDevice(grid.path, 3, padVoice("Hat"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);
    const auto* channel = tm.getPadChannel(grid.path, 3);
    REQUIRE(channel != nullptr);
    const auto padPath = tm.getPadChannelChainPath(channel->id);

    selection.selectTrack(channel->id);
    selection.selectChainNode(padPath.withDevice(voiceId));
    CHECK(selection.getSelectedTrack() == channel->id);

    // From anywhere else the device still takes the selection to its grid's track.
    selection.selectTrack(grid.trackId);
    selection.selectChainNode(padPath.withDevice(voiceId));
    CHECK(selection.getSelectedTrack() == grid.trackId);
}
