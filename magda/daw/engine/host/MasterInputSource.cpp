// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MasterInputSource.hpp"

#include "../../core/SourcePool.hpp"

namespace magda::daw::engine_host {

namespace {

/// Below INVALID_CLIP_ID and above every freeze clip id (-2 - trackId, trackId >= 1).
constexpr ClipId MASTER_INPUT_CLIP_ID = -2;

}  // namespace

TrackInfo masterInputTrack() {
    TrackInfo track;
    track.id = MASTER_INPUT_TRACK_ID;
    track.type = TrackType::Media;
    track.name = "Master input";
    track.audioOutputDevice = "master";
    return track;
}

engine::ClipLane masterInputLane(const MasterInput& input, const engine::TempoMap& tempo) {
    ClipInfo clip;
    clip.id = MASTER_INPUT_CLIP_ID;
    clip.trackId = MASTER_INPUT_TRACK_ID;
    clip.name = "Master input";
    clip.setAudioContent();

    AudioEvent event;
    event.sourceId = input.source;
    clip.audio().addEvent(event);
    clip.setPlacementBeats(0.0, tempo.timeToBeat(input.durationSeconds));

    engine::ClipLane lane;
    lane.trackId = MASTER_INPUT_TRACK_ID;
    lane.clips = {std::move(clip)};
    return lane;
}

std::optional<MasterInput> pooledMasterInput(const juce::File& file) {
    if (!file.existsAsFile())
        return std::nullopt;

    auto& pool = SourcePool::getInstance();
    const auto id = pool.acquire(file.getFullPathName());
    const auto* source = pool.get(id);
    if (source == nullptr || !source->isResolved())
        return std::nullopt;

    return MasterInput{.source = id, .durationSeconds = source->durationSeconds};
}

}  // namespace magda::daw::engine_host
