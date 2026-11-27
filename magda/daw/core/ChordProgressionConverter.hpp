// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ClipInfo.hpp"
#include "music/ChordEnums.hpp"

namespace magda {

/// One chord detected in a MIDI clip's notes.
struct ExtractedChord {
    double startBeat = 0.0;    // Clip-relative beat where the chord starts
    double lengthBeats = 0.0;  // Extends to the next chord (or the last note end)
    juce::String name;         // Display name, e.g. "C4 maj" (carries the octave)
    // The detected harmony, kept as enums so callers can rebuild a clean voicing
    // without re-parsing the (octave-bearing) display name.
    music::ChordRoot root = music::ChordRoot::C;
    music::ChordQuality quality = music::ChordQuality::Major;
    bool exactMatch = true;
    double confidence = 1.0;
    std::vector<juce::String> warnings;
    std::vector<size_t> noteIndices;  // Indices into the scanned notes vector
};

/**
 * @brief Detects the chords played in a clip's notes, each starting at its first note.
 *
 * A chord is two or more notes struck within kChordOnsetSpreadBeats, plus any still held
 * past that spread. A chord struck again extends the one before it. Beats are clip-relative.
 */
std::vector<ExtractedChord> extractChordsFromNotes(const std::vector<MidiNote>& notes);

/// Reads one chord per window, placed at the window's start; the remote API's grid scan.
std::vector<ExtractedChord> extractChordsFromNotes(const std::vector<MidiNote>& notes,
                                                   double startBeat, double endBeat,
                                                   double windowBeats);

/// How far into each window its chord is read: a 16th, capped at half the window.
inline constexpr double kChordReadDelayBeats = 0.25;

/// How far apart, at most, the notes of one played chord start: a 16th.
inline constexpr double kChordOnsetSpreadBeats = 0.25;

/**
 * @brief Detects @p clip's chords and links each one's notes to its annotation.
 *
 * What a MIDI clip landing on the chord track goes through, dropped or recorded.
 */
void annotateChordsFromNotes(ClipInfo& clip);

/**
 * @brief Build a canonical root-position voicing for a chord.
 *
 * Emits the ideal voicing for the given root/quality as MIDI notes at the given
 * clip-relative position. chordGroup is left at 0 so the caller can link the
 * notes to a ChordAnnotation. Takes enums (not a name string) so it never has to
 * parse an octave-bearing display name like "G4 major".
 */
std::vector<MidiNote> buildVoicingNotes(music::ChordRoot root, music::ChordQuality quality,
                                        double startBeat, double lengthBeats, int velocity = 100,
                                        int octave = 4);

}  // namespace magda
