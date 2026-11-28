// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>

#include "clip/ClipSnapshotCompiler.hpp"
#include "core/TrackInfo.hpp"
#include "plan/RenderPlan.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file AuditionSources.hpp
 * @brief Files played beside the master path, one of which the output select lets through (#3081).
 *
 * Each plays on a track of its own, clear of the master chain and fader, in sync with the
 * transport. Nothing about them is saved with the project.
 */

namespace magda::daw::engine_host {

/** @brief A pooled file, its gain and where on the timeline it starts. */
struct AuditionSource {
    SourceId source = INVALID_SOURCE_ID;
    double durationSeconds = 0.0;
    float gainDecibels = 0.0f;
    double offsetBeats = 0.0;

    bool operator==(const AuditionSource&) const = default;
};

using AuditionSlots = std::array<std::optional<AuditionSource>, engine::kMaxAuditionSources>;

/// Below MASTER_INPUT_TRACK_ID, one per slot.
TrackId auditionTrackId(std::size_t slot);

/** @brief The track @p source plays on in @p slot: its fader is the gain. */
TrackInfo auditionTrack(std::size_t slot, const AuditionSource& source);

/** @brief The lane playing @p source from its offset. */
engine::ClipLane auditionLane(std::size_t slot, const AuditionSource& source,
                              const engine::TempoMap& tempo);

/** @brief @p file pooled as an audition source, or nothing when it does not resolve. */
std::optional<AuditionSource> pooledAuditionSource(const juce::File& file, float gainDecibels,
                                                   double offsetBeats);

}  // namespace magda::daw::engine_host
