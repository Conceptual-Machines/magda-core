// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "core/TrackInfo.hpp"

/**
 * @file StemRender.hpp
 * @brief Which tracks a stem export writes, and what each one renders (#54).
 */

namespace magda::daw::engine_host {

/** @brief Every track that gets a stem, in track-list order with the returns last. Muted, off and
 *         monitor-only tracks get none. */
std::vector<TrackId> stemTracks(const std::vector<TrackInfo>& tracks);

/**
 * @brief @p trackId and every track whose audio reaches it, which is what its stem renders.
 *
 * Through outputs and sends, and from a pad or multi-out channel to the track
 * whose instrument plays it.
 */
std::vector<TrackId> stemSources(const std::vector<TrackInfo>& tracks, TrackId trackId);

}  // namespace magda::daw::engine_host
