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

/** @brief Which MIDI input a stream came from, assigned by the host. */
using LiveMidiSourceId = int;

/// The "all" a route also accepts: every stream in the callback.
constexpr LiveMidiSourceId kAnyLiveMidiSource = -1;

/// No source. Host ids start at 1, so this matches no stream.
constexpr LiveMidiSourceId kNoLiveMidiSource = 0;

/**
 * @brief What one track hears of the live MIDI.
 *
 * The host has already applied the track's monitor mode, its arm, the device
 * its route names, and whether that device is connected.
 */
struct TrackLiveMidi {
    TrackId trackId = INVALID_TRACK_ID;

    /// The track's piano-roll preview, independent of the input routing.
    LiveMidiSourceId audition = kNoLiveMidiSource;

    std::vector<LiveMidiSourceId> sources;

    /// Bumped when a source has left since the snapshot before. That source
    /// sends no note-off for the notes it held, so the input raises
    /// all-notes-off (#2418).
    std::uint32_t sourcesLost = 0;

    bool operator==(const TrackLiveMidi&) const = default;
};

/**
 * @brief Every track's routing for one reading of the model.
 *
 * Immutable once published: a route change makes a new one.
 */
struct LiveRouting {
    /// Sorted by trackId, which @ref find binary-searches.
    std::vector<TrackLiveMidi> tracks;

    /** @brief @p trackId's entry, or null for a track the snapshot omits. */
    const TrackLiveMidi* find(TrackId trackId) const {
        const auto byId = [](const TrackLiveMidi& entry, TrackId id) { return entry.trackId < id; };
        const auto found = std::lower_bound(tracks.begin(), tracks.end(), trackId, byId);
        return found != tracks.end() && found->trackId == trackId ? &*found : nullptr;
    }
};

}  // namespace magda::engine
