#include "EngineProject.hpp"

#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/SourcePool.hpp"
#include "../../core/TrackManager.hpp"

namespace magda::daw::engine_host {

std::vector<engine::ClipLane> clipLanesFor(const std::vector<TrackInfo>& tracks) {
    const auto& clips = ClipManager::getInstance();

    std::vector<engine::ClipLane> lanes;
    lanes.reserve(tracks.size());

    for (const auto& track : tracks) {
        engine::ClipLane lane;
        lane.trackId = track.id;
        lane.playbackMode = track.playbackMode;
        lane.clips = clips.arrangementLane(track.id);

        for (const auto id : clips.getClipsOnTrack(track.id, ClipView::Session))
            if (const auto* clip = clips.getClip(id))
                lane.session.push_back(*clip);

        lanes.push_back(std::move(lane));
    }

    return lanes;
}

std::vector<engine::ClipSourceInfo> clipSources() {
    std::vector<engine::ClipSourceInfo> sources;

    for (const auto& source : SourcePool::getInstance().snapshot())
        sources.push_back({.id = source.id,
                           .path = source.filePath.toStdString(),
                           .sampleRate = source.sampleRate,
                           .durationSeconds = source.durationSeconds});

    return sources;
}

engine::TempoMap tempoMapAt(double bpm, int numerator, int denominator) {
    return engine::TempoMap({engine::TempoChange{.startBeat = 0.0, .bpm = bpm}},
                            {engine::TimeSignatureChange{
                                .startBeat = 0.0,
                                .numerator = numerator,
                                .denominator = denominator,
                            }});
}

bool modelHoldsNoDevices() {
    const auto& tracks = TrackManager::getInstance().getTracks();

    // Tested first, so the walk only happens in the one state that can answer
    // yes: every other publish is a project with tracks in it.
    if (!tracks.empty())
        return false;

    const auto* master = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
    return master == nullptr || audio::engine_adapter::devicesIn(tracks, *master).empty();
}

}  // namespace magda::daw::engine_host
