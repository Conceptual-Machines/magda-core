// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <numeric>
#include <optional>
#include <ranges>
#include <vector>

#include "ClipInfo.hpp"

/**
 * @file ClipTimeBend.hpp
 * @brief Where a MIDI clip's notes play under its time bend, for the editors to draw (#3044).
 */

namespace magda {

/** @brief One strike of a written note: the note, and the content beat it plays at. */
struct PlayedNote {
    size_t noteIndex = 0;
    double startBeat = 0.0;
};

/**
 * @brief Every strike @p clip's bend plays, in time order, in a meter of @p beatsPerBar.
 *
 * Mapped onto the timeline as playback maps it (the fold in MidiEventList.cpp), over the clip's
 * span, or the first pass of its loop. A note the head crosses twice is struck twice; one it holds
 * over is not struck. Notes outside the loop window are left where they are written.
 */
inline std::vector<PlayedNote> playedNotes(const ClipInfo& clip, double beatsPerBar) {
    std::vector<PlayedNote> played;
    if (!clip.isMidi() || !clip.timeBend.isActive()) {
        for (size_t i = 0; i < clip.midiNotes.size(); ++i)
            played.push_back({i, clip.midiNotes[i].startBeat});
        return played;
    }

    const auto start = clip.placement.startBeat;
    const bool looped = clip.loopEnabled && clip.loopLengthBeats > 0.0;
    const double zero = looped ? start - clip.midiOffset - clip.loopStartBeats
                               : start - std::max(0.0, clip.midiTrimOffset) - clip.midiOffset;
    const double windowStart = looped ? clip.loopStartBeats : start - zero;
    const double windowEnd =
        looped ? clip.loopStartBeats + clip.loopLengthBeats : clip.placement.endBeat() - zero;

    std::vector<size_t> order(clip.midiNotes.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::stable_sort(order, {}, [&](size_t i) { return clip.midiNotes[i].startBeat; });

    const auto notesIn = [&](double lo, double hi, const auto& visit) {
        for (const auto i : order) {
            const auto c = clip.midiNotes[i].startBeat;
            if (c < windowStart || c >= windowEnd)
                continue;
            if (zero + c > hi)
                break;
            if (zero + c >= lo)
                visit(zero + c, i);
        }
    };
    sdk::sequencer::forEachBentHit(clip.timeBend, clip.timeBend.cycleBeats(beatsPerBar),
                                   zero + windowStart, zero + windowEnd, notesIn,
                                   [&](double beat, size_t i) {
                                       played.push_back({i, beat - zero});
                                   });

    for (size_t i = 0; i < clip.midiNotes.size(); ++i) {
        const auto c = clip.midiNotes[i].startBeat;
        if (c < windowStart || c >= windowEnd)
            played.push_back({i, c});
    }
    std::ranges::stable_sort(played, {}, &PlayedNote::startBeat);
    return played;
}

/**
 * @brief @p clip's notes rewritten where its bend plays them; a note struck twice is written
 *        twice, one never struck is dropped. Nullopt when no bend is in force.
 */
inline std::optional<MidiEventState> bakeTimeBend(const ClipInfo& clip, double beatsPerBar) {
    if (!clip.isMidi() || !clip.timeBend.isActive())
        return std::nullopt;

    auto state = clip.midiEventState();
    std::vector<MidiNote> baked;
    std::vector<bool> used(clip.midiNotes.size(), false);
    for (const auto& played : playedNotes(clip, beatsPerBar)) {
        auto note = clip.midiNotes[played.noteIndex];
        note.startBeat = played.startBeat;
        // A note struck again is a new note; the first strike keeps the written one's id.
        if (used[played.noteIndex])
            note.id = state.nextEventId++;
        used[played.noteIndex] = true;
        baked.push_back(std::move(note));
    }
    state.notes = std::move(baked);
    return state;
}

}  // namespace magda
