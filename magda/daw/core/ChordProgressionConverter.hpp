#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ClipInfo.hpp"
#include "music/ChordEnums.hpp"

namespace magda {

/// One detected chord from a bar-by-bar scan of a MIDI clip's notes.
struct ExtractedChord {
    double startBeat = 0.0;    // Clip-relative beat where the chord starts
    double lengthBeats = 0.0;  // Extends to the next chord (or the last note end)
    juce::String name;         // Display name, e.g. "C4 maj" (carries the octave)
    // The detected harmony, kept as enums so callers can rebuild a clean voicing
    // without re-parsing the (octave-bearing) display name.
    music::ChordRoot root = music::ChordRoot::C;
    music::ChordQuality quality = music::ChordQuality::Major;
    std::vector<size_t> noteIndices;  // Indices into the scanned notes vector
};

/**
 * @brief Bar-by-bar chord detection over a clip's MIDI notes.
 *
 * Mirrors the per-bar scan the piano-roll chord lane performs: at each bar it
 * gathers the notes sounding there and runs ChordEngine detection. Beats are
 * clip-relative on input and output. The single source of truth shared by the
 * in-editor "detect chords" action and the "extract to chord track" feature.
 */
std::vector<ExtractedChord> extractChordsFromNotes(const std::vector<MidiNote>& notes,
                                                   int beatsPerBar);

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
