#pragma once

#include <map>
#include <utility>
#include <vector>

#include "TrackInfo.hpp"

/**
 * @brief Each Drum Grid pad that holds something is a mixer channel (#3010).
 *
 * The channel is a Pad track, a child of the grid's track, linked to its pad by
 * `TrackInfo::padLink`. Its output defaults to the grid's track, which the plan
 * reads as "back into the grid's own mix".
 */
namespace magda::pad_channels {

/// Pad tracks removed by a sync, kept so a pad that comes back (an undo, a
/// redo) gets its channel back with the same id, settings and sends.
using Stash = std::map<std::pair<DeviceId, ChainId>, TrackInfo>;

/// The route a pad channel takes into its grid's own mix.
juce::String gridRoute(TrackId gridTrackId);

/// A Pad track for every pad holding a device, none for the rest, each under its
/// grid's track in pad order. Also migrates pad buses (#2211): a bus fed by one pad
/// becomes its channel, one fed by several a group. True when anything changed.
bool sync(std::vector<TrackInfo>& tracks, TrackId& nextTrackId, Stash* stash = nullptr);

/// The Pad track for @p padId of grid @p gridId, or null.
TrackInfo* findChannel(std::vector<TrackInfo>& tracks, DeviceId gridId, ChainId padId);
const TrackInfo* findChannel(const std::vector<TrackInfo>& tracks, DeviceId gridId, ChainId padId);

}  // namespace magda::pad_channels
