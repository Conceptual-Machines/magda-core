#include "EngineProject.hpp"

#include <algorithm>

#include "../../core/AutomationManager.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/ControlTarget.hpp"
#include "../../core/SourcePool.hpp"
#include "../../core/TempoLane.hpp"

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

std::vector<engine::TempoChange> tempoLaneChanges() {
    auto& automation = AutomationManager::getInstance();
    const auto* lane = automation.getLane(automation.getLaneForTarget(ControlTarget::tempo()));
    // A lane with one point is a new one, still at its default rather than the project tempo.
    if (lane == nullptr || lane->absolutePoints.size() < 2)
        return {};

    auto points = lane->absolutePoints;
    std::ranges::sort(points, {}, &AutomationPoint::beatPosition);
    std::vector<engine::TempoChange> changes;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i > 0 && points[i].beatPosition <= 0.0)
            continue;
        changes.push_back({.startBeat = i == 0 ? 0.0 : points[i].beatPosition,
                           .bpm = tempo_lane::normalizedToBpm(points[i].value),
                           .tension = tempo_lane::segmentTension(points, i)});
    }
    return changes;
}

engine::TempoMap tempoMapFor(const std::vector<engine::TempoChange>& changes, double bpm,
                             int numerator, int denominator) {
    if (changes.empty())
        return tempoMapAt(bpm, numerator, denominator);
    return engine::TempoMap(changes, {engine::TimeSignatureChange{
                                         .startBeat = 0.0,
                                         .numerator = numerator,
                                         .denominator = denominator,
                                     }});
}

double projectEndBeat() {
    double end = 0.0;

    for (const auto& clip : ClipManager::getInstance().getArrangementClips())
        end = std::max(end, clip.placement.endBeat());

    return end;
}

}  // namespace magda::daw::engine_host
