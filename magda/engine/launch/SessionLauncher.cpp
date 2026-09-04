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

namespace {

/// One request against the table that is live now. A slot the table does not
/// name is one emptied between the ask and this block, and there is nothing
/// left to ask.
void apply(const LaunchRequest& request, const LaunchHandleTable& table) {
    auto* handle = table.find(request.key);
    if (handle == nullptr)
        return;

    switch (request.kind) {
        case LaunchRequest::Kind::play:
            if (request.syncTo) {
                // The run to join is the one that handle holds now, which is
                // why the request carries a slot and not a run: a scene launch
                // decided on the message thread would join where the leader was
                // a block ago.
                if (const auto* with = table.find(*request.syncTo); with != nullptr) {
                    handle->playSynced(*with, request.position);
                    return;
                }

                // The leader's slot was emptied in between. Starting alone is
                // the honest answer: the clip was asked to play, and there is
                // no longer anything for it to be in phase with.
            }

            handle->play(request.position);
            return;

        case LaunchRequest::Kind::stop:
            handle->stop(request.position);
            return;

        case LaunchRequest::Kind::backToArrangement:
            handle->releaseSection();
            return;

        case LaunchRequest::Kind::loop:
            handle->setLooping(request.position);
            return;
    }
}

}  // namespace

void advanceLaunchHandles(LaunchHandleFeed& handles, LaunchRequestQueue& requests,
                          const BlockInfo& block) {
    const LaunchHandleFeed::Reader table(handles);

    // Drained whether or not there is a table to apply it to. A queue left
    // filling while no session is published would deliver a launch made minutes
    // ago at whatever moment one appeared.
    if (!table) {
        requests.drain([](const LaunchRequest&) {});
        return;
    }

    // Every request before any advance, so a scene reaches all of its handles
    // while they are still on the same block.
    requests.drain([&table](const LaunchRequest& request) { apply(request, *table.get()); });

    const auto range = syncRangeFor(block);

    for (const auto& entry : table->entries)
        if (entry.handle != nullptr)
            entry.handle->advance(range);
}

}  // namespace magda::engine
