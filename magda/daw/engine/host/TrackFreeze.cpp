#include "TrackFreeze.hpp"

#include <algorithm>
#include <set>

#include "../../core/SourcePool.hpp"
#include "../../core/TrackManager.hpp"
#include "../../project/ProjectManager.hpp"
#include "plan/TrackRouting.hpp"

namespace magda::daw::engine_host {

namespace {

/// Below every id ClipManager hands out, and clear of INVALID_CLIP_ID.
ClipId freezeClipIdFor(TrackId trackId) {
    return -2 - trackId;
}

TrackId outputTrackOf(const TrackInfo& track) {
    const auto route = engine::parseTrackRoute(track.audioOutputDevice);
    return route.namesTrack() ? route.trackId : INVALID_TRACK_ID;
}

const FrozenTrack* findFrozen(const std::vector<FrozenTrack>& frozen, TrackId trackId) {
    const auto found = std::ranges::find(frozen, trackId, &FrozenTrack::trackId);
    return found != frozen.end() ? &*found : nullptr;
}

/** @brief The tracks feeding any of @p frozen, which their files already hold. */
std::set<TrackId> tracksBakedInto(const std::vector<TrackInfo>& tracks,
                                  const std::vector<FrozenTrack>& frozen) {
    std::set<TrackId> baked;
    for (const auto& entry : frozen)
        for (const auto feeding : tracksFeeding(tracks, entry.trackId))
            baked.insert(feeding);

    return baked;
}

ClipInfo freezeClip(const FrozenTrack& frozen, const engine::TempoMap& tempo) {
    ClipInfo clip;
    clip.id = freezeClipIdFor(frozen.trackId);
    clip.trackId = frozen.trackId;
    clip.name = "Freeze";
    clip.setAudioContent();

    AudioEvent event;
    event.sourceId = frozen.source;
    clip.audio().addEvent(event);
    clip.setPlacementBeats(0.0, tempo.timeToBeat(frozen.durationSeconds));
    return clip;
}

}  // namespace

juce::File freezeFileFor(TrackId trackId) {
    const auto renders = ProjectManager::getInstance().getRendersDirectory();
    if (renders == juce::File())
        return {};

    return renders.getChildFile("freeze").getChildFile("track_" + juce::String(trackId) + ".wav");
}

std::vector<TrackId> tracksFeeding(const std::vector<TrackInfo>& tracks, TrackId trackId) {
    std::set<TrackId> reached{trackId};
    std::vector<TrackId> feeding;

    // Repeated until nothing new reaches the set, so a chain of routes is found in any order.
    for (auto grew = true; grew;) {
        grew = false;
        for (const auto& track : tracks) {
            if (reached.contains(track.id) || !reached.contains(outputTrackOf(track)))
                continue;

            reached.insert(track.id);
            feeding.push_back(track.id);
            grew = true;
        }
    }

    return feeding;
}

juce::String freezeRefusal(const std::vector<TrackInfo>& tracks, TrackId trackId) {
    const auto track = std::ranges::find(tracks, trackId, &TrackInfo::id);
    if (track == tracks.end() || track->type != TrackType::Media)
        return "Only a media track can be frozen";

    if (const auto output = outputTrackOf(*track);
        output != INVALID_TRACK_ID && output != MASTER_TRACK_ID)
        return "Tracks which output to another track can't themselves be frozen; "
               "freeze the track they input into instead";

    return {};
}

std::optional<OfflineRenderRequest> freezeRequest(const std::vector<TrackInfo>& tracks,
                                                  const std::vector<engine::ClipLane>& lanes,
                                                  TrackId trackId, const juce::File& destination,
                                                  const engine::RenderContext& context) {
    auto rendered = tracksFeeding(tracks, trackId);
    rendered.insert(rendered.begin(), trackId);

    auto endBeat = 0.0;
    for (const auto& lane : lanes)
        if (std::ranges::find(rendered, lane.trackId) != rendered.end())
            for (const auto& clip : lane.clips)
                endBeat = std::max(endBeat, clip.placement.endBeat());

    if (endBeat <= 0.0)
        return std::nullopt;

    OfflineRenderRequest request;
    request.destination = destination;
    request.bitDepth = 32;
    request.dither = OfflineRenderDither::None;
    request.sampleRate = context.sampleRate;
    request.blockSize = context.maxBlockSize;
    request.useMasterPlugins = false;
    request.range = {{0.0}, {endBeat}};
    request.tailSeconds = std::nullopt;
    request.trackIds = std::move(rendered);
    request.freezeTrackId = trackId;
    return request;
}

std::vector<TrackInfo> tracksAsPlayed(std::vector<TrackInfo> tracks,
                                      const std::vector<FrozenTrack>& frozen) {
    const auto baked = tracksBakedInto(tracks, frozen);
    std::erase_if(tracks, [&baked](const TrackInfo& track) { return baked.contains(track.id); });

    for (auto& track : tracks) {
        if (findFrozen(frozen, track.id) == nullptr)
            continue;

        // What the file already holds. The fader and whatever follows it stay live.
        track.chain.fxChainElements.clear();
        if (!track.chain.postFxPostFader)
            track.chain.postFxChainElements.clear();

        track.audioInputDevice = {};
        track.midiInputDevice = {};
    }

    return tracks;
}

std::vector<engine::ClipLane> lanesAsPlayed(std::vector<engine::ClipLane> lanes,
                                            const std::vector<TrackInfo>& tracks,
                                            const std::vector<FrozenTrack>& frozen,
                                            const engine::TempoMap& tempo) {
    const auto baked = tracksBakedInto(tracks, frozen);
    std::erase_if(lanes,
                  [&baked](const engine::ClipLane& lane) { return baked.contains(lane.trackId); });

    for (auto& lane : lanes) {
        const auto* entry = findFrozen(frozen, lane.trackId);
        if (entry == nullptr)
            continue;

        lane.clips = {freezeClip(*entry, tempo)};
        lane.session.clear();
        lane.recordSlots.clear();
        lane.playbackMode = TrackPlaybackMode::Arrangement;
    }

    return lanes;
}

std::vector<FrozenTrack> frozenTracksWithFiles(const std::vector<TrackInfo>& tracks) {
    auto& pool = SourcePool::getInstance();
    std::vector<FrozenTrack> frozen;

    for (const auto& track : tracks) {
        if (!track.frozen)
            continue;

        const auto file = freezeFileFor(track.id);
        if (!file.existsAsFile())
            continue;

        const auto id = pool.acquire(file.getFullPathName());
        if (const auto* source = pool.get(id); source != nullptr && source->isResolved())
            frozen.push_back(
                {.trackId = track.id, .source = id, .durationSeconds = source->durationSeconds});
    }

    return frozen;
}

void refreshPooledFile(const juce::File& file) {
    auto& pool = SourcePool::getInstance();
    if (const auto id = pool.findByPath(file.getFullPathName()); id != INVALID_SOURCE_ID)
        pool.resolveFacts(id);
}

void unfreezeTracksWithoutFiles() {
    auto& trackManager = TrackManager::getInstance();

    std::vector<TrackId> stale;
    for (const auto& track : trackManager.getTracks())
        if (track.frozen && !freezeFileFor(track.id).existsAsFile())
            stale.push_back(track.id);

    for (const auto trackId : stale)
        trackManager.setTrackFrozen(trackId, false);
}

}  // namespace magda::daw::engine_host
