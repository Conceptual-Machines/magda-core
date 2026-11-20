// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "../../core/ChainNodePath.hpp"
#include "../../core/TrackInfo.hpp"
#include "../../core/TypeIds.hpp"

namespace magda::engine {
class RenderLoad;
}

namespace magda {

/** @brief A share of real time, split by the thread that spent it. */
struct LoadShare {
    double callback = 0.0;
    double ahead = 0.0;

    double total() const {
        return callback + ahead;
    }
};

/** @brief A device as the CPU view lists it: where its time is filed, and its address. */
struct LoadDevice {
    ChainSegment segment = ChainSegment::Fx;
    DeviceId id = INVALID_DEVICE_ID;
    juce::String name;
    ChainNodePath path;
};

/// Every device of @p track in chain order, pads included.
std::vector<LoadDevice> loadDevicesOf(const TrackInfo& track);

/**
 * @brief Each track's and device's share of the audio time between two reads of a RenderLoad.
 *
 * The load keeps running sums; this keeps the previous read and turns the difference into
 * shares (#3357). Message thread.
 */
class RenderLoadShares {
  public:
    /// Reads @p load and replaces the shares with those since the last update. False, and the
    /// shares kept, when no audio time has passed.
    bool update(const engine::RenderLoad& load);

    /// The track's own ops and its devices together.
    LoadShare track(TrackId track) const;
    LoadShare device(TrackId track, ChainSegment segment, DeviceId device) const;

  private:
    using Key = std::tuple<TrackId, ChainSegment, DeviceId>;

    std::map<Key, std::array<std::uint64_t, 2>> previous_;
    std::uint64_t previousBudget_ = 0;
    std::map<Key, LoadShare> shares_;
    std::map<TrackId, LoadShare> tracks_;
};

}  // namespace magda
