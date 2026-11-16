// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "StemRender.hpp"

#include <algorithm>
#include <set>

#include "../../core/TrackAvailability.hpp"
#include "../../core/TrackTypes.hpp"
#include "plan/TrackRouting.hpp"

namespace magda::daw::engine_host {

namespace {

/// Deeper than any real chain of routes, and short of a cycle.
constexpr int kMaxTrackWalk = 64;

const TrackInfo* findTrack(const std::vector<TrackInfo>& tracks, TrackId id) {
    const auto found = std::ranges::find(tracks, id, &TrackInfo::id);
    return found != tracks.end() ? &*found : nullptr;
}

TrackId outputTrackOf(const TrackInfo& track) {
    const auto route = engine::parseTrackRoute(track.audioOutputDevice);
    return route.namesTrack() ? route.trackId : INVALID_TRACK_ID;
}

/** @brief The track whose instrument plays @p track, for a pad or multi-out channel. */
TrackId instrumentTrackOf(const TrackInfo& track) {
    if (track.type == TrackType::Pad)
        return track.parentId;

    if (track.type == TrackType::MultiOut && track.multiOutLink)
        return track.multiOutLink->sourceTrackId;

    return INVALID_TRACK_ID;
}

/** @brief The aux track @p send reaches, the way the plan compiler resolves it. */
TrackId sendDestination(const SendInfo& send, const std::vector<TrackInfo>& tracks) {
    if (send.destTrackId != INVALID_TRACK_ID)
        return send.destTrackId;

    const auto aux = std::ranges::find(tracks, send.busIndex, &TrackInfo::auxBusIndex);
    return aux != tracks.end() ? aux->id : INVALID_TRACK_ID;
}

bool sendsInto(const std::vector<TrackInfo>& tracks, const TrackInfo& track,
               const std::set<TrackId>& reached) {
    return std::ranges::any_of(track.sends, [&](const SendInfo& send) {
        return send.enabled && reached.contains(sendDestination(send, tracks));
    });
}

/** @brief The engine's rule (PlanValues): muted itself, or through its parent, else its output. */
bool mutedThrough(const std::vector<TrackInfo>& tracks, const TrackInfo& track, int depth) {
    if (track.muted)
        return true;
    if (depth >= kMaxTrackWalk)
        return false;

    if (const auto* parent = findTrack(tracks, track.parentId))
        return mutedThrough(tracks, *parent, depth + 1);
    if (const auto* destination = findTrack(tracks, outputTrackOf(track)))
        return mutedThrough(tracks, *destination, depth + 1);

    return false;
}

bool mutedInMix(const std::vector<TrackInfo>& tracks, const TrackInfo& track) {
    if (track.muted)
        return true;

    const auto* parent = findTrack(tracks, track.parentId);
    const auto* destination = findTrack(tracks, outputTrackOf(track));
    return (parent != nullptr && mutedThrough(tracks, *parent, 1)) ||
           (destination != nullptr && mutedThrough(tracks, *destination, 1));
}

bool getsStem(const std::vector<TrackInfo>& tracks, const TrackInfo& track) {
    const auto traits = traitsOf(track.type);
    return track.type != TrackType::Master && traits.isRendered && traits.carriesSignal &&
           track.enabled && unregisteredDevicesOn(track).empty() && !mutedInMix(tracks, track);
}

}  // namespace

std::vector<TrackId> stemTracks(const std::vector<TrackInfo>& tracks) {
    std::vector<TrackId> ordered;
    std::set<TrackId> visited;

    auto visit = [&](auto& self, const TrackInfo& track) -> void {
        if (!visited.insert(track.id).second)
            return;

        ordered.push_back(track.id);
        for (const auto childId : track.childIds)
            if (const auto* child = findTrack(tracks, childId))
                self(self, *child);
    };

    for (const auto& track : tracks)
        if (findTrack(tracks, track.parentId) == nullptr)
            visit(visit, track);

    // A child its parent does not list still gets a stem.
    for (const auto& track : tracks)
        visit(visit, track);

    std::erase_if(ordered,
                  [&tracks](TrackId id) { return !getsStem(tracks, *findTrack(tracks, id)); });
    std::ranges::stable_partition(ordered, [&tracks](TrackId id) {
        return !traitsOf(findTrack(tracks, id)->type).sitsBesideMaster;
    });
    return ordered;
}

std::vector<TrackId> stemSources(const std::vector<TrackInfo>& tracks, TrackId trackId) {
    std::set<TrackId> reached{trackId};
    std::vector<TrackId> sources{trackId};

    auto reach = [&](TrackId id) {
        if (findTrack(tracks, id) == nullptr || !reached.insert(id).second)
            return false;

        sources.push_back(id);
        return true;
    };

    // Repeated until nothing new reaches the set, so a chain of routes is found in any order.
    for (auto grew = true; grew;) {
        grew = false;
        for (const auto& track : tracks) {
            if (reached.contains(track.id))
                grew = reach(instrumentTrackOf(track)) || grew;
            else if (reached.contains(outputTrackOf(track)) || sendsInto(tracks, track, reached))
                grew = reach(track.id) || grew;
        }
    }

    return sources;
}

}  // namespace magda::daw::engine_host
