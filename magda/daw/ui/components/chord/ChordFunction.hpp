// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <optional>
#include <vector>

#include "music/ChordEnums.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui::chord_function {

/// What a chord does in its key: rest, move away, or lead home.
enum class HarmonicFunction { None, Tonic, PreDominant, Dominant };

/// A key as a root pitch class and whether its third is minor.
struct Key {
    int root = 0;
    bool minor = false;
};

/** @brief The function of a roman-numeral degree such as "IV7", "vii°", "V/x" or "♭VII". */
inline HarmonicFunction functionOfDegree(juce::String degree) {
    degree = degree.trim();
    if (degree.startsWithIgnoreCase("tritone") || degree.startsWith("V/"))
        return HarmonicFunction::Dominant;

    const auto flat = juce::String(juce::CharPointer_UTF8("\xe2\x99\xad"));
    while (degree.startsWithChar('b') || degree.startsWithChar('#') || degree.startsWith(flat))
        degree = degree.substring(degree.startsWith(flat) ? flat.length() : 1);

    juce::String numeral;
    for (const auto c : degree) {
        if (juce::String("IViv").containsChar(c))
            numeral += c;
        else
            break;
    }
    numeral = numeral.toUpperCase();
    if (numeral == "I" || numeral == "III" || numeral == "VI")
        return HarmonicFunction::Tonic;
    if (numeral == "II" || numeral == "IV")
        return HarmonicFunction::PreDominant;
    if (numeral == "V" || numeral == "VII")
        return HarmonicFunction::Dominant;
    return HarmonicFunction::None;
}

inline juce::Colour functionColour(HarmonicFunction function) {
    switch (function) {
        case HarmonicFunction::Tonic:
            return ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
        case HarmonicFunction::PreDominant:
            return ActiveTheme::getColour(ActiveTheme::MIDI_KEY_ON);
        case HarmonicFunction::Dominant:
            return ActiveTheme::getColour(ActiveTheme::DEVICE_AMBER);
        case HarmonicFunction::None:
            break;
    }
    return ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2);
}

inline bool hasMinorThird(magda::music::ChordQuality quality) {
    using Q = magda::music::ChordQuality;
    switch (quality) {
        case Q::Minor:
        case Q::Minor7:
        case Q::Minor9:
        case Q::Minor11:
        case Q::Minor13:
        case Q::MinorAdd9:
        case Q::MinorAdd2:
        case Q::MinorAdd4:
        case Q::Minor7Add2:
        case Q::Minor7Add6:
        case Q::Minor7Add2Add6:
        case Q::Minor7Add4:
        case Q::Minor7Add2Add4:
        case Q::Minor7Add4Add6:
        case Q::Minor7Add2Add4Add6:
        case Q::Diminished:
        case Q::Diminished7:
        case Q::Diminished9:
            return true;
        default:
            return false;
    }
}

/**
 * @brief The roman numeral of a chord rooted at @p root in @p key.
 *
 * A root between two scale degrees is the upper one flattened (♭VII, ♭III). Lower case for a
 * minor third, ° for diminished.
 */
inline juce::String romanNumeral(magda::music::ChordRoot root, magda::music::ChordQuality quality,
                                 Key key) {
    static constexpr std::array<int, 7> kMajor{0, 2, 4, 5, 7, 9, 11};
    static constexpr std::array<int, 7> kMinor{0, 2, 3, 5, 7, 8, 10};
    static constexpr std::array<const char*, 7> kNumerals{"I", "II", "III", "IV", "V", "VI", "VII"};
    const auto& scale = key.minor ? kMinor : kMajor;

    const int interval = (static_cast<int>(root) - key.root + 12) % 12;
    juce::String numeral;
    for (size_t i = 0; i < scale.size() && numeral.isEmpty(); ++i)
        if (scale[i] == interval)
            numeral = kNumerals[i];
    for (size_t i = 0; i < scale.size() && numeral.isEmpty(); ++i)
        if (scale[i] == interval + 1)
            numeral = juce::String(juce::CharPointer_UTF8("\xe2\x99\xad")) + kNumerals[i];
    if (numeral.isEmpty())
        return {};

    if (hasMinorThird(quality))
        numeral = numeral.toLowerCase();
    using Q = magda::music::ChordQuality;
    if (quality == Q::Diminished || quality == Q::Diminished7 || quality == Q::Diminished9)
        numeral += juce::String(juce::CharPointer_UTF8("\xc2\xb0"));
    return numeral;
}

/** @brief A key as the chord engine names it ("C", "major"), or nothing for an unknown root. */
inline std::optional<Key> parseKey(const juce::String& root, const juce::String& mode) {
    static const juce::StringArray kSharps{"C",  "C#", "D",  "D#", "E",  "F",
                                           "F#", "G",  "G#", "A",  "A#", "B"};
    static const juce::StringArray kFlats{"C",  "Db", "D",  "Eb", "E",  "F",
                                          "Gb", "G",  "Ab", "A",  "Bb", "B"};
    int pc = kSharps.indexOf(root.trim());
    if (pc < 0)
        pc = kFlats.indexOf(root.trim());
    if (pc < 0)
        return std::nullopt;
    const auto m = mode.toLowerCase();
    const bool minor = m.contains("minor") || m.contains("aeolian") || m.contains("dorian") ||
                       m.contains("phrygian") || m.contains("locrian");
    return Key{pc, minor};
}

/**
 * @brief The key that explains most of @p chords without accidentals.
 *
 * Every major and minor key is scored by how many chords sit in it as plain diatonic triads;
 * ties go to the key whose tonic opens the progression.
 */
inline std::optional<Key> estimateKey(
    const std::vector<std::pair<magda::music::ChordRoot, magda::music::ChordQuality>>& chords) {
    if (chords.empty())
        return std::nullopt;

    std::optional<Key> best;
    int bestScore = -1;
    for (int root = 0; root < 12; ++root) {
        for (const bool minor : {false, true}) {
            const Key key{root, minor};
            int score = 0;
            for (const auto& [chordRoot, quality] : chords) {
                const auto numeral = romanNumeral(chordRoot, quality, key);
                if (numeral.isNotEmpty() &&
                    !numeral.startsWith(juce::CharPointer_UTF8("\xe2\x99\xad")))
                    score += 2;
            }
            const auto& first = chords.front();
            if (static_cast<int>(first.first) == root && hasMinorThird(first.second) == minor)
                score += 1;
            if (score > bestScore) {
                bestScore = score;
                best = key;
            }
        }
    }
    return best;
}

}  // namespace magda::daw::ui::chord_function
