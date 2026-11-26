// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "ClipInfo.hpp"

namespace magda::daw {

/** @brief One channel of one track in a MIDI file, as clip events. */
struct ImportedMidiTrack {
    juce::String name;
    std::vector<MidiNote> notes;
    std::vector<MidiCCData> controllers;
    std::vector<MidiPitchBendData> pitchBends;
    std::vector<MidiProgramChangeData> programChanges;
    double endBeat = 0.0;
};

/** @brief A MIDI file's channels, its length and its first meter. */
struct ImportedMidiFile {
    std::vector<ImportedMidiTrack> tracks;
    double lengthBeats = 0.0;
    double beatsInBar = 4.0;
};

/** @brief Read @p file into one entry per track and channel that holds events. */
std::optional<ImportedMidiFile> readMidiTracks(const juce::File& file, double projectTempo);

}  // namespace magda::daw
