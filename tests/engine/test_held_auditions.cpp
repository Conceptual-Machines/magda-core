// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <utility>
#include <vector>

#include "../../magda/daw/engine/host/HeldAuditions.hpp"

// A preview on a track rendered ahead waits for the plan that plays it live (#1898).

using magda::TrackId;
using magda::daw::engine_host::HeldAuditions;

namespace {

constexpr TrackId kGrid = 6;
constexpr TrackId kSynth = 2;

std::vector<std::pair<TrackId, int>> released(HeldAuditions& held) {
    std::vector<std::pair<TrackId, int>> notes;
    held.release([&notes](TrackId trackId, const juce::MidiMessage& message) {
        notes.emplace_back(trackId,
                           message.isNoteOn() ? message.getNoteNumber() : -message.getNoteNumber());
    });
    return notes;
}

}  // namespace

TEST_CASE("A preview on a live track plays at once", "[engine][1898][audition]") {
    HeldAuditions held;
    CHECK(held.admit(kGrid, juce::MidiMessage::noteOn(1, 36, 1.0f), true));
    CHECK_FALSE(held.waiting());
    CHECK(held.promoted().empty());
}

TEST_CASE("A preview on a track rendered ahead promotes it and waits", "[engine][1898][audition]") {
    HeldAuditions held;
    CHECK_FALSE(held.admit(kGrid, juce::MidiMessage::noteOn(1, 36, 1.0f), false));
    CHECK(held.waiting());
    CHECK(held.promoted() == std::set<TrackId>{kGrid});
}

TEST_CASE("A note-off behind a held note-on waits with it, though the track is live by then",
          "[engine][1898][audition]") {
    HeldAuditions held;
    held.admit(kGrid, juce::MidiMessage::noteOn(1, 36, 1.0f), false);
    CHECK_FALSE(held.admit(kGrid, juce::MidiMessage::noteOff(1, 36), true));

    // Another track's preview has nothing to wait behind.
    CHECK(held.admit(kSynth, juce::MidiMessage::noteOn(1, 60, 1.0f), true));

    CHECK(released(held) == std::vector<std::pair<TrackId, int>>{{kGrid, 36}, {kGrid, -36}});
    CHECK_FALSE(held.waiting());
    CHECK(held.admit(kGrid, juce::MidiMessage::noteOn(1, 38, 1.0f), true));
}

TEST_CASE("A promotion lasts until it is forgotten", "[engine][1898][audition]") {
    HeldAuditions held;
    held.admit(kGrid, juce::MidiMessage::noteOn(1, 36, 1.0f), false);
    released(held);
    CHECK(held.promoted() == std::set<TrackId>{kGrid});

    held.forgetPromotions();
    CHECK(held.promoted().empty());
}
