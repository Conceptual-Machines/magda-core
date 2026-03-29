#pragma once

#include <string>
#include <vector>

#include "ChordTypes.hpp"

namespace magda::music {

struct Scale {
    std::string name;
    std::vector<int> notes;  // pitches in 0-11 range
};

inline const std::vector<Scale> scaleIntervals = {
    {"Ionian", {0, 2, 4, 5, 7, 9, 11}},
    {"Dorian", {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian", {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian", {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian", {0, 2, 4, 5, 7, 9, 10}},
    {"Aeolian", {0, 2, 3, 5, 7, 8, 10}},
    {"Locrian", {0, 1, 3, 5, 6, 8, 10}},
    {"Major Pentatonic", {0, 2, 4, 7, 9}},
    {"Minor Pentatonic", {0, 3, 5, 7, 10}},
    {"Egyptian", {0, 2, 5, 7, 10}},
    {"Whole Tone", {0, 2, 4, 6, 8, 10}},
    {"Blues Hexatonic", {0, 3, 5, 6, 7, 10}},
    {"Hirajoshi", {0, 2, 3, 7, 8}},
    {"In-Sen", {0, 1, 5, 7, 10}},
    {"Pelog", {0, 1, 3, 7, 8}},
    {"Hungarian Gypsy", {0, 2, 3, 6, 7, 8, 11}},
    {"Persian", {0, 1, 4, 5, 6, 8, 11}},
};

inline std::string getNoteName(const int root) {
    static const std::vector<std::string> noteNames = {"C",  "C#", "D",  "D#", "E",  "F",
                                                       "F#", "G",  "G#", "A",  "A#", "B"};
    const int index = root % 12;
    if (index >= 0 && index < static_cast<int>(noteNames.size()))
        return noteNames[static_cast<std::size_t>(index)];
    return "C";
}

struct ScaleWithChords {
    std::string name;
    int rootNote;
    int baseOctave = 3;
    std::vector<int> pitches;
    std::vector<Chord> chords;

    int rootMidiNote() const {
        return (rootNote % 12) + baseOctave * 12;
    }

    [[nodiscard]] std::vector<int> getPitches() const {
        std::vector<int> transposed;
        transposed.reserve(pitches.size());
        for (const int interval : pitches)
            transposed.push_back((interval + rootNote) % 12);
        return transposed;
    }
};

inline std::string formatScaleName(const std::string& root, const std::string& mode) {
    if (mode == "Ionian")
        return root + " Major (Ionian)";
    if (mode == "Aeolian")
        return root + " Minor (Aeolian)";
    return root + " " + mode;
}

inline std::vector<Chord> buildTriadsFromScale(const std::vector<int>& scalePitches,
                                               int baseOctave = 3) {
    std::vector<Chord> chords;
    const size_t scaleSize = scalePitches.size();
    if (scaleSize < 3)
        return chords;

    for (size_t i = 0; i < scaleSize; ++i) {
        int rootPc = scalePitches[i];
        int thirdPc = scalePitches[(i + 2) % scaleSize];
        int fifthPc = scalePitches[(i + 4) % scaleSize];

        int rootMidi = rootPc + baseOctave * 12;
        int thirdMidi = thirdPc + baseOctave * 12;
        int fifthMidi = fifthPc + baseOctave * 12;

        if (thirdMidi < rootMidi)
            thirdMidi += 12;
        if (fifthMidi < rootMidi)
            fifthMidi += 12;

        std::vector<ChordNote> chordNotes = {
            {rootMidi, 100},
            {thirdMidi, 100},
            {fifthMidi, 100},
        };

        // Determine quality from intervals
        int thirdInterval = (thirdPc - rootPc + 12) % 12;
        int fifthInterval = (fifthPc - rootPc + 12) % 12;

        ChordQuality qual = ChordQuality::Major;
        if (thirdInterval == 3 && fifthInterval == 7)
            qual = ChordQuality::Minor;
        else if (thirdInterval == 3 && fifthInterval == 6)
            qual = ChordQuality::Diminished;
        else if (thirdInterval == 4 && fifthInterval == 8)
            qual = ChordQuality::Augmented;

        auto rootEnum = static_cast<ChordRoot>(rootPc % 12);
        Chord chord(rootEnum, qual, chordNotes, rootMidi, static_cast<int>(i + 1), 0);
        chords.push_back(chord);
    }

    return chords;
}

inline std::vector<ScaleWithChords> generateAllScalesWithChords(
    const std::vector<Scale>& baseScales) {
    std::vector<ScaleWithChords> all;

    for (const auto& scale : baseScales) {
        for (int root = 0; root < 12; ++root) {
            std::vector<int> pitches;
            pitches.reserve(scale.notes.size());
            for (int interval : scale.notes)
                pitches.push_back((interval + root) % 12);

            const auto noteName = getNoteName(root);
            const std::string scaleName = formatScaleName(noteName, scale.name);
            const auto chords = buildTriadsFromScale(pitches, 3);
            all.push_back({scaleName, root, 3, pitches, chords});
        }
    }

    return all;
}

inline const std::vector<ScaleWithChords>& getAllScalesWithChordsCached() {
    static const std::vector<ScaleWithChords> kAllScalesWithChords =
        generateAllScalesWithChords(scaleIntervals);
    return kAllScalesWithChords;
}

}  // namespace magda::music
