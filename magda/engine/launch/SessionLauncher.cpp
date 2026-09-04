#include "launch/SessionLauncher.hpp"

#include <algorithm>

namespace magda::engine {

std::pair<const LaunchHandleTable::Entry*, const LaunchHandleTable::Entry*>
LaunchHandleTable::rangeFor(TrackId trackId) const {
    const auto byTrack = [](const Entry& entry, TrackId id) { return entry.key.trackId < id; };
    const auto trackFirst = [](TrackId id, const Entry& entry) { return id < entry.key.trackId; };

    const auto* begin = entries.data();
    const auto* end = begin + entries.size();

    const auto* from = std::lower_bound(begin, end, trackId, byTrack);
    const auto* to = std::upper_bound(from, end, trackId, trackFirst);
    return {from, to};
}

LaunchHandle* LaunchHandleTable::find(const SlotKey& key) const {
    const auto [from, to] = rangeFor(key.trackId);

    const auto* found =
        std::lower_bound(from, to, key, [](const Entry& entry, const SlotKey& wanted) {
            return entry.key < wanted;
        });

    return found != to && found->key == key ? found->handle : nullptr;
}

void advanceLaunchHandles(LaunchHandleFeed& handles, const BlockInfo& block) {
    const LaunchHandleFeed::Reader table(handles);
    if (!table)
        return;

    const auto range = syncRangeFor(block);

    for (const auto& entry : table->entries)
        if (entry.handle != nullptr)
            entry.handle->advance(range);
}

}  // namespace magda::engine
