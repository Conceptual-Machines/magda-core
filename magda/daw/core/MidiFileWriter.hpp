// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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
    /// Resolution of every .mid file MAGDA writes.
    static constexpr int ticksPerQuarter = 960;

    /**
     * @brief Adds one clip's notes, CC and pitch bend to @p seq, cut to an export range.
     *
     * @p clipStartBeats is the clip's start relative to the range start and may be negative.
     * Ticks run from the range start. A note crossing either edge is shortened to it, and one
     * wholly outside is dropped, so no note-off lands before its note-on (#3279). CC and pitch
     * bend from before the range land on its first tick.
     * @return The latest tick added, or 0 when nothing fell inside the range.
     */
    static double appendClipInRange(juce::MidiMessageSequence& seq,
                                    const std::vector<MidiNote>& notes,
                                    const std::vector<MidiCCData>& ccData,
                                    const std::vector<MidiPitchBendData>& pitchBendData,
                                    double clipStartBeats, double rangeLengthBeats, int channel);

    /**
     * Write clip MIDI data to a specific .mid file (Type 0, single track).
     */
    static bool writeToFile(const juce::File& outFile, const std::vector<MidiNote>& notes,
                            const std::vector<MidiCCData>& ccData,
                            const std::vector<MidiPitchBendData>& pitchBendData, double tempo,
                            const juce::String& nameHint = "pattern",
                            const std::vector<ChordMarker>& chordMarkers = {});

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
