// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exec/AheadLaunches.hpp"

#include <algorithm>

#include "clip/ClipSnapshotFeed.hpp"

namespace magda::engine {

namespace {

/// The same handle: a slot emptied and refilled is the same key on another.
bool sameHandle(const LaunchHandleTable::Entry& a, const LaunchHandleTable::Entry& b) {
    return a.key == b.key && a.incarnation == b.incarnation;
}

}  // namespace

AheadLaunches::AheadLaunches(const LaunchHandleTable& real, std::span<const TrackId> aheadTracks,
                             LaunchHandleFeed& realHandles, ClipSnapshotFeed& realClips)
    : tracks_(aheadTracks.begin(), aheadTracks.end()),
      realHandles_(realHandles),
      realClips_(realClips) {
    std::sort(tracks_.begin(), tracks_.end());

    states_.resize(tracks_.size());
    for (std::size_t i = 0; i < tracks_.size(); ++i)
        sections_.entries.push_back(
            TrackSectionTable::Entry{.trackId = tracks_[i], .state = &states_[i]});

    if (auto first = reshapeFor(real))
        adopt(*first);
}

bool AheadLaunches::covers(TrackId trackId) const {
    return std::binary_search(tracks_.begin(), tracks_.end(), trackId);
}

void AheadLaunches::forward(const LaunchRequest& request) {
    const auto at = written_.load(std::memory_order_relaxed);
    if (at - read_.load(std::memory_order_acquire) >= static_cast<std::uint64_t>(kCapacity)) {
        overflows_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    forwarded_[static_cast<std::size_t>(at % kCapacity)] = request;
    written_.store(at + 1, std::memory_order_release);
}

void AheadLaunches::beginAheadBlock(const BlockInfo& block) {
    const auto* clips = block.feeds != nullptr ? block.feeds->clips : nullptr;

    // The callback's order: loops, then requests, then follow actions and the advance. Waiting
    // for Link's phase, it advances nothing and leaves the requests queued.
    if (block.feeds == nullptr || !block.feeds->holdsLaunches) {
        adoptPublishedLoops(table_, clips);
        const auto end = written_.load(std::memory_order_acquire);
        for (auto at = read_.load(std::memory_order_acquire); at != end; ++at)
            applyLaunchRequest(forwarded_[static_cast<std::size_t>(at % kCapacity)], table_);
        read_.store(end, std::memory_order_release);
        advanceHandles(table_, block);
    }

    advanceTrackSections(&sections_, clips, &table_, block);
}

void AheadLaunches::renderedWhole() {
    // Inside the callback's block, so these are what it pinned and has just advanced.
    const LaunchHandleFeed::Reader real(realHandles_);
    for (std::size_t i = 0; i < table_.entries.size(); ++i) {
        const auto& copy = table_.entries[i];
        const auto* found = real ? real->findEntry(copy.key) : nullptr;
        copies_[i] = found != nullptr && found->handle != nullptr && sameHandle(*found, copy)
                         ? *found->handle
                         : LaunchHandle{};
    }

    const auto* sections = realClips_.sections();
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        const auto* state = sections != nullptr ? sections->find(tracks_[i]) : nullptr;
        states_[i] = state != nullptr ? *state : TrackSectionState{};
    }

    // The real handles have had everything forwarded so far, and so now have these.
    read_.store(written_.load(std::memory_order_acquire), std::memory_order_release);
}

std::optional<AheadLaunches::Reshape> AheadLaunches::reshapeFor(
    const LaunchHandleTable& real) const {
    Reshape next;
    auto unchanged = true;
    for (const auto& entry : real.entries) {
        if (!covers(entry.key.trackId))
            continue;
        const auto at = next.entries.size();
        unchanged = unchanged && at < table_.entries.size() &&
                    sameHandle(entry, table_.entries[at]) &&
                    entry.follow == table_.entries[at].follow;
        const auto* old = table_.findEntry(entry.key);
        next.from.push_back(old != nullptr && sameHandle(*old, entry)
                                ? static_cast<int>(old - table_.entries.data())
                                : -1);
        next.entries.push_back(LaunchHandleTable::Entry{
            .key = entry.key, .incarnation = entry.incarnation, .follow = entry.follow});
    }
    if (unchanged && next.entries.size() == table_.entries.size())
        return std::nullopt;

    // Sized once, so the entries' pointers into it hold through the swap in adopt.
    next.copies.resize(next.entries.size());
    for (std::size_t i = 0; i < next.entries.size(); ++i)
        next.entries[i].handle = &next.copies[i];
    return next;
}

void AheadLaunches::adopt(Reshape& next) {
    for (std::size_t i = 0; i < next.copies.size(); ++i)
        if (next.from[i] >= 0)
            next.copies[i] = copies_[static_cast<std::size_t>(next.from[i])];
    copies_.swap(next.copies);
    table_.entries.swap(next.entries);
    holdsLaunches_.store(!copies_.empty(), std::memory_order_release);
}

}  // namespace magda::engine
