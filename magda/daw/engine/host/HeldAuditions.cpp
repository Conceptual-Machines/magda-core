// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HeldAuditions.hpp"

#include <algorithm>

namespace magda::daw::engine_host {

bool HeldAuditions::admit(TrackId trackId, const juce::MidiMessage& message, bool trackIsLive) {
    const auto queued =
        std::ranges::any_of(held_, [trackId](const auto& held) { return held.first == trackId; });
    if (trackIsLive && !queued)
        return true;

    held_.emplace_back(trackId, message);
    promoted_.insert(trackId);
    return false;
}

void HeldAuditions::release(const std::function<void(TrackId, const juce::MidiMessage&)>& play) {
    auto held = std::exchange(held_, {});
    for (const auto& [trackId, message] : held)
        play(trackId, message);
}

}  // namespace magda::daw::engine_host
