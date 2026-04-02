#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

#include "ClipInfo.hpp"

namespace magda::daw {

/**
 * @brief Writes MidiNote data to a temporary .mid file for drag-and-drop export.
 */
/**
 * Chord marker embedded as a MIDI marker meta event in exported files.
 * Used to pre-populate chord annotations when the file is imported.
 */
struct ChordMarker {
    double beatPosition = 0.0;
    double lengthBeats = 4.0;
    juce::String chordName;
};

struct MidiFileWriter {
    /**
     * Write notes to a temporary .mid file (Type 0, single track).
     * @param notes     Vector of MidiNote (startBeat, lengthBeats, noteNumber, velocity)
     * @param tempo     BPM for the tempo meta event
     * @param nameHint  Prefix for the temp file name
     * @param chordMarkers  Optional chord markers to embed as MIDI marker events
     * @return          The temp file, or an invalid File on failure
     */
    static juce::File writeToTempFile(const std::vector<MidiNote>& notes, double tempo,
                                      const juce::String& nameHint = "pattern",
                                      const std::vector<ChordMarker>& chordMarkers = {});
};

}  // namespace magda::daw
