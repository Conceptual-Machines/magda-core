// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MixerStripOrder.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

#include "RangesHelpers.hpp"

namespace magda {

namespace {

const TrackInfo* findTrack(const std::vector<TrackInfo>& tracks, TrackId id) {
    const auto hasId = [id](const TrackInfo& track) { return track.id == id; };
    const auto found = std::ranges::find_if(tracks, hasId);
    return found == tracks.end() ? nullptr : &*found;
}

auto isStripIn(const std::vector<TrackInfo>& tracks, ViewMode mode) {
    return [&tracks, mode](const TrackInfo& track) { return isMixerStrip(track, tracks, mode); };
}

const TrackInfo* findChordStrip(const std::vector<TrackInfo>& tracks, ViewMode mode) {
    const auto isChordStrip = [&tracks, mode](const TrackInfo& track) {
        return track.type == TrackType::Chord && isMixerStrip(track, tracks, mode);
    };
    const auto found = std::ranges::find_if(tracks, isChordStrip);
    return found == tracks.end() ? nullptr : &*found;
}

}  // namespace

bool isMixerStrip(const TrackInfo& track, const std::vector<TrackInfo>& tracks, ViewMode mode) {
    if (!track.isVisibleIn(mode))
        return false;

    // Drawn beside the master, or not a channel at all: neither takes a position
    // among the strips.
    const auto traits = traitsOf(track.type);
    if (traits.sitsBesideMaster || !traits.carriesSignal)
        return false;

    if (track.hasParent()) {
        if (const auto* parent = findTrack(tracks, track.parentId)) {
            if ((parent->isGroup() || parent->hasChildren()) && parent->isCollapsedIn(mode))
                return false;
        }
    }
    return true;
}

std::vector<TrackId> mixerStripOrder(const std::vector<TrackInfo>& tracks, ViewMode mode) {
    auto order = tracks | std::views::filter(isStripIn(tracks, mode)) |
                 std::views::transform(&TrackInfo::id) | toStd<std::vector<TrackId>>();
    if (const auto* chord = findChordStrip(tracks, mode)) {
        const auto found = std::ranges::find(order, chord->id);
        std::rotate(order.begin(), found, std::next(found));
    }
    return order;
}

std::vector<TrackId> pinnedStripOrder(const std::vector<TrackInfo>& tracks, ViewMode mode) {
    const auto isPinned = [mode](const TrackInfo& track) {
        return track.isVisibleIn(mode) && traitsOf(track.type).sitsBesideMaster;
    };
    return tracks | std::views::filter(isPinned) | std::views::transform(&TrackInfo::id) |
           toStd<std::vector<TrackId>>();
}

TrackId mixerStripAtPosition(const std::vector<TrackInfo>& tracks, ViewMode mode, int position) {
    if (position < 1)
        return INVALID_TRACK_ID;

    const auto* chord = findChordStrip(tracks, mode);
    if (chord != nullptr && position == 1)
        return chord->id;

    // Positions count from 1, and the chord track has the first when there is one.
    const auto isOtherStrip = [&tracks, mode](const TrackInfo& track) {
        return track.type != TrackType::Chord && isMixerStrip(track, tracks, mode);
    };
    auto strips = tracks | std::views::filter(isOtherStrip);
    const int stepsAlong = position - (chord != nullptr ? 2 : 1);
    const auto found = std::ranges::next(strips.begin(), stepsAlong, strips.end());
    return found == strips.end() ? INVALID_TRACK_ID : found->id;
}

}  // namespace magda
