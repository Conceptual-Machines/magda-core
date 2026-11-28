// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AuditionSources.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <limits>

#include "../../core/SourcePool.hpp"
#include "MasterInputSource.hpp"

namespace magda::daw::engine_host {

namespace {

/// Far below every freeze clip id (-2 - trackId) and the master input's.
constexpr ClipId FIRST_AUDITION_CLIP_ID = std::numeric_limits<ClipId>::min();

}  // namespace

TrackId auditionTrackId(std::size_t slot) {
    return MASTER_INPUT_TRACK_ID - 1 - static_cast<TrackId>(slot);
}

TrackInfo auditionTrack(std::size_t slot, const AuditionSource& source) {
    TrackInfo track;
    track.id = auditionTrackId(slot);
    track.type = TrackType::Media;
    track.name = "Audition " + juce::String(static_cast<int>(slot) + 1);
    track.volume = juce::Decibels::decibelsToGain(source.gainDecibels, -100.0f);
    track.soloSafe = true;
    return track;
}

engine::ClipLane auditionLane(std::size_t slot, const AuditionSource& source,
                              const engine::TempoMap& tempo) {
    const auto trackId = auditionTrackId(slot);

    ClipInfo clip;
    clip.id = FIRST_AUDITION_CLIP_ID + static_cast<ClipId>(slot);
    clip.trackId = trackId;
    clip.name = "Audition";
    clip.setAudioContent();

    AudioEvent event;
    event.sourceId = source.source;
    clip.audio().addEvent(event);
    const auto start = std::max(0.0, source.offsetBeats);
    const auto end = tempo.timeToBeat(tempo.beatToTime(start) + source.durationSeconds);
    clip.setPlacementBeats(start, end - start);

    engine::ClipLane lane;
    lane.trackId = trackId;
    lane.clips = {std::move(clip)};
    return lane;
}

std::optional<AuditionSource> pooledAuditionSource(const juce::File& file, float gainDecibels,
                                                   double offsetBeats) {
    if (!file.existsAsFile())
        return std::nullopt;

    auto& pool = SourcePool::getInstance();
    const auto id = pool.acquire(file.getFullPathName());
    const auto* pooled = pool.get(id);
    if (pooled == nullptr || !pooled->isResolved())
        return std::nullopt;

    return AuditionSource{.source = id,
                          .durationSeconds = pooled->durationSeconds,
                          .gainDecibels = gainDecibels,
                          .offsetBeats = offsetBeats};
}

}  // namespace magda::daw::engine_host
