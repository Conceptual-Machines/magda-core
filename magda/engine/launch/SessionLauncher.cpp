#include "launch/SessionLauncher.hpp"

#include <algorithm>

#include "launch/FollowActions.hpp"

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

const LaunchHandleTable::Entry* LaunchHandleTable::findEntry(const SlotKey& key) const {
    const auto [from, to] = rangeFor(key.trackId);

    const auto* found =
        std::lower_bound(from, to, key, [](const Entry& entry, const SlotKey& wanted) {
            return entry.key < wanted;
        });

    return found != to && found->key == key ? found : nullptr;
}

LaunchHandle* LaunchHandleTable::find(const SlotKey& key) const {
    const auto* entry = findEntry(key);
    return entry != nullptr ? entry->handle : nullptr;
}

namespace {

/**
 * @brief Apply @p request against the table that is live now.
 *
 * Dropped when the slot has gone, and equally when it was emptied and refilled:
 * that is the same key on a different clip, and launching it would start
 * something the user never clicked. The incarnation is what tells those apart,
 * and it is a number rather than a pointer, so nothing here can name a retired
 * handle.
 */
void apply(const LaunchRequest& request, const LaunchHandleTable& table) {
    const auto* entry = table.findEntry(request.key);
    if (entry == nullptr || entry->handle == nullptr || entry->incarnation != request.incarnation)
        return;

    auto* handle = entry->handle;

    switch (request.kind) {
        case LaunchRequest::Kind::play:
            if (request.syncTo) {
                // A slot and not a run, so the run joined is the one the leader
                // holds now rather than where it was when the gesture was made.
                if (const auto* with = table.find(*request.syncTo); with != nullptr) {
                    // A leader with a launch of its own still queued has no run
                    // to join yet, and the one it is about to begin is the one
                    // the follower means. Both then begin on the same instant,
                    // which is the same origin (#2336); joining what it is
                    // playing would leave a relaunched scene out of phase with
                    // itself.
                    if (with->queuedState() == LaunchHandle::QueueState::playQueued) {
                        handle->play(with->queuedPosition());
                        return;
                    }

                    handle->playSynced(*with, request.position);
                    return;
                }

                // The leader's slot was emptied in between, so there is nothing
                // left to be in phase with.
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

/**
 * @brief End the runs that reach their end inside @p range, and start what
 *        follows them (#2304).
 *
 * Before the advance, so the stop and the launch it triggers land on one beat
 * inside this block rather than on the next callback boundary.
 *
 * A slot with anything queued is left alone: a handle holds one pending request,
 * so writing a stop over it would drop what the user asked for.
 */
void applyDueFollowActions(const LaunchHandleTable& table, const SyncRange& range) {
    for (const auto& entry : table.entries) {
        if (entry.handle == nullptr ||
            entry.handle->playState() != LaunchHandle::PlayState::playing)
            continue;

        if (entry.handle->queuedState())
            continue;

        const auto due = followDueBeat(*entry.handle, entry.follow);
        if (!due || *due >= range.monotonic.end)
            continue;

        // A run whose end was already behind this block: one shorter than a
        // callback, whose launch was this block's one event and left the end
        // nowhere to go. The handle clamps it to the first sample here, and the
        // block it slipped is counted rather than left silent (#2304 review).
        if (*due < range.monotonic.start)
            entry.handle->noteLateRunEnd();

        auto* target = followTarget(table, entry.key, entry.follow, *due);

        // Asked before the stop below, so a slot following itself is not turned
        // away by the stop this pass is about to write. A target already spoken
        // for keeps what it was asked: a handle holds one pending request, and
        // the user's outranks this one.
        const auto spokenFor = target != nullptr && target->queuedState().has_value();

        entry.handle->stop(*due);

        if (target != nullptr && !spokenFor)
            target->play(*due);
    }
}

}  // namespace

void advanceLaunchHandles(LaunchHandleFeed& handles, LaunchRequestQueue& requests,
                          const BlockInfo& block) {
    const LaunchHandleFeed::Reader table(handles);

    // Drained whether or not there is a table to apply it to: a queue left
    // filling would deliver a launch made minutes ago at whatever moment a
    // session appeared.
    if (!table) {
        requests.drain([](const LaunchRequest&) {});
        return;
    }

    // Every request before any advance, so a scene reaches all of its handles
    // on the same block.
    requests.drain([&table](const LaunchRequest& request) { apply(request, *table.get()); });

    const auto range = syncRangeFor(block);

    // After the requests, so a launch made in this block beats the follow
    // action of the run it replaces.
    applyDueFollowActions(*table.get(), range);

    for (const auto& entry : table->entries)
        if (entry.handle != nullptr) {
            entry.handle->advance(range);

            // Published by the block that decided it, so the UI is never a
            // frame behind the audio and has nothing to poll (#2303).
            if (entry.tap != nullptr)
                entry.tap->write(*entry.handle);
        }
}

}  // namespace magda::engine
