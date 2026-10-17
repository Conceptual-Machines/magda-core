#pragma once

#include <optional>

#include "clip/ClipSnapshotCompiler.hpp"
#include "core/TrackInfo.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file MasterInputSource.hpp
 * @brief A file played into the master chain in place of the tracks (#3081).
 *
 * Live playback compiles one track carrying the file as a clip, routed to the master with
 * nothing before its fader, and none of the model's tracks: the master chain, fader and meter
 * run on the file as they would on the mix. Nothing about it is saved with the project.
 */

namespace magda::daw::engine_host {

/// The track the file plays on: clear of every id the model hands out and of MASTER_TRACK_ID.
constexpr TrackId MASTER_INPUT_TRACK_ID = -3;

/** @brief A pooled file playing into the master. */
struct MasterInput {
    SourceId source = INVALID_SOURCE_ID;
    double durationSeconds = 0.0;

    bool operator==(const MasterInput&) const = default;
};

/** @brief The one track live playback compiles while a master input plays. */
TrackInfo masterInputTrack();

/** @brief The lane playing @p input from beat zero. */
engine::ClipLane masterInputLane(const MasterInput& input, const engine::TempoMap& tempo);

/** @brief @p file pooled as a master input, or nothing when it does not resolve. Message thread. */
std::optional<MasterInput> pooledMasterInput(const juce::File& file);

}  // namespace magda::daw::engine_host
