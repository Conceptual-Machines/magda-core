#pragma once

#include <juce_core/juce_core.h>

#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace magda {

/**
 * @brief Shared music helpers used by both the DSL interpreter and compact executor.
 */
namespace music {

/** Parse note name (e.g. "C4", "C#4", "Bb3") or MIDI number to MIDI note number. Returns -1 on
 * error. */
inline int parseNoteName(const std::string& name) {
    if (name.empty())
        return -1;

    // Plain number → return directly
    bool allDigits = true;
    size_t start = (name[0] == '-') ? 1 : 0;
    for (size_t i = start; i < name.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits && !name.empty())
        return std::atoi(name.c_str());

    // Parse note letter
    static const int noteOffsets[] = {
        9, 11, 0, 2, 4, 5, 7  // A=9, B=11, C=0, D=2, E=4, F=5, G=7
    };

    char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    if (letter < 'A' || letter > 'G')
        return -1;

    int semitone = noteOffsets[letter - 'A'];
    size_t pos = 1;

    if (pos < name.size() && name[pos] == '#') {
        semitone++;
        pos++;
    } else if (pos < name.size() && (name[pos] == 'b' || name[pos] == 'B')) {
        semitone--;
        pos++;
    }

    // Parse octave number
    if (pos >= name.size())
        return -1;

    bool negative = false;
    if (name[pos] == '-') {
        negative = true;
        pos++;
    }
    if (pos >= name.size())
        return -1;

    int octave = 0;
    while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) {
        octave = octave * 10 + (name[pos] - '0');
        pos++;
    }
    if (negative)
        octave = -octave;

    return (octave + 1) * 12 + semitone;
}

/** Chord quality → semitone intervals from root. */
inline const std::map<std::string, std::vector<int>>& chordQualities() {
    static const std::map<std::string, std::vector<int>> q = {
        {"major", {0, 4, 7}},
        {"maj", {0, 4, 7}},
        {"minor", {0, 3, 7}},
        {"min", {0, 3, 7}},
        {"dim", {0, 3, 6}},
        {"aug", {0, 4, 8}},
        {"sus2", {0, 2, 7}},
        {"sus4", {0, 5, 7}},
        {"dom7", {0, 4, 7, 10}},
        {"7", {0, 4, 7, 10}},
        {"maj7", {0, 4, 7, 11}},
        {"min7", {0, 3, 7, 10}},
        {"dim7", {0, 3, 6, 9}},
        {"dom9", {0, 4, 7, 10, 14}},
        {"9", {0, 4, 7, 10, 14}},
        {"maj9", {0, 4, 7, 11, 14}},
        {"min9", {0, 3, 7, 10, 14}},
        {"dom11", {0, 4, 7, 10, 14, 17}},
        {"11", {0, 4, 7, 10, 14, 17}},
        {"min11", {0, 3, 7, 10, 14, 17}},
        {"maj11", {0, 4, 7, 11, 14, 17}},
        {"dom13", {0, 4, 7, 10, 14, 21}},
        {"13", {0, 4, 7, 10, 14, 21}},
        {"min13", {0, 3, 7, 10, 14, 21}},
        {"maj13", {0, 4, 7, 11, 14, 21}},
        {"add9", {0, 4, 7, 14}},
        {"add11", {0, 4, 7, 17}},
        {"add13", {0, 4, 7, 21}},
        {"madd9", {0, 3, 7, 14}},
        {"6", {0, 4, 7, 9}},
        {"maj6", {0, 4, 7, 9}},
        {"min6", {0, 3, 7, 9}},
        {"7b5", {0, 4, 6, 10}},
        {"7sharp5", {0, 4, 8, 10}},
        {"7b9", {0, 4, 7, 10, 13}},
        {"7sharp9", {0, 4, 7, 10, 15}},
        {"min7b5", {0, 3, 6, 10}},
        {"half_dim", {0, 3, 6, 10}},
        {"power", {0, 7}},
        {"5", {0, 7}},
    };
    return q;
}

/**
 * @brief Resolve chord notes from root, quality, and inversion.
 * @param root      Note name or MIDI number string
 * @param quality   Chord quality string (e.g. "major", "min7")
 * @param inversion 0=root, 1=first, 2=second
 * @param outNotes  Output MIDI note numbers
 * @param error     Output error string on failure
 * @return true on success
 */
inline bool resolveChordNotes(const std::string& root, const std::string& quality, int inversion,
                              std::vector<int>& outNotes, juce::String& error) {
    int rootNote = parseNoteName(root);
    if (rootNote < 0 || rootNote > 127) {
        error = "Invalid root note: " + juce::String(root);
        return false;
    }

    auto it = chordQualities().find(quality);
    if (it == chordQualities().end()) {
        error = "Unknown chord quality: " + juce::String(quality);
        return false;
    }

    const auto& intervals = it->second;
    outNotes.clear();
    for (int interval : intervals)
        outNotes.push_back(rootNote + interval);

    for (int i = 0; i < inversion && i < static_cast<int>(outNotes.size()); i++)
        outNotes[static_cast<size_t>(i)] += 12;

    for (auto& n : outNotes)
        n = juce::jlimit(0, 127, n);

    return true;
}

}  // namespace music
}  // namespace magda
