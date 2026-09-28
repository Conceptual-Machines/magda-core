#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/TypeIds.hpp"

/**
 * @file LiveRouting.hpp
 * @brief Which live input a track hears, as one published snapshot (#2592).
 *
 * The engine knows opaque source ids and callback channel indices. Turning a
 * device name, a monitor mode and an arm into these is the host's
 * (LiveMidiRouting.hpp, HardwareInputMap.hpp).
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

/** @brief The hardware input channels one track reads (#2553). */
struct TrackLiveAudio {
    TrackId trackId = INVALID_TRACK_ID;

    /// Indices into the callback's input channels. Empty reads silence.
    std::vector<int> channels;

    bool operator==(const TrackLiveAudio&) const = default;
};

/**
 * @brief Every track's routing for one reading of the model.
 *
 * Immutable once published. A route change produces a new one.
 */
struct LiveRouting {
    /// Sorted by trackId, which @ref find binary-searches.
    std::vector<TrackLiveMidi> tracks;

    /// Sorted by trackId. Only tracks whose input names a hardware channel.
    std::vector<TrackLiveAudio> audio;

    /** @brief @p trackId's MIDI entry, or null. */
    const TrackLiveMidi* find(TrackId trackId) const {
        return findIn(tracks, trackId);
    }

    /** @brief @p trackId's audio entry, or null. */
    const TrackLiveAudio* findAudio(TrackId trackId) const {
        return findIn(audio, trackId);
    }

  private:
    template <typename Entry>
    static const Entry* findIn(const std::vector<Entry>& entries, TrackId trackId) {
        const auto byId = [](const Entry& entry, TrackId id) { return entry.trackId < id; };
        const auto found = std::lower_bound(entries.begin(), entries.end(), trackId, byId);
        return found != entries.end() && found->trackId == trackId ? &*found : nullptr;
    }
};

}  // namespace magda::engine
