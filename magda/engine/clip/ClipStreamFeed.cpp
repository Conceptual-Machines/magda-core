#include "clip/ClipStreamFeed.hpp"

#include <algorithm>

namespace magda::engine {

std::pair<const ClipStreamTable::Entry*, const ClipStreamTable::Entry*> ClipStreamTable::rangeFor(
    TrackId trackId) const {
    const auto byTrack = [](const Entry& entry, TrackId id) { return entry.trackId < id; };
    const auto trackFirst = [](TrackId id, const Entry& entry) { return id < entry.trackId; };

    const auto* begin = entries.data();
    const auto* end = begin + entries.size();

    const auto* from = std::lower_bound(begin, end, trackId, byTrack);
    const auto* to = std::upper_bound(from, end, trackId, trackFirst);
    return {from, to};
}

}  // namespace magda::engine
