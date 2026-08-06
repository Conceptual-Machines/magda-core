#include "clip/ClipSnapshot.hpp"

#include <algorithm>

namespace magda::engine {

const TrackClipPlayback* ClipSnapshot::find(TrackId trackId) const {
    const auto it = std::lower_bound(
        tracks.begin(), tracks.end(), trackId,
        [](const TrackClipPlayback& track, TrackId id) { return track.trackId < id; });
    if (it == tracks.end() || it->trackId != trackId)
        return nullptr;
    return &*it;
}

}  // namespace magda::engine
