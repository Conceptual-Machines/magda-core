// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TrackManager.hpp"

namespace magda {

/// How much darker each track draws its colour, for juce::Colour::darker. Absent is none.
using TrackShades = std::unordered_map<TrackId, float>;

inline constexpr float kGroupShade = 0.5f;
inline constexpr float kNeighbourShade = 0.2f;

inline float shadeOf(const TrackShades& shades, TrackId trackId) {
    const auto found = shades.find(trackId);
    return found == shades.end() ? 0.0f : found->second;
}

/**
 * @brief The shades for tracks in the order a view draws them (#3228).
 *
 * A group goes darker when one of the tracks shown inside it shares its colour, so the parent
 * stands apart from its children. Otherwise neighbours of one colour alternate, or they would
 * read as one track. A separator, or INVALID_TRACK_ID for a gap in the view, ends a run.
 */
inline TrackShades trackShades(const std::vector<TrackId>& displayOrder) {
    const auto& tracks = TrackManager::getInstance();

    std::unordered_set<TrackId> darkerGroups;
    for (const auto trackId : displayOrder) {
        const auto* track = tracks.getTrack(trackId);
        const auto* parent = track != nullptr ? tracks.getTrack(track->parentId) : nullptr;
        if (parent != nullptr && parent->isGroup() && parent->colour == track->colour &&
            std::ranges::find(displayOrder, parent->id) != displayOrder.end())
            darkerGroups.insert(parent->id);
    }

    TrackShades shades;
    const TrackInfo* previous = nullptr;
    float previousShade = 0.0f;
    for (const auto trackId : displayOrder) {
        const auto* track = tracks.getTrack(trackId);
        if (track == nullptr || track->isSeparator()) {
            previous = nullptr;
            continue;
        }

        float shade = 0.0f;
        if (darkerGroups.contains(trackId))
            shade = kGroupShade;
        else if (previous != nullptr && previous->colour == track->colour && previousShade == 0.0f)
            shade = kNeighbourShade;

        if (shade > 0.0f)
            shades[trackId] = shade;
        previous = track;
        previousShade = shade;
    }
    return shades;
}

}  // namespace magda
