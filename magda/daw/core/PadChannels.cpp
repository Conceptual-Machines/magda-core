#include "PadChannels.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <set>

#include "ChainWalk.hpp"
#include "DrumGridPads.hpp"

namespace magda::pad_channels {

namespace {

using PadKey = std::pair<DeviceId, ChainId>;

/// A pad that wants a channel, in grid order.
struct WantedPad {
    TrackId gridTrackId = INVALID_TRACK_ID;
    PadKey key;
    int slot = 0;
    int bus = 0;
    juce::String name;
    // The pad's own mix, moved onto a channel made for it.
    float volumeDb = 0.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
};

juce::String channelName(const ChainInfo& pad) {
    if (pad.name.isNotEmpty())
        return pad.name;
    if (auto voice = padVoiceName(pad); voice.isNotEmpty())
        return voice;
    return juce::MidiMessage::getMidiNoteName(pad.lowNote, true, true, 3);
}

const TrackInfo* trackById(const std::vector<TrackInfo>& tracks, TrackId id) {
    const auto found = std::ranges::find(tracks, id, &TrackInfo::id);
    return found == tracks.end() ? nullptr : &*found;
}

bool isChannel(const TrackInfo& track) {
    return track.type == TrackType::Pad;
}

PadKey keyOf(const TrackInfo& channel) {
    return channel.padLink ? PadKey{channel.padLink->gridId, channel.padLink->padId}
                           : PadKey{INVALID_DEVICE_ID, INVALID_CHAIN_ID};
}

/// Every grid's pads that hold a device, and every grid's id in @p grids. Pads
/// whose channel does not exist yet hand their mix over, and the buses and
/// multi-out pairs of #2211 are cleared.
std::vector<WantedPad> collectPads(std::vector<TrackInfo>& tracks, const std::set<PadKey>& linked,
                                   std::set<DeviceId>& grids, bool& changed) {
    std::vector<WantedPad> wanted;
    for (auto& track : tracks) {
        if (isChannel(track))
            continue;
        chain_walk::forEachDevice(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                                  chain_walk::Pads::Enter,
                                  [&](DeviceInfo& device, const ChainNodePath&) {
                                      if (!device.pads || !isPadRackDevice(device.pluginId))
                                          return;
                                      grids.insert(device.id);
                                      if (device.multiOut.isMultiOut) {
                                          device.multiOut = {};
                                          changed = true;
                                      }

                                      std::vector<WantedPad> grid;
                                      for (auto& pad : device.pads->chains) {
                                          const auto bus = std::exchange(pad.outputIndex, 0);
                                          changed = changed || bus != 0;
                                          if (!padHasDevices(pad))
                                              continue;

                                          WantedPad want{track.id,
                                                         {device.id, pad.id},
                                                         padParameterSlot(pad),
                                                         bus,
                                                         channelName(pad)};
                                          if (!linked.contains(want.key)) {
                                              want.volumeDb = std::exchange(pad.volume, 0.0f);
                                              want.pan = std::exchange(pad.pan, 0.0f);
                                              want.muted = std::exchange(pad.muted, false);
                                              want.solo = std::exchange(pad.solo, false);
                                          }
                                          grid.push_back(std::move(want));
                                      }
                                      std::ranges::stable_sort(grid, {}, &WantedPad::slot);
                                      wanted.insert(wanted.end(), grid.begin(), grid.end());
                                  });
    }
    return wanted;
}

/// A pad bus track becomes the channel of the one pad it carried, or a group
/// the channels of several route into. One that carried nothing goes.
void convertBusTracks(std::vector<TrackInfo>& tracks, const std::set<DeviceId>& grids,
                      std::vector<WantedPad>& wanted, std::set<PadKey>& linked,
                      std::map<std::pair<DeviceId, int>, TrackId>& groups, bool& changed) {
    std::set<TrackId> emptied;
    for (auto& track : tracks) {
        if (track.type != TrackType::MultiOut || !track.multiOutLink ||
            !grids.contains(track.multiOutLink->sourceDeviceId))
            continue;
        const auto link = *track.multiOutLink;
        track.multiOutLink.reset();
        changed = true;

        std::vector<WantedPad*> fed;
        for (auto& want : wanted)
            if (want.key.first == link.sourceDeviceId && want.bus == link.outputPairIndex)
                fed.push_back(&want);

        if (fed.empty()) {
            emptied.insert(track.id);
        } else if (fed.size() == 1 && !linked.contains(fed.front()->key)) {
            const auto& want = *fed.front();
            track.type = TrackType::Pad;
            track.padLink = PadChannelLink{want.key.first, want.key.second};
            track.volume *= juce::Decibels::decibelsToGain(want.volumeDb, -60.0f);
            track.manualVolume = track.volume;
            if (track.pan == 0.0f)
                track.pan = track.manualPan = want.pan;
            track.muted = track.muted || want.muted;
            track.soloed = track.soloed || want.solo;
            linked.insert(want.key);
        } else {
            track.type = TrackType::Group;
            groups[{link.sourceDeviceId, link.outputPairIndex}] = track.id;
        }
        track.normalizeForType();
    }
    std::erase_if(tracks, [&](const TrackInfo& track) { return emptied.contains(track.id); });
}

/// Each grid track's channels stand right after it, in pad order, and are its
/// children. Nothing else keeps a channel as a child.
bool placeChannels(std::vector<TrackInfo>& tracks, const std::vector<WantedPad>& wanted) {
    std::map<PadKey, std::size_t> rank;
    for (std::size_t i = 0; i < wanted.size(); ++i)
        rank[wanted[i].key] = i;

    std::map<TrackId, std::vector<std::size_t>> channelsOf;
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (isChannel(tracks[i]))
            channelsOf[tracks[i].parentId].push_back(i);
    for (auto& [parent, channels] : channelsOf)
        std::ranges::sort(channels, {}, [&](std::size_t i) { return rank[keyOf(tracks[i])]; });

    bool changed = false;
    std::vector<std::size_t> order;
    order.reserve(tracks.size());
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto& track = tracks[i];
        if (isChannel(track))
            continue;
        order.push_back(i);

        std::vector<TrackId> children;
        for (const auto id : track.childIds)
            if (const auto* child = trackById(tracks, id); child != nullptr && !isChannel(*child))
                children.push_back(id);
        if (const auto channels = channelsOf.find(track.id); channels != channelsOf.end()) {
            for (const auto channel : channels->second) {
                children.push_back(tracks[channel].id);
                order.push_back(channel);
            }
        }
        if (track.childIds != children) {
            track.childIds = std::move(children);
            changed = true;
        }
    }

    // A channel whose parent is not in the list has nowhere to stand.
    if (order.size() != tracks.size())
        changed = true;
    else if (std::ranges::is_sorted(order))
        return changed;

    std::vector<TrackInfo> ordered;
    ordered.reserve(order.size());
    for (const auto i : order)
        ordered.push_back(std::move(tracks[i]));
    tracks = std::move(ordered);
    return true;
}

}  // namespace

juce::String gridRoute(TrackId gridTrackId) {
    return "track:" + juce::String(gridTrackId);
}

TrackInfo* findChannel(std::vector<TrackInfo>& tracks, DeviceId gridId, ChainId padId) {
    const auto found = std::ranges::find_if(tracks, [&](const TrackInfo& track) {
        return isChannel(track) && keyOf(track) == PadKey{gridId, padId};
    });
    return found == tracks.end() ? nullptr : &*found;
}

const TrackInfo* findChannel(const std::vector<TrackInfo>& tracks, DeviceId gridId, ChainId padId) {
    return findChannel(const_cast<std::vector<TrackInfo>&>(tracks), gridId, padId);
}

bool sync(std::vector<TrackInfo>& tracks, TrackId& nextTrackId, Stash* stash) {
    bool changed = false;

    // Two channels for one pad, a restore meeting a sync that got there first:
    // the older one stays.
    std::map<PadKey, TrackId> oldest;
    for (const auto& track : tracks)
        if (isChannel(track))
            if (const auto [it, added] = oldest.emplace(keyOf(track), track.id); !added)
                it->second = std::min(it->second, track.id);
    changed = std::erase_if(tracks, [&](const TrackInfo& track) {
                  return isChannel(track) && oldest[keyOf(track)] != track.id;
              }) > 0;

    std::set<PadKey> linked;
    for (const auto& [key, id] : oldest)
        linked.insert(key);

    std::set<DeviceId> grids;
    auto wanted = collectPads(tracks, linked, grids, changed);
    std::map<std::pair<DeviceId, int>, TrackId> groups;
    convertBusTracks(tracks, grids, wanted, linked, groups, changed);

    // A channel for every pad that holds a device, under the grid's track.
    std::set<PadKey> wantedKeys;
    for (const auto& want : wanted) {
        wantedKeys.insert(want.key);

        if (auto* channel = findChannel(tracks, want.key.first, want.key.second)) {
            if (channel->parentId == want.gridTrackId)
                continue;
            // The grid moved to another track, and its channels follow it.
            if (channel->audioOutputDevice == gridRoute(channel->parentId))
                channel->audioOutputDevice = gridRoute(want.gridTrackId);
            channel->parentId = want.gridTrackId;
            changed = true;
            continue;
        }

        TrackInfo channel;
        const auto stashed = stash != nullptr ? stash->find(want.key) : Stash::iterator{};
        if (stash != nullptr && stashed != stash->end()) {
            channel = std::move(stashed->second);
            stash->erase(stashed);
            if (channel.audioOutputDevice == gridRoute(channel.parentId))
                channel.audioOutputDevice = gridRoute(want.gridTrackId);
        } else {
            const auto* grid = trackById(tracks, want.gridTrackId);
            const auto group = groups.find({want.key.first, want.bus});
            channel.id = nextTrackId++;
            channel.type = TrackType::Pad;
            channel.name = want.name;
            channel.colour = grid != nullptr ? grid->colour : juce::Colour();
            channel.padLink = PadChannelLink{want.key.first, want.key.second};
            channel.volume = channel.manualVolume =
                juce::Decibels::decibelsToGain(want.volumeDb, -60.0f);
            channel.pan = channel.manualPan = want.pan;
            channel.muted = want.muted;
            channel.soloed = want.solo;
            channel.audioOutputDevice =
                group != groups.end() ? gridRoute(group->second) : gridRoute(want.gridTrackId);
        }
        channel.parentId = want.gridTrackId;
        channel.childIds.clear();
        tracks.push_back(std::move(channel));
        changed = true;
    }

    // No channel for a pad that holds nothing, or whose grid is gone.
    std::erase_if(tracks, [&](const TrackInfo& track) {
        if (!isChannel(track) || wantedKeys.contains(keyOf(track)))
            return false;
        if (stash != nullptr && track.padLink)
            (*stash)[keyOf(track)] = track;
        changed = true;
        return true;
    });

    return placeChannels(tracks, wanted) || changed;
}

}  // namespace magda::pad_channels
