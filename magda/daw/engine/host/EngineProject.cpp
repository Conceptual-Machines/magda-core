#include "EngineProject.hpp"

#include <algorithm>

#include "../../core/ClipManager.hpp"
#include "../../core/SourcePool.hpp"
#include "io/SourceLoopInfo.hpp"

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

void installSourceTempoProbe() {
    SourcePool::getInstance().setSourceTempoProbe([](Source& source,
                                                     const juce::AudioFormatReader& reader) {
        // A gap only: a project that saved a tempo for this file, or a user
        // who typed one, outranks what the header claims.
        if (source.detectedBpm > 0.0)
            return;

        const auto info =
            engine::loopInfoFrom(reader.metadataValues, reader.sampleRate, reader.lengthInSamples);
        if (info.bpm && *info.bpm > 0.0)
            source.detectedBpm = *info.bpm;
    });
}

double projectEndBeat() {
    double end = 0.0;

    for (const auto& clip : ClipManager::getInstance().getArrangementClips())
        end = std::max(end, clip.placement.endBeat());

    return end;
}

}  // namespace magda::daw::engine_host
