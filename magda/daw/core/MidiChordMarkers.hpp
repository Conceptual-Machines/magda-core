#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

#include "MidiFileWriter.hpp"

namespace magda::daw {

// Decode CHORD: marker meta-events back into chord markers (beats). This is
// the reverse of MidiFileWriter's chord-marker writing: each marker is a text
// meta event (type 6) of the form "CHORD:name:lengthBeats", where the last
// colon-delimited token is the length and everything between the first and
// last colon is the chord name (names may themselves contain colons, e.g.
// "C:maj7"). Markers in SMPTE-timed files are skipped (we only handle
// ticks-per-quarter-note timing, which is what the writer emits).
std::vector<ChordMarker> readChordMarkers(const juce::MidiFile& midi);

// Convenience overload: open and parse a .mid file. Returns an empty vector
// if the file cannot be read.
std::vector<ChordMarker> readChordMarkers(const juce::File& midiFile);

}  // namespace magda::daw
