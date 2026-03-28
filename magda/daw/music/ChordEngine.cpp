#include "ChordEngine.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>

namespace magda::music {

namespace {

std::vector<int> applyInversion(const std::vector<int>& baseIntervals, int inversion) {
    std::vector<int> result = baseIntervals;
    if (inversion == 0)
        return result;
    if (inversion < static_cast<int>(result.size()))
        result[static_cast<size_t>(inversion)] += 12;
    return result;
}

std::vector<int> applyNegativeInversion(const std::vector<int>& baseIntervals, int inversion) {
    std::vector<int> result = baseIntervals;
    if (inversion == 0)
        return result;
    if (inversion < static_cast<int>(result.size()))
        result[static_cast<size_t>(inversion)] -= 12;
    return result;
}

}  // namespace

ChordEngine* ChordEngine::instance = nullptr;
std::mutex ChordEngine::instanceMutex;

ChordEngine& ChordEngine::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance == nullptr)
        instance = new ChordEngine();
    return *instance;
}

void ChordEngine::cleanup() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance != nullptr) {
        delete instance;
        instance = nullptr;
    }
}

ChordEngine::ChordEngine()
    : chordShapes([]() {
          std::map<std::vector<int>, ChordSpec> allShapes;

          std::vector<ChordQuality> qualities = {
              ChordQuality::Major,          ChordQuality::Minor,
              ChordQuality::Diminished,     ChordQuality::Augmented,
              ChordQuality::Power,          ChordQuality::Sus2,
              ChordQuality::Sus4,           ChordQuality::Major7,
              ChordQuality::Minor7,         ChordQuality::Dominant7,
              ChordQuality::Diminished7,    ChordQuality::Dominant9,
              ChordQuality::Major9,         ChordQuality::Minor9,
              ChordQuality::Diminished9,    ChordQuality::Dominant11,
              ChordQuality::Dominant13,     ChordQuality::Minor11,
              ChordQuality::Minor13,        ChordQuality::MajorAdd9,
              ChordQuality::MinorAdd9,      ChordQuality::MajorAdd6,
              ChordQuality::Major7Add13,    ChordQuality::Sus4Add6,
              ChordQuality::Sus2Add6,       ChordQuality::Minor7Add2,
              ChordQuality::Minor7Add6,     ChordQuality::Minor7Add2Add6,
              ChordQuality::Minor7Add4,     ChordQuality::Minor7Add2Add4,
              ChordQuality::Minor7Add4Add6, ChordQuality::Minor7Add2Add4Add6,
              ChordQuality::MajorAdd2,      ChordQuality::MajorAdd4,
              ChordQuality::MinorAdd2,      ChordQuality::MinorAdd4};

          for (ChordQuality quality : qualities) {
              std::vector<int> baseIntervals = ChordUtils::getChordIntervals(quality);
              int maxInversions = static_cast<int>(baseIntervals.size());

              for (int inv = 0; inv < maxInversions; ++inv) {
                  std::vector<int> positiveShape = applyInversion(baseIntervals, inv);
                  std::vector<int> negativeShape = applyNegativeInversion(baseIntervals, inv);

                  std::vector<int> positivePitchClasses = positiveShape;
                  std::vector<int> negativePitchClasses = negativeShape;

                  for (int& pitch : positivePitchClasses)
                      pitch += 12;
                  for (int& pitch : negativePitchClasses)
                      pitch += 12;

                  for (int& pitch : positivePitchClasses)
                      pitch = (pitch + 36) % 36;
                  for (int& pitch : negativePitchClasses)
                      pitch = (pitch + 36) % 36;

                  std::sort(positivePitchClasses.begin(), positivePitchClasses.end());
                  std::sort(negativePitchClasses.begin(), negativePitchClasses.end());

                  allShapes.emplace(positivePitchClasses, ChordSpec(ChordRoot::C, quality, inv));
                  allShapes.emplace(negativePitchClasses, ChordSpec(ChordRoot::C, quality, inv));
              }
          }

          return allShapes;
      }()) {}

ChordEngine::~ChordEngine() = default;

// === CHORD DETECTION ===

Chord ChordEngine::detect(const std::vector<ChordNote>& heldNotes) const {
    if (heldNotes.empty())
        return Chord("none");

    if (heldNotes.size() < 2) {
        Chord chord("");
        chord.notes = heldNotes;
        return chord;
    }

    int actualBassNote = heldNotes[0].noteNumber;
    for (const auto& note : heldNotes)
        if (note.noteNumber < actualBassNote)
            actualBassNote = note.noteNumber;

    std::vector<int> pitchClasses;
    for (const auto& note : heldNotes) {
        int pitchClass = note.noteNumber % 12;
        if (std::find(pitchClasses.begin(), pitchClasses.end(), pitchClass) == pitchClasses.end())
            pitchClasses.push_back(pitchClass);
    }

    if (pitchClasses.size() < 2)
        return Chord("unknown");

    std::sort(pitchClasses.begin(), pitchClasses.end());

    if (pitchClasses.size() == 2) {
        int a = pitchClasses[0];
        int b = pitchClasses[1];
        int diff = (b - a + 12) % 12;
        if (diff != 7 && diff != 5) {
            Chord chord("");
            chord.notes = heldNotes;
            return chord;
        }
    }

    struct ChordCandidate {
        ChordRoot root;
        ChordQuality quality;
        int inversion;
        int matchScore;
        bool isExactMatch;
    };

    std::vector<ChordCandidate> candidates;

    for (int rootOffset = 0; rootOffset < 12; rootOffset++) {
        ChordRoot candidateRoot = static_cast<ChordRoot>(rootOffset);

        std::vector<ChordQuality> qualities = {
            ChordQuality::Major,     ChordQuality::Minor,     ChordQuality::Diminished,
            ChordQuality::Augmented, ChordQuality::Power,     ChordQuality::Major7,
            ChordQuality::Minor7,    ChordQuality::Dominant7, ChordQuality::Sus2,
            ChordQuality::Sus4,      ChordQuality::Major9,    ChordQuality::Minor9,
            ChordQuality::Dominant9};

        for (ChordQuality quality : qualities) {
            auto intervals = ChordUtils::getChordIntervals(quality);

            std::vector<int> chordPitches;
            for (int interval : intervals)
                chordPitches.push_back((rootOffset + interval) % 12);
            std::sort(chordPitches.begin(), chordPitches.end());

            bool isExactMatch = false;
            if (chordPitches.size() == pitchClasses.size()) {
                bool matches = true;
                for (size_t i = 0; i < chordPitches.size(); i++) {
                    if (chordPitches[i] != pitchClasses[i]) {
                        matches = false;
                        break;
                    }
                }
                isExactMatch = matches;
            }

            if (isExactMatch || !chordPitches.empty()) {
                std::vector<int> intersection;
                std::set_intersection(chordPitches.begin(), chordPitches.end(),
                                      pitchClasses.begin(), pitchClasses.end(),
                                      std::back_inserter(intersection));

                std::vector<int> unionSet;
                std::set_union(chordPitches.begin(), chordPitches.end(), pitchClasses.begin(),
                               pitchClasses.end(), std::back_inserter(unionSet));

                double similarity =
                    unionSet.empty() ? 0.0
                                     : static_cast<double>(intersection.size()) / unionSet.size();

                double minSimilarity = isExactMatch ? 1.0 : 0.5;
                if (similarity >= minSimilarity) {
                    int bassPC = actualBassNote % 12;
                    int actualInversion = 0;

                    std::vector<int> rootPositionIntervals = ChordUtils::getChordIntervals(quality);
                    for (size_t i = 0; i < rootPositionIntervals.size(); i++) {
                        int chordTone = (rootOffset + rootPositionIntervals[i]) % 12;
                        if (chordTone == bassPC) {
                            actualInversion = static_cast<int>(i);
                            break;
                        }
                    }

                    // Weighted scoring: root=3, 3rd=2, 5th=2, extensions=1
                    int weightedScore = 0;
                    if (isExactMatch) {
                        weightedScore = 10000;
                    } else {
                        for (size_t i = 0; i < intervals.size(); ++i) {
                            int chordTone = (rootOffset + intervals[i]) % 12;
                            bool found = std::find(pitchClasses.begin(), pitchClasses.end(),
                                                   chordTone) != pitchClasses.end();
                            if (found) {
                                if (i == 0)
                                    weightedScore += 300;  // root
                                else if (i == 1)
                                    weightedScore += 200;  // 3rd (or 2nd for sus)
                                else if (i == 2)
                                    weightedScore += 200;  // 5th
                                else
                                    weightedScore += 100;  // extensions
                            }
                        }
                        // Penalize unmatched input notes (extra notes not in chord)
                        int extraNotes = static_cast<int>(pitchClasses.size()) -
                                         static_cast<int>(intersection.size());
                        weightedScore -= extraNotes * 50;
                    }
                    // Prefer root position
                    weightedScore -= actualInversion * 100;

                    candidates.push_back(
                        {candidateRoot, quality, actualInversion, weightedScore, isExactMatch});
                }
            }
        }
    }

    if (candidates.empty()) {
        Chord chord("");
        chord.notes = heldNotes;
        return chord;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ChordCandidate& a, const ChordCandidate& b) {
                  return a.matchScore > b.matchScore;
              });

    ChordCandidate best = candidates[0];

    Chord chord(best.root, best.quality, heldNotes, actualBassNote, std::nullopt, best.inversion);
    chord.exactMatch = best.isExactMatch;

    // Compute missing intervals and extra notes for partial matches
    if (!best.isExactMatch) {
        auto intervals = ChordUtils::getChordIntervals(best.quality);
        int rootOffset = static_cast<int>(best.root);

        // Which ideal chord tones are missing from the input?
        std::vector<int> idealPitchClasses;
        for (int interval : intervals) {
            int chordTone = (rootOffset + interval) % 12;
            idealPitchClasses.push_back(chordTone);
            if (std::find(pitchClasses.begin(), pitchClasses.end(), chordTone) ==
                pitchClasses.end())
                chord.missingIntervals.push_back(interval);
        }

        // Which input notes are not in the ideal chord?
        for (int pc : pitchClasses) {
            if (std::find(idealPitchClasses.begin(), idealPitchClasses.end(), pc) ==
                idealPitchClasses.end())
                chord.extraPitchClasses.push_back(pc);
        }
    }

    return chord;
}

Chord ChordEngine::smartDetect(const std::vector<ChordNote>& notes) const {
    // Deduplicate by pitch class, keeping highest velocity for each
    std::vector<ChordNote> unique;
    for (const auto& n : notes) {
        int pc = n.noteNumber % 12;
        bool found = false;
        for (auto& u : unique) {
            if (u.noteNumber % 12 == pc) {
                if (n.velocity > u.velocity)
                    u = n;
                found = true;
                break;
            }
        }
        if (!found)
            unique.push_back(n);
    }

    if (unique.size() <= 3)
        return detect(unique);
    if (isPolychordCandidate(unique))
        return detectPolychord(unique);
    return detect(unique);
}

Chord ChordEngine::detectPolychord(const std::vector<ChordNote>& notes) const {
    if (notes.size() < 6)
        return detect(notes);

    bool present[12] = {false};
    for (const auto& n : notes)
        present[(n.noteNumber % 12 + 12) % 12] = true;

    auto triadPcs = [](int rootPc, bool minor) {
        std::array<int, 3> pcs{rootPc, (rootPc + (minor ? 3 : 4)) % 12, (rootPc + 7) % 12};
        return pcs;
    };

    auto pcsContained = [&](const std::array<int, 3>& pcs) {
        return present[pcs[0]] && present[pcs[1]] && present[pcs[2]];
    };

    auto nameOfPc = [](int pc) {
        static const char* kNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                         "F#", "G",  "G#", "A",  "A#", "B"};
        pc = (pc % 12 + 12) % 12;
        return juce::String(kNames[pc]);
    };

    const int offsets[] = {1, 2, 6, 7, 9};
    for (int lowerPc = 0; lowerPc < 12; ++lowerPc) {
        for (bool lowerMinor : {false, true}) {
            auto lowerTriad = triadPcs(lowerPc, lowerMinor);
            if (!pcsContained(lowerTriad))
                continue;

            for (int off : offsets) {
                int upperPc = (lowerPc + off) % 12;
                for (bool upperMinor : {false, true}) {
                    if (off == 9 && !upperMinor)
                        continue;
                    if (off == 2 && lowerMinor != upperMinor)
                        continue;
                    auto upperTriad = triadPcs(upperPc, upperMinor);
                    if (!pcsContained(upperTriad))
                        continue;

                    juce::String lname =
                        nameOfPc(lowerPc) + juce::String(lowerMinor ? " min" : " maj");
                    juce::String uname =
                        nameOfPc(upperPc) + juce::String(upperMinor ? " min" : " maj");
                    juce::String label = lname + " + " + uname;

                    Chord chord(label);
                    chord.notes = notes;
                    chord.name = label;
                    chord.displayName = label;
                    chord.root = static_cast<ChordRoot>(lowerPc);
                    chord.quality = lowerMinor ? ChordQuality::Minor : ChordQuality::Major;
                    chord.inversion = 0;
                    return chord;
                }
            }
        }
    }

    return detect(notes);
}

bool ChordEngine::isPolychordCandidate(const std::vector<ChordNote>& notes) const {
    return notes.size() >= 6;
}

// === CHORD CREATION ===

Chord ChordEngine::buildChordInRootPosition(ChordRoot root, ChordQuality quality,
                                            int octave) const {
    std::vector<int> intervals = ChordUtils::getChordIntervals(quality);
    std::vector<ChordNote> notes;

    int rootMidiNote = static_cast<int>(root) + ((octave + 1) * 12);

    for (int interval : intervals)
        notes.emplace_back(rootMidiNote + interval, 100);

    ChordSpec spec(root, quality, 0);
    juce::String chordName = chordSpecToString(spec);

    Chord chord(chordName);
    chord.notes = notes;
    return chord;
}

std::vector<Chord> ChordEngine::buildChordInversions(ChordRoot root, ChordQuality quality,
                                                     int octave) const {
    std::vector<Chord> inversions;
    int maxInv = getMaxInversions(quality);
    for (int inv = 0; inv < maxInv; ++inv)
        inversions.push_back(buildChordInversion(root, quality, inv, octave));
    return inversions;
}

Chord ChordEngine::buildChordInversion(ChordRoot root, ChordQuality quality, int inversion,
                                       int octave) const {
    std::vector<int> intervals = ChordUtils::getChordIntervals(quality);
    std::vector<ChordNote> notes;

    const int rootMidiNote = static_cast<int>(root) + ((octave + 1) * 12);

    for (size_t i = 0; i < intervals.size(); ++i)
        notes.emplace_back(rootMidiNote + intervals[i], 100);

    const int k = std::max(0, inversion);
    if (k > 0) {
        std::sort(notes.begin(), notes.end(), [](const ChordNote& a, const ChordNote& b) {
            return a.noteNumber < b.noteNumber;
        });
        const int limit = std::min(k, static_cast<int>(notes.size()) - 1);
        for (int i = 0; i < limit; ++i)
            notes[static_cast<size_t>(i)].noteNumber += 12;
        std::sort(notes.begin(), notes.end(), [](const ChordNote& a, const ChordNote& b) {
            return a.noteNumber < b.noteNumber;
        });
    }

    ChordSpec spec(root, quality, inversion);
    juce::String chordName = chordSpecToString(spec);

    Chord chord(chordName);
    chord.notes = notes;
    return chord;
}

// === UTILITY ===

std::vector<int> ChordEngine::getChordIntervals(ChordQuality quality) {
    return ChordUtils::getChordIntervals(quality);
}

int ChordEngine::getMaxInversions(ChordQuality quality) {
    return ChordUtils::getMaxInversions(quality);
}

int ChordEngine::getChordNoteCount(ChordQuality quality) {
    return ChordUtils::getChordNoteCount(quality);
}

ChordSpec ChordEngine::parseChordName(const juce::String& chordString) {
    return ChordUtils::stringToChordSpec(chordString);
}

juce::String ChordEngine::chordSpecToString(const ChordSpec& spec, bool includeInversion) {
    return ChordUtils::chordSpecToString(spec, includeInversion);
}

std::vector<std::pair<juce::String, float>> ChordEngine::findChordsFromNotes(
    const std::vector<int>& pitchClasses) const {
    std::vector<std::pair<juce::String, float>> results;
    if (pitchClasses.empty())
        return results;

    std::vector<int> sortedPitchClasses = pitchClasses;
    std::sort(sortedPitchClasses.begin(), sortedPitchClasses.end());

    for (int rootOffset = 0; rootOffset < 12; rootOffset++) {
        std::vector<ChordQuality> qualities = {
            ChordQuality::Major,     ChordQuality::Minor,     ChordQuality::Diminished,
            ChordQuality::Augmented, ChordQuality::Power,     ChordQuality::Major7,
            ChordQuality::Minor7,    ChordQuality::Dominant7, ChordQuality::Sus2,
            ChordQuality::Sus4,      ChordQuality::Major9,    ChordQuality::Minor9,
            ChordQuality::Dominant9};

        for (ChordQuality quality : qualities) {
            auto intervals = ChordUtils::getChordIntervals(quality);
            std::vector<int> chordPitches;
            for (int interval : intervals)
                chordPitches.push_back((rootOffset + interval) % 12);
            std::sort(chordPitches.begin(), chordPitches.end());

            std::vector<int> intersection;
            std::set_intersection(chordPitches.begin(), chordPitches.end(),
                                  sortedPitchClasses.begin(), sortedPitchClasses.end(),
                                  std::back_inserter(intersection));

            std::vector<int> unionSet;
            std::set_union(chordPitches.begin(), chordPitches.end(), sortedPitchClasses.begin(),
                           sortedPitchClasses.end(), std::back_inserter(unionSet));

            double similarity =
                unionSet.empty() ? 0.0 : static_cast<double>(intersection.size()) / unionSet.size();

            if (similarity >= 0.5) {
                ChordRoot candidateRoot = static_cast<ChordRoot>(rootOffset);
                ChordSpec spec(candidateRoot, quality, 0);
                juce::String chordName = chordSpecToString(spec, false);
                results.emplace_back(chordName, static_cast<float>(similarity));
            }
        }
    }

    std::sort(results.begin(), results.end(),
              [](const std::pair<juce::String, float>& a, const std::pair<juce::String, float>& b) {
                  return a.second > b.second;
              });

    return results;
}

void ChordEngine::finalizeChord(Chord& c) {
    std::sort(c.notes.begin(), c.notes.end(),
              [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });
    if (c.notes.empty())
        return;

    const int bassPc = c.notes.front().noteNumber % 12;
    const int rootPc = static_cast<int>(c.root);
    const auto iv = ChordUtils::getChordIntervals(c.quality);
    int inv = 0;
    for (int i = 0; i < static_cast<int>(iv.size()); ++i) {
        if (((rootPc + iv[static_cast<size_t>(i)]) % 12) == bassPc) {
            inv = i;
            break;
        }
    }
    c.inversion = inv;

    // Don't set displayName — let Chord::getDisplayName() build it dynamically
    // with octave info and (noX)/(+X) annotations for partial matches.
}

}  // namespace magda::music
