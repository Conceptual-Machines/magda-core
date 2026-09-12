#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/TypeIds.hpp"

/**
 * @file LiveRouting.hpp
 * @brief Which live MIDI a track hears, as one published snapshot (#2592).
 *
 * The engine knows opaque source ids. Turning a device name, a monitor mode and
 * an arm into these is the host's (LiveMidiRouting.hpp).
 */

namespace magda::engine {

/// Which MIDI input a stream came from, assigned by the host.
using LiveMidiSourceId = int;

/// The "all" a route also accepts: every stream in the callback.
constexpr LiveMidiSourceId kAnyLiveMidiSource = -1;

/// No source. Never an id a stream arrives under, so reading it is silence.
constexpr LiveMidiSourceId kNoLiveMidiSource = 0;

/// What one track hears. Monitor mode, arm, the device the route names and
/// whether it is still connected are all already answered.
struct TrackLiveMidi {
    TrackId trackId = INVALID_TRACK_ID;

    /// Its own preview, heard whatever the input routing is.
    LiveMidiSourceId audition = kNoLiveMidiSource;

    std::vector<LiveMidiSourceId> sources;

    /// Bumped when a source has left since the snapshot before: the note-off it
    /// was holding is never coming, so the input panics for it (#2418).
    std::uint32_t sourcesLost = 0;

    bool operator==(const TrackLiveMidi&) const = default;
};

/// Every track's routing for one reading of the model, immutable once
/// published.
struct LiveRouting {
    /// Sorted by trackId, which @ref find binary-searches.
    std::vector<TrackLiveMidi> tracks;

    const TrackLiveMidi* find(TrackId trackId) const {
        const auto byId = [](const TrackLiveMidi& entry, TrackId id) { return entry.trackId < id; };
        const auto found = std::lower_bound(tracks.begin(), tracks.end(), trackId, byId);
        return found != tracks.end() && found->trackId == trackId ? &*found : nullptr;
    }
};

}  // namespace magda::engine
