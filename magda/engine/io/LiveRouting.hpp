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

/// Matches every stream in the callback.
constexpr LiveMidiSourceId kAnyLiveMidiSource = -1;

/// Sentinel below the host's ids, which start at 1.
constexpr LiveMidiSourceId kNoLiveMidiSource = 0;

/**
 * @brief What one track hears of the live MIDI.
 *
 * The host resolves it from the track's monitor mode, its arm, the device its
 * route names, and that device's connection state.
 */
struct TrackLiveMidi {
    TrackId trackId = INVALID_TRACK_ID;

    /// The track's piano-roll preview, independent of the input routing.
    LiveMidiSourceId audition = kNoLiveMidiSource;

    std::vector<LiveMidiSourceId> sources;

    /// Counts the sources this track has lost. A change raises all-notes-off,
    /// which releases the notes the departed source held (#2418).
    std::uint32_t sourcesLost = 0;

    bool operator==(const TrackLiveMidi&) const = default;
};

/**
 * @brief Every track's routing for one reading of the model.
 *
 * Immutable once published. A route change produces a new one.
 */
struct LiveRouting {
    /// Sorted by trackId, which @ref find binary-searches.
    std::vector<TrackLiveMidi> tracks;

    /** @brief @p trackId's entry, or null. */
    const TrackLiveMidi* find(TrackId trackId) const {
        const auto byId = [](const TrackLiveMidi& entry, TrackId id) { return entry.trackId < id; };
        const auto found = std::lower_bound(tracks.begin(), tracks.end(), trackId, byId);
        return found != tracks.end() && found->trackId == trackId ? &*found : nullptr;
    }
};

}  // namespace magda::engine
