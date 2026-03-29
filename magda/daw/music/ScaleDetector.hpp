#pragma once

#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

#include "ChordTypes.hpp"
#include "Scales.hpp"

namespace magda::music {

inline bool chordsAreEquivalent(const Chord& a, const Chord& b) {
    std::set<int> notesA, notesB;
    for (const auto& n : a.notes)
        notesA.insert(n.noteNumber % 12);
    for (const auto& n : b.notes)
        notesB.insert(n.noteNumber % 12);
    return notesA == notesB;
}

struct MatchScore {
    std::vector<Chord> matchedChords;
    int totalChords = 0;
    int score = 0;
};

inline MatchScore computeMatchScore(const std::vector<Chord>& laneChords,
                                    const ScaleWithChords& scale) {
    int score = 0;
    std::vector<Chord> matches;

    for (size_t i = 0; i < laneChords.size(); ++i) {
        const auto& c = laneChords[i];
        for (const auto& scaleChord : scale.chords) {
            if (chordsAreEquivalent(c, scaleChord)) {
                score += 1;
                if (scaleChord.degree)
                    score += (8 - *scaleChord.degree);
                matches.push_back(scaleChord);
                if (i == 0 && scaleChord.degree && *scaleChord.degree == 1)
                    score += 10;
                if (scale.name.find("Ionian") != std::string::npos)
                    score += 5;
                else if (scale.name.find("Aeolian") != std::string::npos)
                    score += 3;
                break;
            }
        }
    }

    return MatchScore({matches, static_cast<int>(laneChords.size()), score});
}

inline std::vector<std::pair<ScaleWithChords, MatchScore>> detectBestMatchingScales(
    const std::vector<Chord>& laneChords, const std::vector<ScaleWithChords>& allScales) {
    std::vector<std::pair<ScaleWithChords, MatchScore>> scored;

    for (const auto& scale : allScales) {
        const auto matchedChords = computeMatchScore(laneChords, scale);
        if (matchedChords.score > 0) {
            auto scaleCopy = scale;
            for (size_t i = 0; i < scaleCopy.chords.size(); ++i)
                scaleCopy.chords[i].degree = static_cast<int>(i + 1);
            scored.emplace_back(scaleCopy, matchedChords);
        }
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return b.second.score < a.second.score; });

    return scored;
}

struct NoteBasedMatchScore {
    std::set<int> matchedNotes;
    int totalScaleNotes = 0;
    int score = 0;
    double coverage = 0.0;
};

// Detect scales from a set of pitch classes (0-11)
// preferredRootPitchClass: when >= 0, scales rooted on this pitch class get a significant boost
// (use the detected key root so that e.g. C Minor Aeolian ranks above Eb Major Ionian)
inline std::vector<std::pair<ScaleWithChords, NoteBasedMatchScore>> detectScalesFromPitchClasses(
    const std::set<int>& pitchClasses, const std::vector<ScaleWithChords>& allScales,
    int preferredRootPitchClass = -1) {
    if (pitchClasses.empty())
        return {};

    std::vector<std::pair<ScaleWithChords, NoteBasedMatchScore>> scored;

    for (const auto& scale : allScales) {
        std::set<int> scalePitches;
        for (int pitch : scale.pitches)
            scalePitches.insert(pitch % 12);

        std::set<int> matchedNotes;
        std::set_intersection(pitchClasses.begin(), pitchClasses.end(), scalePitches.begin(),
                              scalePitches.end(),
                              std::inserter(matchedNotes, matchedNotes.begin()));

        int matchCount = static_cast<int>(matchedNotes.size());
        if (matchCount == 0)
            continue;

        double coverage = static_cast<double>(matchCount) / scalePitches.size();
        double precision = static_cast<double>(matchCount) / pitchClasses.size();

        int score = matchCount * 10;
        // Use F1 score (harmonic mean of coverage and precision) to avoid
        // pentatonic scales scoring higher than 7-note scales due to coverage
        double f1 = (coverage + precision > 0.0)
                        ? 2.0 * coverage * precision / (coverage + precision)
                        : 0.0;
        score += static_cast<int>(f1 * 80);

        // Prefer diatonic (7-note) scales over pentatonic (5-note) when both match
        int scaleSize = static_cast<int>(scalePitches.size());
        if (scaleSize >= 7)
            score += 10;

        if (scale.name.find("Ionian") != std::string::npos ||
            scale.name.find("Major") != std::string::npos)
            score += 15;
        else if (scale.name.find("Aeolian") != std::string::npos ||
                 scale.name.find("Minor") != std::string::npos)
            score += 12;
        else if (scale.name.find("Pentatonic") != std::string::npos)
            score += 5;

        if (pitchClasses.count(scale.rootNote % 12) > 0)
            score += 10;

        // Strong boost for scales matching the detected key root
        if (preferredRootPitchClass >= 0 && (scale.rootNote % 12) == preferredRootPitchClass)
            score += 20;

        NoteBasedMatchScore matchScore;
        matchScore.matchedNotes = matchedNotes;
        matchScore.totalScaleNotes = static_cast<int>(scalePitches.size());
        matchScore.score = score;
        matchScore.coverage = coverage;

        scored.emplace_back(scale, matchScore);
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.second.score > b.second.score; });

    // Second pass: if a scale is a strict subset of a same-root scale that ranks
    // lower, promote the superset to sit directly above the subset.
    // E.g. C Minor Pentatonic (5 notes) ⊂ C Minor Aeolian (7 notes) →
    // move Aeolian to rank just above Pentatonic.
    for (size_t i = 0; i < scored.size(); ++i) {
        int rootI = scored[i].first.rootNote % 12;
        std::set<int> pitchesI;
        for (int p : scored[i].first.pitches)
            pitchesI.insert(p % 12);

        for (size_t j = i + 1; j < scored.size(); ++j) {
            if (scored[j].first.rootNote % 12 != rootI)
                continue;

            std::set<int> pitchesJ;
            for (int p : scored[j].first.pitches)
                pitchesJ.insert(p % 12);

            // If i is a strict subset of j, rotate j into position i (pushing i to i+1)
            if (pitchesI.size() < pitchesJ.size() &&
                std::includes(pitchesJ.begin(), pitchesJ.end(), pitchesI.begin(), pitchesI.end())) {
                auto promoted = std::move(scored[j]);
                scored.erase(scored.begin() + static_cast<std::ptrdiff_t>(j));
                scored.insert(scored.begin() + static_cast<std::ptrdiff_t>(i), std::move(promoted));
                break;  // re-check from i with the newly inserted element
            }
        }
    }

    return scored;
}

// Convenience: detect scales from a vector of MIDI note numbers
inline std::vector<std::pair<ScaleWithChords, NoteBasedMatchScore>> detectScalesFromMidiNotes(
    const std::vector<int>& midiNotes) {
    std::set<int> pitchClasses;
    for (int note : midiNotes)
        pitchClasses.insert(note % 12);
    return detectScalesFromPitchClasses(pitchClasses, getAllScalesWithChordsCached());
}

}  // namespace magda::music
