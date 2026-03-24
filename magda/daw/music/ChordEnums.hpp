#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <vector>

namespace magda::music {

enum class ChordRoot : uint8_t {
    C = 0,
    CSharp = 1,
    D = 2,
    DSharp = 3,
    E = 4,
    F = 5,
    FSharp = 6,
    G = 7,
    GSharp = 8,
    A = 9,
    ASharp = 10,
    B = 11
};

enum class ChordQuality : uint8_t {
    Major = 0,
    Minor = 1,
    Diminished = 2,
    Augmented = 3,
    Power = 4,
    Sus2 = 5,
    Sus4 = 6,
    Major7 = 7,
    Minor7 = 8,
    Dominant7 = 9,
    Diminished7 = 10,
    Dominant9 = 11,
    Major9 = 12,
    Minor9 = 13,
    Dominant11 = 14,
    Minor11 = 15,
    Dominant13 = 16,
    Minor13 = 17,
    MajorAdd9 = 18,
    MinorAdd9 = 19,
    MajorAdd6 = 20,
    Major7Add13 = 21,
    Sus4Add6 = 22,
    Diminished9 = 23,
    Sus2Add6 = 24,
    Minor7Add2 = 25,
    Minor7Add6 = 26,
    Minor7Add2Add6 = 27,
    Minor7Add4 = 28,
    Minor7Add2Add4 = 29,
    Minor7Add4Add6 = 30,
    Minor7Add2Add4Add6 = 31,
    MajorAdd2 = 32,
    MajorAdd4 = 33,
    MinorAdd2 = 34,
    MinorAdd4 = 35
};

enum class ChordInversion : uint8_t {
    RootPosition = 0,
    FirstInversion = 1,
    SecondInversion = 2,
    ThirdInversion = 3
};

struct ChordSpec {
    ChordRoot root;
    ChordQuality quality;
    int inversion;

    ChordSpec(ChordRoot r, ChordQuality q, int inv = 0) : root(r), quality(q), inversion(inv) {}

    bool operator==(const ChordSpec& other) const {
        return root == other.root && quality == other.quality && inversion == other.inversion;
    }

    bool operator!=(const ChordSpec& other) const {
        return !(*this == other);
    }
};

namespace ChordUtils {

inline juce::String rootToString(ChordRoot root) {
    static const std::map<ChordRoot, juce::String> rootNames = {
        {ChordRoot::C, "C"},       {ChordRoot::CSharp, "C#"}, {ChordRoot::D, "D"},
        {ChordRoot::DSharp, "D#"}, {ChordRoot::E, "E"},       {ChordRoot::F, "F"},
        {ChordRoot::FSharp, "F#"}, {ChordRoot::G, "G"},       {ChordRoot::GSharp, "G#"},
        {ChordRoot::A, "A"},       {ChordRoot::ASharp, "A#"}, {ChordRoot::B, "B"}};
    auto it = rootNames.find(root);
    return it != rootNames.end() ? it->second : "C";
}

inline ChordRoot stringToRoot(const juce::String& noteName) {
    static const std::map<juce::String, ChordRoot> rootMap = {
        {"C", ChordRoot::C},       {"C#", ChordRoot::CSharp}, {"D", ChordRoot::D},
        {"D#", ChordRoot::DSharp}, {"E", ChordRoot::E},       {"F", ChordRoot::F},
        {"F#", ChordRoot::FSharp}, {"G", ChordRoot::G},       {"G#", ChordRoot::GSharp},
        {"A", ChordRoot::A},       {"A#", ChordRoot::ASharp}, {"B", ChordRoot::B}};
    auto it = rootMap.find(noteName);
    return it != rootMap.end() ? it->second : ChordRoot::C;
}

inline juce::String qualityToString(ChordQuality quality) {
    static const std::map<ChordQuality, juce::String> qualityNames = {
        {ChordQuality::Major, "maj"},
        {ChordQuality::Minor, "min"},
        {ChordQuality::Diminished, "dim"},
        {ChordQuality::Augmented, "aug"},
        {ChordQuality::Power, "5"},
        {ChordQuality::Sus2, "sus2"},
        {ChordQuality::Sus4, "sus4"},
        {ChordQuality::Major7, "maj7"},
        {ChordQuality::Minor7, "min7"},
        {ChordQuality::Dominant7, "7"},
        {ChordQuality::Diminished7, "dim7"},
        {ChordQuality::Dominant9, "9"},
        {ChordQuality::Major9, "maj9"},
        {ChordQuality::Minor9, "min9"},
        {ChordQuality::Diminished9, "dim9"},
        {ChordQuality::Dominant11, "11"},
        {ChordQuality::Minor11, "min11"},
        {ChordQuality::Dominant13, "13"},
        {ChordQuality::Minor13, "min13"},
        {ChordQuality::MajorAdd9, "maj add9"},
        {ChordQuality::MinorAdd9, "min add9"},
        {ChordQuality::MajorAdd6, "maj add6"},
        {ChordQuality::Major7Add13, "maj7 add13"},
        {ChordQuality::Sus4Add6, "sus4 add6"},
        {ChordQuality::Sus2Add6, "sus2 add6"},
        {ChordQuality::Minor7Add2, "min7 add2"},
        {ChordQuality::Minor7Add6, "min7 add6"},
        {ChordQuality::Minor7Add2Add6, "min7 add2 add6"},
        {ChordQuality::Minor7Add4, "min7 add4"},
        {ChordQuality::Minor7Add2Add4, "min7 add2 add4"},
        {ChordQuality::Minor7Add4Add6, "min7 add4 add6"},
        {ChordQuality::Minor7Add2Add4Add6, "min7 add2 add4 add6"},
        {ChordQuality::MajorAdd2, "maj add2"},
        {ChordQuality::MajorAdd4, "maj add4"},
        {ChordQuality::MinorAdd2, "min add2"},
        {ChordQuality::MinorAdd4, "min add4"}};
    auto it = qualityNames.find(quality);
    return it != qualityNames.end() ? it->second : "maj";
}

inline ChordQuality stringToQuality(const juce::String& qualityStr) {
    static const std::map<juce::String, ChordQuality> qualityMap = {
        {"maj", ChordQuality::Major},
        {"major", ChordQuality::Major},
        {"", ChordQuality::Major},
        {"min", ChordQuality::Minor},
        {"minor", ChordQuality::Minor},
        {"m", ChordQuality::Minor},
        {"dim", ChordQuality::Diminished},
        {"diminished", ChordQuality::Diminished},
        {"aug", ChordQuality::Augmented},
        {"augmented", ChordQuality::Augmented},
        {"5", ChordQuality::Power},
        {"power", ChordQuality::Power},
        {"sus2", ChordQuality::Sus2},
        {"sus4", ChordQuality::Sus4},
        {"maj7", ChordQuality::Major7},
        {"major7", ChordQuality::Major7},
        {"min7", ChordQuality::Minor7},
        {"minor7", ChordQuality::Minor7},
        {"7", ChordQuality::Dominant7},
        {"dominant7", ChordQuality::Dominant7},
        {"dim7", ChordQuality::Diminished7},
        {"diminished7", ChordQuality::Diminished7},
        {"9", ChordQuality::Dominant9},
        {"maj9", ChordQuality::Major9},
        {"major9", ChordQuality::Major9},
        {"min9", ChordQuality::Minor9},
        {"minor9", ChordQuality::Minor9},
        {"11", ChordQuality::Dominant11},
        {"min11", ChordQuality::Minor11},
        {"minor11", ChordQuality::Minor11},
        {"13", ChordQuality::Dominant13},
        {"min13", ChordQuality::Minor13},
        {"minor13", ChordQuality::Minor13},
        {"maj add9", ChordQuality::MajorAdd9},
        {"major add9", ChordQuality::MajorAdd9},
        {"min add9", ChordQuality::MinorAdd9},
        {"minor add9", ChordQuality::MinorAdd9},
        {"maj add6", ChordQuality::MajorAdd6},
        {"major add6", ChordQuality::MajorAdd6},
        {"maj7 add13", ChordQuality::Major7Add13},
        {"major7 add13", ChordQuality::Major7Add13},
        {"sus4 add6", ChordQuality::Sus4Add6},
        {"sus2 add6", ChordQuality::Sus2Add6},
        {"min7 add2", ChordQuality::Minor7Add2},
        {"min7 add6", ChordQuality::Minor7Add6},
        {"min7 add2 add6", ChordQuality::Minor7Add2Add6},
        {"min7 add4", ChordQuality::Minor7Add4},
        {"min7 add2 add4", ChordQuality::Minor7Add2Add4},
        {"min7 add4 add6", ChordQuality::Minor7Add4Add6},
        {"min7 add2 add4 add6", ChordQuality::Minor7Add2Add4Add6},
        {"maj add2", ChordQuality::MajorAdd2},
        {"maj add4", ChordQuality::MajorAdd4},
        {"min add2", ChordQuality::MinorAdd2},
        {"min add4", ChordQuality::MinorAdd4}};

    auto it = qualityMap.find(qualityStr.toLowerCase());
    return (it != qualityMap.end()) ? it->second : ChordQuality::Major;
}

inline juce::String chordSpecToString(const ChordSpec& spec, bool includeInversion = true) {
    juce::String result = rootToString(spec.root) + " " + qualityToString(spec.quality);
    if (includeInversion && spec.inversion > 0) {
        result += " (inv " + juce::String(spec.inversion) + ")";
    }
    return result;
}

inline ChordSpec stringToChordSpec(const juce::String& chordString) {
    juce::String cleanStr = chordString.trim();
    int colonPos = cleanStr.indexOf(":");
    int spacePos = cleanStr.indexOf(" ");

    juce::String rootStr, qualityStr;
    if (colonPos >= 0) {
        rootStr = cleanStr.substring(0, colonPos).trim();
        qualityStr = cleanStr.substring(colonPos + 1).trim();
    } else if (spacePos >= 0) {
        rootStr = cleanStr.substring(0, spacePos).trim();
        qualityStr = cleanStr.substring(spacePos + 1).trim();
    } else {
        rootStr = cleanStr;
        qualityStr = "maj";
    }

    return ChordSpec(stringToRoot(rootStr), stringToQuality(qualityStr));
}

inline std::vector<int> getChordIntervals(ChordQuality quality) {
    static const std::map<ChordQuality, std::vector<int>> intervalMap = {
        {ChordQuality::Major, {0, 4, 7}},
        {ChordQuality::Minor, {0, 3, 7}},
        {ChordQuality::Diminished, {0, 3, 6}},
        {ChordQuality::Augmented, {0, 4, 8}},
        {ChordQuality::Major7, {0, 4, 7, 11}},
        {ChordQuality::Minor7, {0, 3, 7, 10}},
        {ChordQuality::Dominant7, {0, 4, 7, 10}},
        {ChordQuality::Diminished7, {0, 3, 6, 9}},
        {ChordQuality::Major9, {0, 4, 7, 11, 14}},
        {ChordQuality::Minor9, {0, 3, 7, 10, 14}},
        {ChordQuality::Dominant9, {0, 4, 7, 10, 14}},
        {ChordQuality::Diminished9, {0, 3, 6, 9, 14}},
        {ChordQuality::Sus2, {0, 2, 7}},
        {ChordQuality::Sus4, {0, 5, 7}},
        {ChordQuality::Sus2Add6, {0, 2, 7, 9}},
        {ChordQuality::Sus4Add6, {0, 5, 7, 9}},
        {ChordQuality::Power, {0, 7}},
        {ChordQuality::Dominant11, {0, 4, 7, 10, 14, 17}},
        {ChordQuality::Dominant13, {0, 4, 7, 10, 14, 17, 21}},
        {ChordQuality::Minor11, {0, 3, 7, 10, 14, 17}},
        {ChordQuality::Minor13, {0, 3, 7, 10, 14, 17, 21}},
        {ChordQuality::MajorAdd9, {0, 4, 7, 14}},
        {ChordQuality::MinorAdd9, {0, 3, 7, 14}},
        {ChordQuality::MajorAdd6, {0, 4, 7, 9}},
        {ChordQuality::Major7Add13, {0, 4, 7, 11, 9}},
        {ChordQuality::Minor7Add2, {0, 2, 3, 7, 10}},
        {ChordQuality::Minor7Add6, {0, 3, 7, 10, 9}},
        {ChordQuality::Minor7Add2Add6, {0, 2, 3, 7, 10, 9}},
        {ChordQuality::Minor7Add4, {0, 3, 5, 7, 10}},
        {ChordQuality::Minor7Add2Add4, {0, 2, 3, 5, 7, 10}},
        {ChordQuality::Minor7Add4Add6, {0, 3, 5, 7, 10, 9}},
        {ChordQuality::Minor7Add2Add4Add6, {0, 2, 3, 5, 7, 10, 9}},
        {ChordQuality::MajorAdd2, {0, 2, 4, 7}},
        {ChordQuality::MajorAdd4, {0, 4, 5, 7}},
        {ChordQuality::MinorAdd2, {0, 2, 3, 7}},
        {ChordQuality::MinorAdd4, {0, 3, 5, 7}}};

    auto it = intervalMap.find(quality);
    return it != intervalMap.end() ? it->second : std::vector<int>{0, 4, 7};
}

inline int getChordNoteCount(ChordQuality quality) {
    return static_cast<int>(getChordIntervals(quality).size());
}

inline int getMaxInversions(ChordQuality quality) {
    return getChordNoteCount(quality);
}

}  // namespace ChordUtils

}  // namespace magda::music
