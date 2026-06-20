#include "ChordProgressionConverter.hpp"

#include <algorithm>

#include "music/ChordEngine.hpp"

namespace magda {

std::vector<ExtractedChord> extractChordsFromNotes(const std::vector<MidiNote>& notes,
                                                   int beatsPerBar) {
    std::vector<ExtractedChord> detected;
    if (notes.empty())
        return detected;

    const double step = std::max(1, beatsPerBar);

    // Scan only up to the last note end, not the full clip length.
    double lastNoteEnd = 0.0;
    for (const auto& note : notes)
        lastNoteEnd = std::max(lastNoteEnd, note.startBeat + note.lengthBeats);

    auto& engine = magda::music::ChordEngine::getInstance();
    for (double beat = 0.0; beat < lastNoteEnd; beat += step) {
        std::vector<magda::music::ChordNote> chordNotes;
        std::vector<size_t> indices;
        for (size_t i = 0; i < notes.size(); ++i) {
            const auto& note = notes[i];
            if (note.startBeat <= beat && (note.startBeat + note.lengthBeats) > beat) {
                chordNotes.push_back({note.noteNumber, note.velocity});
                indices.push_back(i);
            }
        }

        if (chordNotes.size() < 2)
            continue;

        auto chord = engine.detect(chordNotes);
        if (chord.name == "none" || chord.name == "unknown" || chord.name.isEmpty())
            continue;

        ExtractedChord ex;
        ex.startBeat = beat;
        ex.name = chord.getDisplayName();
        ex.root = chord.root;
        ex.quality = chord.quality;
        ex.noteIndices = std::move(indices);
        detected.push_back(std::move(ex));
    }

    // Each chord extends to the next one, or to the last note end for the tail.
    for (size_t i = 0; i < detected.size(); ++i) {
        detected[i].lengthBeats = (i + 1 < detected.size())
                                      ? (detected[i + 1].startBeat - detected[i].startBeat)
                                      : (lastNoteEnd - detected[i].startBeat);
    }

    return detected;
}

std::vector<MidiNote> buildVoicingNotes(music::ChordRoot root, music::ChordQuality quality,
                                        double startBeat, double lengthBeats, int velocity,
                                        int octave) {
    std::vector<MidiNote> out;
    const auto chord =
        magda::music::ChordEngine::getInstance().buildChordInRootPosition(root, quality, octave);
    out.reserve(chord.notes.size());
    for (const auto& n : chord.notes) {
        MidiNote m;
        m.noteNumber = std::clamp(n.noteNumber, 0, 127);
        m.velocity = velocity;
        m.startBeat = startBeat;
        m.lengthBeats = lengthBeats;
        m.chordGroup = 0;
        out.push_back(m);
    }
    return out;
}

}  // namespace magda
