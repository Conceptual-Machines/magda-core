#include "OfflineRenderModel.hpp"

#include <algorithm>
#include <set>

#include "plan/TrackRouting.hpp"

namespace magda::daw::engine_host {

namespace {

bool listed(const std::vector<TrackId>& ids, TrackId id) {
    return std::ranges::find(ids, id) != ids.end();
}

bool rendersTrack(const OfflineRenderRequest& request, TrackId id) {
    if (!request.trackIds.empty())
        return listed(request.trackIds, id);

    return !listed(request.excludedTrackIds, id);
}

/** @brief The aux track @p send reaches, the way the plan compiler resolves it. */
TrackId sendDestination(const SendInfo& send, const std::vector<TrackInfo>& tracks) {
    if (send.destTrackId != INVALID_TRACK_ID)
        return send.destTrackId;

    const auto aux = std::ranges::find(tracks, send.busIndex, &TrackInfo::auxBusIndex);
    return aux != tracks.end() ? aux->id : INVALID_TRACK_ID;
}

/** @brief Point whatever @p track routes to a dropped track at the master instead. */
void detachFromDropped(TrackInfo& track, const std::set<TrackId>& kept,
                       const std::vector<TrackInfo>& original) {
    if (const auto route = engine::parseTrackRoute(track.audioOutputDevice);
        route.namesTrack() && !kept.contains(route.trackId))
        track.audioOutputDevice = "master";

    if (track.parentId != INVALID_TRACK_ID && !kept.contains(track.parentId))
        track.parentId = INVALID_TRACK_ID;

    std::erase_if(track.childIds, [&kept](TrackId child) { return !kept.contains(child); });
    std::erase_if(track.sends, [&](const SendInfo& send) {
        return !kept.contains(sendDestination(send, original));
    });
}

bool holdsInstrument(const ChainElement& element) {
    if (isDevice(element))
        return getDevice(element).isInstrument;

    return std::ranges::any_of(getRack(element).chains, [](const auto& chain) {
        return std::ranges::any_of(chain.elements, holdsInstrument);
    });
}

/** @brief Cut @p chain after its last instrument, and everything after the fader. */
void keepInstrumentOnly(TrackChain& chain) {
    auto& elements = chain.fxChainElements;
    const auto last = std::find_if(elements.rbegin(), elements.rend(), holdsInstrument);
    elements.erase(last.base(), elements.end());
    chain.postFxChainElements.clear();
}

void removePlugins(TrackChain& chain) {
    chain.fxChainElements.clear();
    chain.postFxChainElements.clear();
}

/// Tracktion's fader is a plugin, so a chain rendered without plugins renders at unity.
void faderToUnity(TrackInfo& track) {
    track.volume = 1.0f;
    track.pan = 0.0f;
}

/** @brief Cut @p track at its fader, the way a freeze renders it. */
void keepPreFader(TrackInfo& track) {
    faderToUnity(track);
    track.muted = false;

    if (track.chain.postFxPostFader)
        track.chain.postFxChainElements.clear();
}

bool automatesFader(const AutomationLaneInfo& lane, TrackId trackId) {
    return lane.target.devicePath.trackId == trackId &&
           (lane.target.kind == ControlTarget::Kind::TrackVolume ||
            lane.target.kind == ControlTarget::Kind::TrackPan);
}

}  // namespace

OfflineRenderModel narrowForRender(OfflineRenderModel model, const OfflineRenderRequest& request) {
    const auto original = model.tracks;

    std::erase_if(model.tracks,
                  [&request](const TrackInfo& track) { return !rendersTrack(request, track.id); });

    std::set<TrackId> kept{model.master.id};
    for (const auto& track : model.tracks)
        kept.insert(track.id);

    std::set<TrackId> unityFaders;

    for (auto& track : model.tracks) {
        detachFromDropped(track, kept, original);

        // Live input is monitoring, and a render has nobody to monitor.
        track.inputMonitor = InputMonitorMode::Off;
        track.recordArmed = false;

        if (!request.usePlugins)
            removePlugins(track.chain);
        else if (!request.useTrackEffects)
            keepInstrumentOnly(track.chain);

        if (!request.usePlugins || !request.useTrackEffects) {
            faderToUnity(track);
            unityFaders.insert(track.id);
        }

        if (request.freezeTrackId != INVALID_TRACK_ID) {
            track.soloed = false;

            if (track.id == request.freezeTrackId) {
                keepPreFader(track);
                unityFaders.insert(track.id);
            }
        }
    }

    if (!request.useMasterPlugins) {
        removePlugins(model.master.chain);
        faderToUnity(model.master);
        unityFaders.insert(model.master.id);
    }

    std::erase_if(model.lanes,
                  [&kept](const engine::ClipLane& lane) { return !kept.contains(lane.trackId); });

    for (auto& lane : model.lanes) {
        lane.playbackMode = TrackPlaybackMode::Arrangement;
        lane.session.clear();
        lane.recordSlots.clear();

        if (!request.clipIds.empty())
            std::erase_if(lane.clips, [&request](const ClipInfo& clip) {
                return std::ranges::find(request.clipIds, clip.id) == request.clipIds.end();
            });
    }

    std::erase_if(model.automation, [&](const AutomationLaneInfo& lane) {
        if (lane.target.isEditScoped())
            return false;

        const auto trackId = lane.target.devicePath.trackId;
        return !kept.contains(trackId) ||
               (unityFaders.contains(trackId) && automatesFader(lane, trackId));
    });

    return model;
}

}  // namespace magda::daw::engine_host
