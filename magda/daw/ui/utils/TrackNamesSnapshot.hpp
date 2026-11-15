// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include "core/TrackManager.hpp"

namespace magda {

/**
 * @brief The track names a view last built its labels and routing menus from.
 *
 * A rename notifies only the renamed track, but other tracks' sends and menus name it too. A
 * view that stays built while hidden checks this when shown, instead of rebuilding every time.
 */
class TrackNamesSnapshot {
  public:
    void take() {
        names_.clear();
        for (const auto& track : TrackManager::getInstance().getTracks())
            names_.emplace_back(track.id, track.name);
    }

    bool isStale() const {
        const auto matches = [](const TrackInfo& track, const auto& taken) {
            return track.id == taken.first && track.name == taken.second;
        };
        return !std::ranges::equal(TrackManager::getInstance().getTracks(), names_, matches);
    }

  private:
    std::vector<std::pair<TrackId, juce::String>> names_;
};

}  // namespace magda
