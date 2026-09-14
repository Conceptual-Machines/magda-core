#pragma once

#include <optional>
#include <vector>

#include "../AudioEngine.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "core/ClipTypes.hpp"
#include "core/TrackInfo.hpp"
#include "exec/RenderContext.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file TrackFreeze.hpp
 * @brief A track rendered to a file and played back from it on magda::engine (#2555).
 *
 * The file holds the track's signal up to its fader, with every track routed
 * into it. Live playback compiles the track as a clip on that file with nothing
 * before the fader, and leaves out the tracks feeding it; their devices stay in
 * the store, so an unfreeze carries their state.
 */

namespace magda::daw::engine_host {

/** @brief A frozen track and the pooled file it plays. */
struct FrozenTrack {
    TrackId trackId = INVALID_TRACK_ID;
    SourceId source = INVALID_SOURCE_ID;
    double durationSeconds = 0.0;

    bool operator==(const FrozenTrack&) const = default;
};

/** @brief Where @p trackId's freeze lives in the open project, or no file without a project. */
juce::File freezeFileFor(TrackId trackId);

/** @brief Every track whose output reaches @p trackId, through any number of tracks. */
std::vector<TrackId> tracksFeeding(const std::vector<TrackInfo>& tracks, TrackId trackId);

/** @brief Why @p trackId cannot be frozen, or empty when it can. */
juce::String freezeRefusal(const std::vector<TrackInfo>& tracks, TrackId trackId);

/**
 * @brief The render that freezes @p trackId into @p destination at @p context.
 *
 * From beat zero to the last clip on the track or anything feeding it, plus
 * the tail its devices declare. Nothing when there are no clips to render.
 */
std::optional<OfflineRenderRequest> freezeRequest(const std::vector<TrackInfo>& tracks,
                                                  const std::vector<engine::ClipLane>& lanes,
                                                  TrackId trackId, const juce::File& destination,
                                                  const engine::RenderContext& context);

/** @brief @p tracks as live playback compiles them, with each of @p frozen reading its file. */
std::vector<TrackInfo> tracksAsPlayed(std::vector<TrackInfo> tracks,
                                      const std::vector<FrozenTrack>& frozen);

/** @brief @p lanes of @p tracks with each of @p frozen playing its file from beat zero. */
std::vector<engine::ClipLane> lanesAsPlayed(std::vector<engine::ClipLane> lanes,
                                            const std::vector<TrackInfo>& tracks,
                                            const std::vector<FrozenTrack>& frozen,
                                            const engine::TempoMap& tempo);

/** @brief The frozen tracks among @p tracks whose file is on disk, pooling each. Message thread. */
std::vector<FrozenTrack> frozenTracksWithFiles(const std::vector<TrackInfo>& tracks);

/** @brief Read @p file's facts again, after a render replaced what the pool knew. */
void refreshPooledFile(const juce::File& file);

/** @brief Unfreeze every frozen track whose file is gone. Message thread. */
void unfreezeTracksWithoutFiles();

}  // namespace magda::daw::engine_host
