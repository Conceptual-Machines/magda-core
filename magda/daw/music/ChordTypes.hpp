#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ChordEnums.hpp"

namespace magda::music {

struct ChordNote {
    ChordNote(int midiNote, int noteVelocity) : noteNumber(midiNote), velocity(noteVelocity) {}

    int noteNumber;
    int velocity;

    [[nodiscard]] int getMidiNoteNumber() const {
        return noteNumber;
    }

    bool operator==(const ChordNote& other) const {
        return noteNumber == other.noteNumber && velocity == other.velocity;
    }

    bool operator<(const ChordNote& other) const {
        return std::tie(noteNumber, velocity) < std::tie(other.noteNumber, other.velocity);
    }
};

struct Chord {
    ChordRoot root = ChordRoot::C;
    ChordQuality quality = ChordQuality::Major;
    int inversion = 0;
    juce::String displayName;
    juce::String name;
    std::vector<ChordNote> notes;
    int rootNoteNumber = -1;
    std::optional<int> degree;
    bool exactMatch = true;              // false when detected chord is a partial/fuzzy match
    std::vector<int> missingIntervals;   // intervals present in ideal chord but absent in input
    std::vector<int> extraPitchClasses;  // pitch classes in input but not in ideal chord

    Chord()
        : root(ChordRoot::C),
          quality(ChordQuality::Major),
          inversion(0),
          notes({}),
          rootNoteNumber(-1),
          degree(std::nullopt) {}

    Chord(juce::String chordName) {  // NOLINT(google-explicit-constructor)
        auto parsedSpec = ChordUtils::stringToChordSpec(chordName);
        root = parsedSpec.root;
        quality = parsedSpec.quality;
        inversion = parsedSpec.inversion;
        displayName = chordName.contains(" + ") ? chordName : juce::String();
        name = chordName;
        notes = {};
        rootNoteNumber = -1;
        degree = std::nullopt;
    }

    Chord(ChordRoot chordRoot, ChordQuality chordQuality, const std::vector<ChordNote>& chordNotes,
          const int rootNote = -1, std::optional<int> chordDegree = std::nullopt,
          const int chordInversion = 0)
        : root(chordRoot),
          quality(chordQuality),
          inversion(chordInversion),
          name(ChordUtils::rootToString(chordRoot) + " " +
               ChordUtils::qualityToString(chordQuality)),
          notes(chordNotes),
          rootNoteNumber(rootNote),
          degree(chordDegree) {}

    Chord(juce::String chordName, const std::vector<ChordNote>& chordNotes, const int rootNote = -1,
          std::optional<int> chordDegree = std::nullopt, const int chordInversion = 0) {
        auto parsedSpec = ChordUtils::stringToChordSpec(chordName);
        root = parsedSpec.root;
        quality = parsedSpec.quality;
        inversion = chordInversion;
        displayName = chordName.contains(" + ") ? chordName : juce::String();
        name = chordName;
        notes = chordNotes;
        rootNoteNumber = rootNote;
        degree = chordDegree;
    }

    bool operator==(const Chord& other) const {
        if (root != other.root || quality != other.quality || inversion != other.inversion)
            return false;
        if (notes.size() != other.notes.size())
            return false;
        std::vector<int> a, b;
        for (const auto& n : notes)
            a.push_back(n.noteNumber);
        for (const auto& n : other.notes)
            b.push_back(n.noteNumber);
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        return a == b;
    }

    bool operator!=(const Chord& other) const {
        return !(*this == other);
    }

    juce::String getName() const {
        if (name == "none" || name == "unknown" || name == "no chord" || name.isEmpty())
            return name;
        ChordSpec tempSpec(root, quality, inversion);
        return ChordUtils::chordSpecToString(tempSpec, false);
    }

    juce::String getDisplayName() const {
        if (displayName.isNotEmpty())
            return displayName;
        // Build name with octave after root: "D3 sus4" instead of "D sus4"
        int octave = findRootOctave();
        juce::String base;
        if (octave >= 0)
            base = ChordUtils::rootToString(root) + juce::String(octave) + " " +
                   ChordUtils::qualityToString(quality);
        else
            base = getName();

        // Append (noX) for missing intervals in partial matches
        if (!exactMatch) {
            for (int interval : missingIntervals) {
                auto degree = intervalToDegreeName(interval);
                if (degree.isNotEmpty())
                    base += " (no" + degree + ")";
            }
            if (!extraPitchClasses.empty()) {
                static const char* kNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                 "F#", "G",  "G#", "A",  "A#", "B"};
                for (int pc : extraPitchClasses)
                    base += " (+" + juce::String(kNames[pc % 12]) + ")";
            }
        }

        if (inversion <= 0)
            return base;
        const int bassPc = getBassPitchClass();
        if (bassPc >= 0)
            return base + " / " + getBassName();
        return base;
    }

    [[nodiscard]] int getBassPitchClass() const {
        if (!notes.empty())
            return (notes.front().noteNumber % 12);
        if (rootNoteNumber >= 0)
            return (rootNoteNumber % 12);
        return -1;
    }

    [[nodiscard]] juce::String getBassName() const {
        static const char* kNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                         "F#", "G",  "G#", "A",  "A#", "B"};
        int pc = getBassPitchClass();
        if (pc < 0)
            return {};
        pc = (pc % 12 + 12) % 12;
        return kNames[pc];
    }

    int getRootOctave() const {
        if (notes.empty())
            return -1;
        return notes.front().noteNumber / 12 - 2;
    }

  private:
    /** Find the octave of the root note from the played notes.
        Returns -1 if no note matches the root pitch class. */
    int findRootOctave() const {
        int rootPc = static_cast<int>(root);
        for (const auto& note : notes) {
            if (note.noteNumber % 12 == rootPc)
                return note.noteNumber / 12 - 1;  // MIDI octave convention: C4 = 60
        }
        return -1;
    }

    /** Map a semitone interval to its degree name for (noX) notation. */
    static juce::String intervalToDegreeName(int semitones) {
        switch (semitones) {
            case 2:
                return "2";
            case 3:
                return "3";
            case 4:
                return "3";
            case 5:
                return "4";
            case 7:
                return "5";
            case 9:
                return "6";
            case 10:
                return "7";
            case 11:
                return "7";
            case 14:
                return "9";
            case 17:
                return "11";
            case 21:
                return "13";
            default:
                return {};
        }
    }
};

struct ChordProgression {
    juce::String description;
    std::vector<Chord> chords;

    ChordProgression() = default;
};

}  // namespace magda::music
