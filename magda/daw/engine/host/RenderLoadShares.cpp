// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RenderLoadShares.hpp"

#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"
#include "exec/RenderLoad.hpp"

namespace magda {

std::vector<LoadDevice> loadDevicesOf(const TrackInfo& track) {
    std::vector<LoadDevice> devices;
    for (const auto& found : daw::audio::engine_adapter::devicesInTrack(track))
        devices.push_back({.segment = found.key.segment,
                           .id = found.key.deviceId,
                           .name = found.device->name,
                           .path = found.path});
    return devices;
}

bool RenderLoadShares::update(const engine::RenderLoad& load) {
    const auto budget = load.budgetNanos();
    if (budget <= previousBudget_)
        return false;
    const auto elapsed = static_cast<double>(budget - previousBudget_);
    previousBudget_ = budget;

    shares_.clear();
    tracks_.clear();
    all_ = {};
    load.forEach([&](const engine::RenderLoad::Entry& entry) {
        const Key key{entry.track, entry.segment, entry.device};
        auto& before = previous_[key];
        const LoadShare share{.callback = static_cast<double>(entry.nanos[0] - before[0]) / elapsed,
                              .ahead = static_cast<double>(entry.nanos[1] - before[1]) / elapsed};
        before = entry.nanos;

        shares_[key] = share;
        auto& track = tracks_[entry.track];
        track.callback += share.callback;
        track.ahead += share.ahead;
        all_.callback += share.callback;
        all_.ahead += share.ahead;
    });
    return true;
}

LoadShare RenderLoadShares::track(TrackId track) const {
    const auto found = tracks_.find(track);
    return found != tracks_.end() ? found->second : LoadShare{};
}

LoadShare RenderLoadShares::device(TrackId track, ChainSegment segment, DeviceId device) const {
    const auto found = shares_.find({track, segment, device});
    return found != shares_.end() ? found->second : LoadShare{};
}

}  // namespace magda
