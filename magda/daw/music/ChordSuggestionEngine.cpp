#include "ChordSuggestionEngine.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <unordered_set>

#include "ChordEngine.hpp"
#include "ScaleDetector.hpp"
#include "Scales.hpp"

namespace magda::music {

// Krumhansl-Kessler profiles (from aideas-db/chord_assistant.py)
const std::array<double, 12> ChordSuggestionEngine::KRUMHANSL_MAJOR_PROFILE = {
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};

const std::array<double, 12> ChordSuggestionEngine::KRUMHANSL_MINOR_PROFILE = {
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

// Extended mode profiles based on music theory and psychological research
const std::array<double, 12> ChordSuggestionEngine::KRUMHANSL_DORIAN_PROFILE = {
    6.20, 2.15, 3.40, 2.25, 4.30, 4.15, 2.45, 5.10, 2.30, 3.60, 2.20, 2.80};

const std::array<double, 12> ChordSuggestionEngine::KRUMHANSL_PHRYGIAN_PROFILE = {
    6.15, 2.10, 3.35, 2.20, 4.25, 4.10, 2.40, 5.05, 2.25, 3.55, 2.15, 2.75};

const std::array<double, 12> ChordSuggestionEngine::KRUMHANSL_LYDIAN_PROFILE = {
    6.30, 2.20, 3.45, 2.30, 4.35, 4.05, 2.50, 5.15, 2.35, 3.65, 2.25, 2.85};

const std::array<double, 12> ChordSuggestionEngine::KRUMHANSL_MIXOLYDIAN_PROFILE = {
    6.25, 2.18, 3.42, 2.28, 4.32, 4.12, 2.48, 5.12, 2.32, 3.62, 2.22, 2.82};

const std::array<double, 12> ChordSuggestionEngine::KRUMHANSL_LOCRIAN_PROFILE = {
    6.10, 2.05, 3.30, 2.15, 4.20, 4.05, 2.35, 5.00, 2.20, 3.50, 2.10, 2.70};

const std::array<juce::String, 12> ChordSuggestionEngine::NOTE_NAMES = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

ChordSuggestionEngine::ChordSuggestionEngine() {
    reset();
}

ChordSuggestionEngine::~ChordSuggestionEngine() = default;

juce::String ChordSuggestionEngine::getNoteName(int pitchClass) {
    return NOTE_NAMES[pitchClass % 12];
}

void ChordSuggestionEngine::updateWithChord(const Chord& chord, double currentTimeSeconds) {
    std::lock_guard<std::mutex> lock(pcsHistogramMutex);

    // Decay histogram since last update (exponential decay)
    if (lastPcsUpdateTime > 0.0) {
        const double dt = std::max(0.0, currentTimeSeconds - lastPcsUpdateTime);
        if (dt > 0.0) {
            const double decay = std::exp(-dt / std::max(0.1, pcsHistogramTau));
            for (auto& bin : pcsHistogram) {
                bin *= decay;
            }
        }
    }

    // Add chord notes to histogram (weight = 1.0 per note)
    for (const auto& note : chord.notes) {
        if (note.noteNumber >= 0) {
            const int pc = note.noteNumber % 12;
            pcsHistogram[pc] += 1.0;
        }
    }

    lastPcsUpdateTime = currentTimeSeconds;
}

bool ChordSuggestionEngine::addChordToContext(const Chord& chord, double currentTimeSeconds,
                                              const SuggestionParams& params) {
    // Update histogram
    updateWithChord(chord, currentTimeSeconds);

    // Check for duplicates and manage context
    bool wasAdded = false;
    {
        std::lock_guard<std::mutex> lock(chordContextMutex);

        bool isDuplicate = false;
        if (!recentChords_.empty()) {
            const Chord& last = recentChords_.back();
            isDuplicate = chordsAreEquivalent(chord, last);
        }

        if (!isDuplicate) {
            // Add to recent chords
            recentChords_.push_back(chord);
            while (recentChords_.size() > maxChordContext) {
                recentChords_.pop_front();
            }
            wasAdded = true;

            // Auto-refresh context if enabled and context is getting stale
            if (params.autoRefreshContext && recentChords_.size() >= maxChordContext) {
                static int chordsProcessedSinceLastClear = 0;
                chordsProcessedSinceLastClear++;

                // Refresh less aggressively: only after 2x context size
                if (chordsProcessedSinceLastClear >= static_cast<int>(maxChordContext * 2)) {
                    // Keep a more useful tail of recent chords
                    auto toKeep = std::min(static_cast<size_t>(5), recentChords_.size());
                    std::deque<Chord> refreshedContext;
                    for (size_t i = recentChords_.size() - toKeep; i < recentChords_.size(); ++i) {
                        refreshedContext.push_back(recentChords_[i]);
                    }
                    recentChords_ = std::move(refreshedContext);
                    chordsProcessedSinceLastClear = 0;
                }
            }
        }
    }

    return wasAdded;
}

void ChordSuggestionEngine::reset() {
    {
        std::lock_guard<std::mutex> lock(pcsHistogramMutex);
        pcsHistogram.fill(0.0);
        lastPcsUpdateTime = 0.0;
    }
    {
        std::lock_guard<std::mutex> lock(chordContextMutex);
        recentChords_.clear();
    }
}

void ChordSuggestionEngine::clearContext() {
    std::lock_guard<std::mutex> lock(chordContextMutex);
    recentChords_.clear();
}

std::array<double, 12> ChordSuggestionEngine::getDecayedHistogram(double currentTimeSeconds) const {
    std::array<double, 12> decayed = pcsHistogram;

    if (lastPcsUpdateTime > 0.0) {
        const double dt = std::max(0.0, currentTimeSeconds - lastPcsUpdateTime);
        if (dt > 0.0) {
            const double decay = std::exp(-dt / std::max(0.1, pcsHistogramTau));
            for (auto& bin : decayed) {
                bin *= decay;
            }
        }
    }

    return decayed;
}

std::optional<std::pair<juce::String, juce::String>>
ChordSuggestionEngine::inferKeyModeFromHistogram() const {
    std::lock_guard<std::mutex> lock(pcsHistogramMutex);

    const auto hist = getDecayedHistogram(juce::Time::getMillisecondCounter() / 1000.0);

    // Check if we have enough data
    const double total = std::accumulate(hist.begin(), hist.end(), 0.0);
    if (total <= 3.0) {
        return std::nullopt;
    }

    // Normalize histogram
    std::array<double, 12> normHist{};
    for (int i = 0; i < 12; ++i) {
        normHist[i] = hist[i] / total;
    }

    // Find best matching key/mode using extended Krumhansl profiles
    double bestScore = -1e9;
    int bestTonic = 0;
    juce::String bestMode = "major";

    for (int tonic = 0; tonic < 12; ++tonic) {
        // Test all modes for each tonic
        std::vector<std::pair<const std::array<double, 12>*, juce::String>> profiles = {
            {&KRUMHANSL_MAJOR_PROFILE, "major"},    {&KRUMHANSL_MINOR_PROFILE, "minor"},
            {&KRUMHANSL_DORIAN_PROFILE, "dorian"},  {&KRUMHANSL_PHRYGIAN_PROFILE, "phrygian"},
            {&KRUMHANSL_LYDIAN_PROFILE, "lydian"},  {&KRUMHANSL_MIXOLYDIAN_PROFILE, "mixolydian"},
            {&KRUMHANSL_LOCRIAN_PROFILE, "locrian"}};

        for (const auto& [profile, modeName] : profiles) {
            const auto rotatedProfile = rotateProfile(*profile, tonic);
            const double score = dotProduct(normHist, rotatedProfile);
            if (score > bestScore) {
                bestScore = score;
                bestTonic = tonic;
                bestMode = modeName;
            }
        }
    }

    return std::make_pair(NOTE_NAMES[bestTonic], bestMode);
}

std::pair<juce::String, juce::String> ChordSuggestionEngine::inferKeyModeFromContext(
    const std::vector<Chord>& recentChords) const {
    if (recentChords.empty()) {
        return {"C", "major"};
    }

    // Count root occurrences and major/minor qualities
    std::map<juce::String, int> rootCounts;
    int majorCount = 0;
    int minorCount = 0;

    // Look at last 32 chords max
    const int maxLookback = std::min(32, static_cast<int>(recentChords.size()));
    const int startIdx = static_cast<int>(recentChords.size()) - maxLookback;

    for (int i = startIdx; i < static_cast<int>(recentChords.size()); ++i) {
        const auto& chord = recentChords[i];

        // Count root notes
        if (chord.rootNoteNumber >= 0) {
            const juce::String rootName = NOTE_NAMES[chord.rootNoteNumber % 12];
            rootCounts[rootName]++;
        }

        // Count major/minor qualities from chord name
        const auto chordName = chord.name.toLowerCase();

        if (chordName.contains("min")) {
            minorCount++;

        } else if (chordName.contains("dim") || chordName.contains("aug") ||
                   chordName.contains("sus") || chordName.contains("5")) {
            // Don't count diminished, augmented, suspended, or power chords toward major/minor

        } else {
            majorCount++;
        }
    }

    // Find most common root
    juce::String mostCommonRoot = "C";
    int maxRootCount = 0;
    for (const auto& pair : rootCounts) {
        if (pair.second > maxRootCount) {
            maxRootCount = pair.second;
            mostCommonRoot = pair.first;
        }
    }

    // Determine mode
    const juce::String mode = (minorCount > majorCount) ? "minor" : "major";

    return {mostCommonRoot, mode};
}

double ChordSuggestionEngine::dotProduct(const std::array<double, 12>& a,
                                         const std::array<double, 12>& b) const {
    double sum = 0.0;
    for (int i = 0; i < 12; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

std::array<double, 12> ChordSuggestionEngine::rotateProfile(const std::array<double, 12>& profile,
                                                            int shift) const {
    std::array<double, 12> rotated{};
    shift = shift % 12;
    if (shift < 0)
        shift += 12;

    for (int i = 0; i < 12; ++i) {
        rotated[i] = profile[(i - shift + 12) % 12];
    }

    return rotated;
}

std::vector<ChordSuggestionEngine::SuggestionItem> ChordSuggestionEngine::generateSuggestions(
    const std::vector<Chord>& recentChords, const SuggestionParams& params) {
    // Use rule-based suggestions (pure algorithmic approach)
    auto keyMode = inferKeyModeFromHistogram();
    if (!keyMode.has_value()) {
        if (recentChords.empty())
            return {};  // No input yet — don't fall back to C major
        // Final fallback to context method
        keyMode = inferKeyModeFromContext(recentChords);
    }

    const auto& [key, mode] = keyMode.value();
    return generateSuggestions(recentChords, params, key, mode);
}

std::vector<ChordSuggestionEngine::SuggestionItem> ChordSuggestionEngine::generateSuggestions(
    const std::vector<Chord>& recentChords, const SuggestionParams& params, const juce::String& key,
    const juce::String& mode) {
    // Calculate average octave from recent chords for contextual voicing
    int targetOctave = calculateTargetOctave(recentChords);

    // Use inversions slider for voice leading, independent of slash chords toggle
    const float effectiveInversions = params.inversions;

    // Generate diatonic and non-diatonic candidates with octave context and recent chords for voice
    // leading
    auto diatonicCandidatesRaw = generateDiatonicCandidates(
        key, mode, params.add7ths, params.add9ths, params.add11ths, params.add13ths, targetOctave,
        effectiveInversions, recentChords);
    auto nonDiatonicCandidatesRaw =
        generateNonDiatonicCandidates(key, mode, params.addAlterations, params.addSlashChords,
                                      targetOctave, effectiveInversions, recentChords);

    auto diatonicCandidates = diatonicCandidatesRaw;
    auto nonDiatonicCandidates = nonDiatonicCandidatesRaw;

    // Filter out recently played chords (last 2-3 chords)
    diatonicCandidates = filterRecentChords(diatonicCandidates, recentChords);
    nonDiatonicCandidates = filterRecentChords(nonDiatonicCandidates, recentChords);

    // Filter candidates based on scales, respecting novelty parameter
    if (params.useScaleFiltering) {
        // Use explicit pitch classes from UI selection, or auto-detect
        std::set<int> allowedPitchClasses;

        if (!params.explicitScalePitchClasses.empty()) {
            allowedPitchClasses = params.explicitScalePitchClasses;
        } else {
            auto detectedScales =
                getTopDetectedScales(3, params.novelty);  // Get top 3 scales (novelty-aware)
            for (const auto& [scaleRoot, scaleName] : detectedScales) {
                int scaleRootSemitone = noteToSemitone(scaleRoot);
                if (scaleRootSemitone < 0)
                    continue;

                const auto& allScales = getAllScalesWithChordsCached();
                for (const auto& scale : allScales) {
                    if (juce::String(scale.name) == scaleName &&
                        scale.rootNote % 12 == scaleRootSemitone) {
                        for (int pitch : scale.pitches) {
                            allowedPitchClasses.insert(pitch % 12);
                        }
                        break;
                    }
                }
            }
        }

        if (!allowedPitchClasses.empty() && params.novelty < 0.8f) {
            float filterStrength =
                1.0f - (params.novelty / 0.8f);  // 1.0 at novelty=0, 0.0 at novelty=0.8

            if (filterStrength > 0.2f) {
                auto filterByPitchClasses = [&](const std::vector<SuggestionItem>& candidates) {
                    if (allowedPitchClasses.empty())
                        return candidates;
                    std::vector<SuggestionItem> filtered;
                    for (const auto& candidate : candidates) {
                        if (candidate.source == "polychord") {
                            filtered.push_back(candidate);
                            continue;
                        }
                        bool belongs = true;
                        for (const auto& note : candidate.chord.notes) {
                            int pc = note.noteNumber % 12;
                            if (allowedPitchClasses.find(pc) == allowedPitchClasses.end()) {
                                belongs = false;
                                break;
                            }
                        }
                        if (belongs) {
                            filtered.push_back(candidate);
                        }
                    }
                    return filtered;
                };

                diatonicCandidates = filterByPitchClasses(diatonicCandidates);

                if (filterStrength > 0.5f) {
                    nonDiatonicCandidates = filterByPitchClasses(nonDiatonicCandidates);
                }
            }
        }
    }

    // Mix candidates based on novelty (following aideas-lab logic)
    auto mixedCandidates =
        mixCandidates(diatonicCandidates, nonDiatonicCandidates, params.novelty, params.topK);

    // Enforce hard parameter filters regardless of earlier stages
    auto isSeventhQuality = [](ChordQuality q) {
        using Q = ChordQuality;
        return q == Q::Major7 || q == Q::Minor7 || q == Q::Dominant7 || q == Q::Diminished7;
    };
    auto is9thQuality = [](ChordQuality q) {
        using Q = ChordQuality;
        return q == Q::Major9 || q == Q::Minor9 || q == Q::Dominant9 || q == Q::Diminished9;
    };
    auto is11thQuality = [](ChordQuality q) {
        using Q = ChordQuality;
        return q == Q::Dominant11 || q == Q::Minor11;
    };
    auto is13thQuality = [](ChordQuality q) {
        using Q = ChordQuality;
        return q == Q::Dominant13 || q == Q::Minor13;
    };
    auto isSlashName = [](const juce::String& name) { return name.contains(" / "); };

    // Helper: central predicate for whether a candidate is allowed under current params
    auto allowedByParams = [&](const SuggestionItem& c) -> bool {
        // novelty = 0.0 => only diatonic allowed
        if (params.novelty <= 0.0f && c.source != "diatonic")
            return false;
        // add7ths=false => exclude seventh qualities
        if (!params.add7ths && isSeventhQuality(c.chord.quality))
            return false;
        if (!params.add9ths && is9thQuality(c.chord.quality))
            return false;
        if (!params.add11ths && is11thQuality(c.chord.quality))
            return false;
        if (!params.add13ths && is13thQuality(c.chord.quality))
            return false;
        // addSlashChords=false => exclude slash names
        if (!params.addSlashChords && isSlashName(c.chord.getName()))
            return false;
        return true;
    };

    std::vector<SuggestionItem> filtered;
    filtered.reserve(mixedCandidates.size());
    for (auto& c : mixedCandidates) {
        if (!allowedByParams(c))
            continue;
        filtered.push_back(std::move(c));
    }

    // Apply priority boost (sort by score descending)
    std::sort(filtered.begin(), filtered.end(),
              [](const SuggestionItem& a, const SuggestionItem& b) { return a.score > b.score; });
    auto boostedCandidates = std::move(filtered);

    // Convert to final format with score decay by position and deduplicate by chord name
    std::vector<SuggestionItem> suggestions;
    auto normName = [](const juce::String& s) {
        auto n = s.toLowerCase();
        n = n.replace(" ", "");
        return n;
    };
    std::set<juce::String> seen;  // normalized chord names
    for (int i = 0;
         i < static_cast<int>(boostedCandidates.size()) && (int)suggestions.size() < params.topK;
         ++i) {
        auto candidate = boostedCandidates[i];
        auto k = normName(candidate.chord.name);
        if (seen.find(k) != seen.end())
            continue;  // skip duplicates
        seen.insert(k);
        candidate.score = candidate.score / (static_cast<float>(suggestions.size()) +
                                             1.0f);  // Decay by final position
        suggestions.push_back(std::move(candidate));
    }

    // Ensure a minimum number of suggestions by progressively relaxing filters
    auto addUnique = [&](const SuggestionItem& item) {
        const auto name = normName(item.chord.name);
        if (seen.find(name) != seen.end())
            return;
        seen.insert(name);
        suggestions.push_back(item);
    };

    const int minPages = 2;    // guarantee at least two pages of chords in UI
    const int minPerPage = 8;  // blocksPerPage in UI
    const int minDesired =
        std::max(params.topK, minPages * minPerPage);  // at least 16, or topK if larger

    if ((int)suggestions.size() < minDesired) {
        // Backfill using raw pools but still respect current params to avoid disabled types
        for (const auto& c : diatonicCandidatesRaw) {
            if ((int)suggestions.size() >= minDesired)
                break;
            if (allowedByParams(c))
                addUnique(c);
        }
        // Only consider non-diatonic backfill when novelty > 0
        if (params.novelty > 0.0f) {
            for (const auto& c : nonDiatonicCandidatesRaw) {
                if ((int)suggestions.size() >= minDesired)
                    break;
                if (allowedByParams(c))
                    addUnique(c);
            }
        }
    }

    if ((int)suggestions.size() < minDesired) {
        // Final safety: synthesize only when novelty allows non-diatonic/fallback content
        if (params.novelty > 0.0f) {
            for (int i = 0; i < 12 && (int)suggestions.size() < minDesired; ++i) {
                juce::String rootName = NOTE_NAMES[i];
                juce::String quality = (i % 2 == 0) ? "maj" : "min";
                auto chord = buildChordObject(rootName, quality, targetOctave, effectiveInversions,
                                              recentChords);
                SuggestionItem item{chord, 0.1f, "", "fallback"};
                addUnique(item);
            }
        }
    }

    // Trim to max desired output (preserve earlier ranking at the top)
    if ((int)suggestions.size() > std::max(minDesired, params.topK)) {
        suggestions.resize(std::max(minDesired, params.topK));
    }

    return suggestions;
}

std::vector<ChordSuggestionEngine::SuggestionItem>
ChordSuggestionEngine::generateDiatonicCandidates(const juce::String& key, const juce::String& mode,
                                                  bool add7ths, bool add9ths, bool add11ths,
                                                  bool add13ths, int targetOctave,
                                                  float inversionStrength,
                                                  const std::vector<Chord>& recentChords) const {
    std::vector<SuggestionItem> candidates;

    const int rootIdx = noteToSemitone(key);
    if (rootIdx < 0)
        return candidates;

    // Scale patterns and qualities for all modes
    std::vector<int> steps;
    std::vector<juce::String> qualities;
    std::vector<juce::String> degrees;

    if (mode.toLowerCase() == "major" || mode.toLowerCase() == "ionian") {
        steps = {0, 2, 4, 5, 7, 9, 11};
        qualities = {"maj", "min", "min", "maj", "maj", "min", "dim"};
        degrees = {"I", "ii", "iii", "IV", "V", "vi", "vii"};
    } else if (mode.toLowerCase() == "minor" || mode.toLowerCase() == "aeolian") {
        steps = {0, 2, 3, 5, 7, 8, 10};
        qualities = {"min", "dim", "maj", "min", "min", "maj", "maj"};
        degrees = {"i", "ii", "III", "iv", "v", "VI", "VII"};
    } else if (mode.toLowerCase() == "dorian") {
        steps = {0, 2, 3, 5, 7, 9, 10};
        qualities = {"min", "min", "maj", "maj", "min", "dim", "maj"};
        degrees = {"i", "ii", "III", "IV", "v", "vi", "VII"};
    } else if (mode.toLowerCase() == "phrygian") {
        steps = {0, 1, 3, 5, 7, 8, 10};
        qualities = {"min", "maj", "maj", "min", "dim", "maj", "min"};
        degrees = {"i", "II", "III", "iv", "v", "VI", "vii"};
    } else if (mode.toLowerCase() == "lydian") {
        steps = {0, 2, 4, 6, 7, 9, 11};
        qualities = {"maj", "maj", "min", "dim", "maj", "min", "min"};
        degrees = {"I", "II", "iii", "iv", "V", "vi", "vii"};
    } else if (mode.toLowerCase() == "mixolydian") {
        steps = {0, 2, 4, 5, 7, 9, 10};
        qualities = {"maj", "min", "dim", "maj", "min", "min", "maj"};
        degrees = {"I", "ii", "iii", "IV", "v", "vi", "VII"};
    } else if (mode.toLowerCase() == "locrian") {
        steps = {0, 1, 3, 5, 6, 8, 10};
        qualities = {"dim", "maj", "min", "min", "maj", "maj", "min"};
        degrees = {"i", "II", "iii", "iv", "V", "VI", "vii"};
    } else if (mode.toLowerCase() == "harmonic_minor") {
        steps = {0, 2, 3, 5, 7, 8, 11};
        qualities = {"min", "dim", "aug", "min", "maj", "maj", "dim"};
        degrees = {"i", "ii", "III", "iv", "V", "VI", "vii"};
    } else if (mode.toLowerCase() == "melodic_minor") {
        steps = {0, 2, 3, 5, 7, 9, 11};
        qualities = {"min", "min", "aug", "maj", "maj", "dim", "dim"};
        degrees = {"i", "ii", "III", "IV", "V", "vi", "vii"};
    } else if (mode.toLowerCase() == "pentatonic_major") {
        steps = {0, 2, 4, 7, 9};
        qualities = {"maj", "maj", "min", "maj", "min"};
        degrees = {"I", "II", "iii", "V", "vi"};
    } else if (mode.toLowerCase() == "pentatonic_minor") {
        steps = {0, 3, 5, 7, 10};
        qualities = {"min", "maj", "min", "min", "maj"};
        degrees = {"i", "III", "iv", "v", "VII"};
    } else if (mode.toLowerCase() == "blues") {
        steps = {0, 3, 5, 6, 7, 10};
        qualities = {"min", "maj", "min", "dim", "min", "maj"};
        degrees = {"i", "III", "iv", "v", "v", "VII"};
    } else {
        // Default to major if mode not recognized
        steps = {0, 2, 4, 5, 7, 9, 11};
        qualities = {"maj", "min", "min", "maj", "maj", "min", "dim"};
        degrees = {"I", "ii", "iii", "IV", "V", "vi", "vii"};
    }

    // Generate basic triads with scale degree priority
    for (size_t i = 0; i < steps.size(); ++i) {
        const juce::String noteAtStep = noteAtSemitone(key, steps[i]);

        // Base priority - scale degree priority will be applied in mixing if needed
        float priority = 1.0f;

        // Create Chord object with proper notes and target octave
        Chord chord = buildChordObject(noteAtStep, qualities[i], targetOctave, inversionStrength,
                                       recentChords);
        candidates.push_back({chord, priority, degrees[i], "diatonic"});

        // Add 7th chords if enabled
        if (add7ths &&
            (degrees[i] == "I" || degrees[i] == "IV" || degrees[i] == "V" ||
             (mode == "major" && degrees[i] == "vi") || (mode == "minor" && degrees[i] == "v"))) {
            juce::String quality7th;
            if (qualities[i] == "maj") {
                quality7th = "maj7";
            } else if (qualities[i] == "min") {
                quality7th = "min7";
            } else if (qualities[i] == "dim") {
                quality7th = "dim7";
            } else {
                continue;  // Skip for other qualities
            }

            // Create 7th chord object with proper notes and target octave
            Chord chord7th = buildChordObject(noteAtStep, quality7th, targetOctave,
                                              inversionStrength, recentChords);
            candidates.push_back({chord7th,
                                  1.5f,  // Higher priority for 7ths
                                  degrees[i] + "7", "diatonic"});
        }

        // Add 9th chords if enabled
        if (add9ths &&
            (degrees[i] == "I" || degrees[i] == "V" || (mode == "major" && degrees[i] == "vi") ||
             (mode == "minor" && degrees[i] == "v"))) {
            juce::String quality9th;
            if (qualities[i] == "maj") {
                quality9th = "maj9";
            } else if (qualities[i] == "min") {
                quality9th = "min9";
            } else {
                continue;  // Skip for other qualities
            }

            // Create 9th chord object with proper notes and target octave
            Chord chord9th = buildChordObject(noteAtStep, quality9th, targetOctave,
                                              inversionStrength, recentChords);
            candidates.push_back({chord9th,
                                  2.0f,  // Highest priority for 9ths
                                  degrees[i] + "9", "diatonic"});
        }

        // Add 11th chords if enabled
        if (add11ths && (degrees[i] == "I" || degrees[i] == "V" || degrees[i] == "IV")) {
            juce::String quality11th;
            if (qualities[i] == "maj") {
                quality11th = "11";  // Dominant11
            } else if (qualities[i] == "min") {
                quality11th = "min11";
            } else {
                continue;
            }

            Chord chord11th = buildChordObject(noteAtStep, quality11th, targetOctave,
                                               inversionStrength, recentChords);
            candidates.push_back({chord11th, 2.5f, degrees[i] + "11", "diatonic"});
        }

        // Add 13th chords if enabled
        if (add13ths && (degrees[i] == "I" || degrees[i] == "V")) {
            juce::String quality13th;
            if (qualities[i] == "maj") {
                quality13th = "13";  // Dominant13
            } else if (qualities[i] == "min") {
                quality13th = "min13";
            } else {
                continue;
            }

            Chord chord13th = buildChordObject(noteAtStep, quality13th, targetOctave,
                                               inversionStrength, recentChords);
            candidates.push_back({chord13th, 3.0f, degrees[i] + "13", "diatonic"});
        }
    }

    return candidates;
}

std::vector<ChordSuggestionEngine::SuggestionItem>
ChordSuggestionEngine::generateNonDiatonicCandidates(const juce::String& key,
                                                     const juce::String& mode, bool addAlterations,
                                                     bool addSlashChords, int targetOctave,
                                                     float inversionStrength,
                                                     const std::vector<Chord>& recentChords) const {
    std::vector<SuggestionItem> candidates;

    const int rootIdx = noteToSemitone(key);
    if (rootIdx < 0)
        return candidates;

    // Borrowed chords based on mode
    if (mode.toLowerCase() == "major" || mode.toLowerCase() == "ionian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 10), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bVII", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 8), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bVI", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 5), "min", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "iv", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "minor" || mode.toLowerCase() == "aeolian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 1), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bII", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 5), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "IV", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 9), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "VI", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "dorian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 1), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bII", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 6), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bV", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "mixolydian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 1), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bII", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 6), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bV", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "lydian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 10), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bVII", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 8), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bVI", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "phrygian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 5), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "IV", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 9), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "VI", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "locrian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 5), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "IV", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 9), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "VI", "non-diatonic"},
                          });
    } else {
        // Default borrowed chords for other modes
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 10), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bVII", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 8), "maj", targetOctave,
                                                inversionStrength, recentChords),
                               1.2f, "bVI", "non-diatonic"},
                          });
    }

    // Secondary dominants
    const std::vector<int> secTargets = {2, 4, 5, 7, 9};
    for (int target : secTargets) {
        const juce::String domRoot = noteAtSemitone(key, (target + 7) % 12);
        candidates.insert(
            candidates.end(),
            {
                {buildChordObject(domRoot, "maj", targetOctave, inversionStrength, recentChords),
                 1.3f, "V/x", "non-diatonic"},
                {buildChordObject(domRoot, "7", targetOctave, inversionStrength, recentChords),
                 1.4f, "V/x", "non-diatonic"},
            });
    }

    // Tritone substitution
    candidates.push_back({buildChordObject(noteAtSemitone(key, 1), "7", targetOctave,
                                           inversionStrength, recentChords),
                          1.3f, "tritone", "non-diatonic"});

    // Extended chords based on mode
    if (mode.toLowerCase() == "major" || mode.toLowerCase() == "ionian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 0), "maj9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "I9", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 7), "9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "V9", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "minor" || mode.toLowerCase() == "aeolian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 0), "min9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "i9", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 7), "9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "v9", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "dorian" || mode.toLowerCase() == "mixolydian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 0), "min9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "i9", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 7), "9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "v9", "non-diatonic"},
                          });
    } else if (mode.toLowerCase() == "lydian") {
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 0), "maj9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "I9", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 7), "9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "V9", "non-diatonic"},
                          });
    } else {
        // Default extended chords for other modes
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(noteAtSemitone(key, 0), "maj9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "I9", "non-diatonic"},
                              {buildChordObject(noteAtSemitone(key, 7), "9", targetOctave,
                                                inversionStrength, recentChords),
                               1.6f, "V9", "non-diatonic"},
                          });
    }

    // Suspended chords
    candidates.insert(candidates.end(),
                      {
                          {buildChordObject(noteAtSemitone(key, 0), "sus4", targetOctave,
                                            inversionStrength, recentChords),
                           1.1f, "sus4", "non-diatonic"},
                          {buildChordObject(noteAtSemitone(key, 0), "sus2", targetOctave,
                                            inversionStrength, recentChords),
                           1.1f, "sus2", "non-diatonic"},
                      });

    // Add alterations if enabled - give them HIGH priority to ensure they appear
    if (addAlterations) {
        const juce::String dominantRoot = noteAtSemitone(key, 7);
        candidates.insert(candidates.end(),
                          {
                              {buildChordObject(dominantRoot, "7alt", targetOctave,
                                                inversionStrength, recentChords),
                               2.5f, "V7alt", "non-diatonic"},
                              {buildChordObject(dominantRoot, "7#5", targetOctave,
                                                inversionStrength, recentChords),
                               2.3f, "V7#5", "non-diatonic"},
                              {buildChordObject(dominantRoot, "7b9", targetOctave,
                                                inversionStrength, recentChords),
                               2.2f, "V7b9", "non-diatonic"},
                              {buildChordObject(dominantRoot, "7#9", targetOctave,
                                                inversionStrength, recentChords),
                               2.1f, "V7#9", "non-diatonic"},
                          });
    }

    // Add true polychords (stacked triads) if enabled by 'Poly' toggle (reusing addSlashChords
    // flag)
    if (addSlashChords) {
        auto makeTriadNotes = [&](const juce::String& rootName, bool isMinor, int octave) {
            int base = noteToSemitone(rootName) + octave * 12;
            std::vector<int> iv = isMinor ? std::vector<int>{0, 3, 7} : std::vector<int>{0, 4, 7};
            std::vector<int> out;
            out.reserve(3);
            for (int d : iv)
                out.push_back(base + d);
            return out;
        };

        auto addPolychord = [&](int lowerSemitoneOffset, bool lowerMinor, int upperSemitoneOffset,
                                bool upperMinor, float priority, const juce::String& label) {
            // Lower chord near targetOctave, upper one octave higher for clarity
            juce::String lowerRoot = noteAtSemitone(key, lowerSemitoneOffset);
            juce::String upperRoot = noteAtSemitone(key, upperSemitoneOffset);
            auto lowerNotes = makeTriadNotes(lowerRoot, lowerMinor, std::max(3, targetOctave));
            auto upperNotes = makeTriadNotes(upperRoot, upperMinor, std::max(4, targetOctave + 1));

            std::vector<ChordNote> notes;
            notes.reserve(lowerNotes.size() + upperNotes.size());
            for (int n : lowerNotes)
                notes.push_back({n, 100});
            for (int n : upperNotes)
                notes.push_back({n, 100});
            std::sort(notes.begin(), notes.end(),
                      [](auto& a, auto& b) { return a.noteNumber < b.noteNumber; });

            // Build chord object with base lower quality for compatibility; override names
            auto baseQuality = lowerMinor ? ChordQuality::Minor : ChordQuality::Major;
            Chord lowerChord(ChordUtils::stringToRoot(lowerRoot), baseQuality, notes,
                             noteToSemitone(lowerRoot) + std::max(3, targetOctave) * 12,
                             std::nullopt, 0);

            juce::String lname = lowerRoot + (lowerMinor ? " min" : " maj");
            juce::String uname = upperRoot + (upperMinor ? " min" : " maj");
            juce::String fullName = lname + " + " + uname;
            lowerChord.name = fullName;
            lowerChord.displayName = fullName;

            candidates.push_back({lowerChord, priority, label, "polychord"});
        };

        // A small, musical set of common stacks
        addPolychord(0, false, 2, false, 2.4f, "I+II");   // C + D
        addPolychord(0, false, 1, false, 2.3f, "I+bII");  // C + Db
        addPolychord(0, false, 6, false, 2.3f, "I+bV");   // C + Gb (tritone)
        addPolychord(0, false, 7, false, 2.2f, "I+V");    // C + G
        addPolychord(0, true, 2, true, 2.1f, "i+ii");     // Cm + Dm
        addPolychord(0, false, 9, true, 2.0f, "I+vi");    // C + Am
    }

    return candidates;
}

std::vector<ChordSuggestionEngine::SuggestionItem> ChordSuggestionEngine::mixCandidates(
    const std::vector<SuggestionItem>& diatonic, const std::vector<SuggestionItem>& nonDiatonic,
    float novelty, int topK) const {
    std::vector<SuggestionItem> result;

    if (novelty <= 0.0f) {
        // Diatonic-only: apply scale degree priorities for traditional harmonic progression
        std::vector<SuggestionItem> prioritizedDiatonic = diatonic;

        // Apply scale degree priorities only for pure diatonic (novelty=0)
        for (auto& candidate : prioritizedDiatonic) {
            const auto& degree = candidate.degree;

            // Base priority based on scale degree importance (traditional harmony)
            float baseScore = 1.0f;
            if (degree == "I" || degree == "i")
                baseScore = 2.0f;  // Tonic - highest
            else if (degree == "V" || degree == "v")
                baseScore = 1.8f;  // Dominant
            else if (degree == "IV" || degree == "iv")
                baseScore = 1.6f;  // Subdominant
            else if (degree == "vi" || degree == "VI")
                baseScore = 1.4f;  // Relative minor/major
            else if (degree == "ii")
                baseScore = 1.2f;  // Supertonic
            else if (degree == "iii" || degree == "III")
                baseScore = 1.1f;  // Mediant
            else
                baseScore = 1.0f;  // vii, VII

            // Boost for 7th and 9th chords
            if (degree.contains("7"))
                baseScore += 0.3f;  // 7th chords get priority boost
            if (degree.contains("9"))
                baseScore += 0.5f;  // 9th chords get higher boost

            candidate.score = baseScore;
        }

        // Sort by priority (highest first)
        std::sort(
            prioritizedDiatonic.begin(), prioritizedDiatonic.end(),
            [](const SuggestionItem& a, const SuggestionItem& b) { return a.score > b.score; });

        // Take top candidates up to topK
        result.resize(std::min(topK, static_cast<int>(prioritizedDiatonic.size())));
        for (int i = 0; i < static_cast<int>(result.size()); ++i) {
            result[i] = prioritizedDiatonic[i];
        }

        return result;
    }

    // Linear mixing: at 50% novelty, we get 50% of each type
    const int ndTake = std::max(1, static_cast<int>(std::round(novelty * topK)));
    const int diaTake = std::max(0, topK - ndTake);

    // Sort non-diatonic candidates by score (highest first) before taking
    std::vector<SuggestionItem> sortedNonDiatonic = nonDiatonic;
    std::sort(sortedNonDiatonic.begin(), sortedNonDiatonic.end(),
              [](const SuggestionItem& a, const SuggestionItem& b) { return a.score > b.score; });

    // Ensure polychords are visible even at low novelty by reserving a couple of early slots
    std::vector<SuggestionItem> polyList;
    for (const auto& c : sortedNonDiatonic) {
        if (c.source == "polychord")
            polyList.push_back(c);
    }
    const int minPolyFirstPage = 2;
    const int polyTake = std::min({minPolyFirstPage, static_cast<int>(polyList.size()), ndTake});

    // Track already used chord names to avoid duplicates when we backfill later
    auto norm = [](const juce::String& s) { return s.toLowerCase().removeCharacters(" \t\n\r"); };
    std::unordered_set<std::string> used;

    // Insert polychords first
    for (int i = 0; i < polyTake; ++i) {
        result.push_back(polyList[i]);
        used.insert(norm(polyList[i].chord.name).toStdString());
    }

    // Add diatonic portion
    for (int i = 0; i < std::min(diaTake, static_cast<int>(diatonic.size())); ++i) {
        const auto k = norm(diatonic[i].chord.name).toStdString();
        if (used.insert(k).second)
            result.push_back(diatonic[i]);
    }

    // Add remaining non-diatonic (excluding ones already inserted), up to ndTake total
    int ndAdded = polyTake;
    for (const auto& cand : sortedNonDiatonic) {
        if (ndAdded >= ndTake)
            break;
        const auto k = norm(cand.chord.name).toStdString();
        if (used.insert(k).second) {
            result.push_back(cand);
            ++ndAdded;
        }
    }

    // If not enough total, backfill from remaining pool
    if (result.size() < static_cast<size_t>(topK)) {
        std::vector<SuggestionItem> pool;

        // Add remaining diatonic
        for (int i = diaTake; i < static_cast<int>(diatonic.size()); ++i) {
            pool.push_back(diatonic[i]);
        }

        // Add remaining non-diatonic (skip ones we already used)
        int skippedForNd = 0;  // count how many non-diatonic we've conceptually taken
        for (const auto& cand : sortedNonDiatonic) {
            if (skippedForNd < ndTake) {
                ++skippedForNd;
                continue;
            }
            const auto k = norm(cand.chord.name).toStdString();
            if (used.find(k) == used.end())
                pool.push_back(cand);
        }

        // Take from pool to fill remaining slots
        const int remaining = topK - static_cast<int>(result.size());
        for (int i = 0; i < std::min(remaining, static_cast<int>(pool.size())); ++i) {
            result.push_back(pool[i]);
        }
    }

    // Ensure we don't exceed topK
    result.resize(std::min(topK, static_cast<int>(result.size())));

    return result;
}

juce::String ChordSuggestionEngine::noteAtSemitone(const juce::String& root, int semitones) const {
    const int rootIdx = noteToSemitone(root);
    if (rootIdx < 0)
        return "C";

    const int targetIdx = (rootIdx + semitones) % 12;
    return NOTE_NAMES[targetIdx];
}

int ChordSuggestionEngine::noteToSemitone(const juce::String& note) const {
    // Handle enharmonic equivalents
    static const std::map<juce::String, int> noteMap = {
        {"C", 0},  {"C#", 1}, {"Db", 1},  {"D", 2},   {"D#", 3}, {"Eb", 3},
        {"E", 4},  {"F", 5},  {"F#", 6},  {"Gb", 6},  {"G", 7},  {"G#", 8},
        {"Ab", 8}, {"A", 9},  {"A#", 10}, {"Bb", 10}, {"B", 11}};

    const auto it = noteMap.find(note);
    return (it != noteMap.end()) ? it->second : -1;
}

Chord ChordSuggestionEngine::buildChordObject(const juce::String& root, const juce::String& quality,
                                              int targetOctave, float inversionStrength,
                                              const std::vector<Chord>& recentChords) const {
    // First, build the basic chord in root position
    Chord chord = buildChordInRootPosition(root, quality, targetOctave);

    // Apply voice leading optimization based on inversion strength
    Chord result = optimizeVoicing(chord, inversionStrength, recentChords);
    // Standardize the chord object (notes sorted, inversion, displayName)
    ChordEngine::finalizeChord(result);
    return result;
}

Chord ChordSuggestionEngine::buildChordInRootPosition(const juce::String& root,
                                                      const juce::String& quality,
                                                      int targetOctave) const {
    // Convert basic parameters
    ChordRoot rootEnum = ChordUtils::stringToRoot(root);
    int rootMidiNote = noteToSemitone(root) + (targetOctave * 12);

    std::vector<ChordNote> notes;
    juce::String finalName = root + " " + quality;
    juce::String displayName = finalName;

    // Helpers
    auto addNote = [&](int midi) {
        if (midi >= 0)
            notes.push_back({midi, 100});
    };
    auto ensureSorted = [&]() {
        std::sort(notes.begin(), notes.end(),
                  [](const auto& a, const auto& b) { return a.noteNumber < b.noteNumber; });
    };

    // Handle slash chords: e.g., "maj/5", "maj/b7"
    if (quality.containsChar('/')) {
        auto base = quality.upToFirstOccurrenceOf("/", false, false).trim();
        auto bassToken = quality.fromFirstOccurrenceOf("/", false, false).trim();

        auto baseQuality = ChordUtils::stringToQuality(base);
        auto intervals = ChordUtils::getChordIntervals(baseQuality);
        for (int iv : intervals)
            addNote(rootMidiNote + iv);

        // Map bass token to semitone offset from root
        int bassOffset = 0;
        if (bassToken == "5")
            bassOffset = 7;
        else if (bassToken == "b7")
            bassOffset = 10;
        else if (bassToken == "3")
            bassOffset = 4;
        else if (bassToken == "b3")
            bassOffset = 3;
        else if (bassToken == "4")
            bassOffset = 5;
        else if (bassToken == "2" || bassToken == "9")
            bassOffset = 2;
        else
            bassOffset = 7;  // sensible default

        int bassMidi = rootMidiNote + bassOffset;
        // Push bass down to be the lowest note
        if (!notes.empty()) {
            while (bassMidi >= notes.front().noteNumber)
                bassMidi -= 12;
        }
        addNote(bassMidi);
        ensureSorted();

        // Build name/display including actual bass note
        auto bassName = noteAtSemitone(root, bassOffset);
        finalName = root + " " + base + " / " + bassName;
        displayName = finalName;

        // Create chord with base quality (enum), but override names below
        Chord chord(rootEnum, baseQuality, notes, rootMidiNote, std::nullopt, 0);
        chord.name = finalName;
        chord.displayName = displayName;
        return chord;
    }

    // Handle altered dominants: 7alt, 7#5, 7b9, 7#9, 7b5
    if (quality.startsWithIgnoreCase("7")) {
        // Start from dominant7
        auto baseQuality = ChordQuality::Dominant7;
        std::vector<int> intervals = {0, 4, 7, 10};

        auto qLower = quality.toLowerCase();
        auto has = [&](const char* tok) { return qLower.contains(tok); };

        // Fifth alterations
        if (has("#5")) {
            // replace 5th (7) with #5 (8)
            for (auto& iv : intervals)
                if (iv == 7)
                    iv = 8;
        }
        if (has("b5")) {
            for (auto& iv : intervals)
                if (iv == 7)
                    iv = 6;
        }
        // Add tension tones for alt
        if (has("b9") || has("alt"))
            intervals.push_back(13);  // b9
        if (has("#9") || has("alt"))
            intervals.push_back(15);  // #9

        // Build notes
        for (int iv : intervals)
            addNote(rootMidiNote + iv);
        ensureSorted();

        Chord chord(rootEnum, baseQuality, notes, rootMidiNote, std::nullopt, 0);
        chord.name = finalName;  // preserve original descriptor like "7alt"
        chord.displayName = finalName;
        return chord;
    }

    // Default path: map via enum table
    auto qualityEnum = ChordUtils::stringToQuality(quality);
    auto intervals = ChordUtils::getChordIntervals(qualityEnum);
    for (int iv : intervals)
        addNote(rootMidiNote + iv);
    Chord chord(rootEnum, qualityEnum, notes, rootMidiNote, std::nullopt, 0);
    chord.name = finalName;
    chord.displayName = finalName;
    return chord;
}

int ChordSuggestionEngine::calculateTargetOctave(const std::vector<Chord>& recentChords) const {
    if (recentChords.empty()) {
        return 4;  // Default to octave 4 (C4 = 60)
    }

    // Calculate average octave from recent chords
    double totalOctave = 0.0;
    int noteCount = 0;

    // Look at last 3-4 chords to get recent context
    const int lookback = std::min(4, static_cast<int>(recentChords.size()));
    const int startIdx = static_cast<int>(recentChords.size()) - lookback;

    for (int i = startIdx; i < static_cast<int>(recentChords.size()); ++i) {
        const auto& chord = recentChords[i];

        // Calculate average octave of all notes in this chord
        for (const auto& note : chord.notes) {
            // Standard General MIDI octave calculation: MIDI 60 = C4 (middle C), so subtract 1 from
            // raw division
            int octave = (note.noteNumber / 12) - 1;
            // Only consider valid MIDI notes (0-127, but practically 12-108)
            if (note.noteNumber >= 12 && note.noteNumber <= 127) {
                totalOctave += octave;
                noteCount++;
            }
        }

        // Also consider root note if it's set and valid
        if (chord.rootNoteNumber >= 12 && chord.rootNoteNumber <= 127) {
            // Standard General MIDI octave calculation: MIDI 60 = C4 (middle C), so subtract 1 from
            // raw division
            int rootOctave = (chord.rootNoteNumber / 12) - 1;
            totalOctave += rootOctave * 2.0;  // Weight root note more heavily
            noteCount += 2;
        }
    }

    if (noteCount == 0) {
        // No valid notes found, return a reasonable default
        return 4;  // Default octave (C4 = 60)
    }

    double averageFloat = totalOctave / noteCount;
    int averageOctave = static_cast<int>(std::round(averageFloat));

    // Clamp to reasonable range (octaves 2-6)
    int clampedOctave = std::max(2, std::min(6, averageOctave));

    return clampedOctave;
}

Chord ChordSuggestionEngine::optimizeVoicing(const Chord& chord, float inversionStrength,
                                             const std::vector<Chord>& recentChords) const {
    // If inversionStrength is 0.0, return root position
    if (inversionStrength <= 0.0f) {
        return chord;
    }

    // If no recent chords, return the chord as-is
    if (recentChords.empty()) {
        return chord;
    }

    // Calculate the centroid (average pitch) of the most recent chord
    const auto& lastChord = recentChords.back();
    double totalPitch = 0.0;
    int noteCount = 0;

    for (const auto& note : lastChord.notes) {
        if (note.noteNumber >= 12 && note.noteNumber <= 127) {
            totalPitch += note.noteNumber;
            noteCount++;
        }
    }

    if (noteCount == 0) {
        return chord;  // No valid notes to work with
    }

    double targetCentroid = totalPitch / noteCount;

    // Generate different inversions of the chord
    std::vector<Chord> inversions = generateInversions(chord);

    // Find the inversion with the centroid closest to the target, but prefer staying in the same
    // octave
    Chord bestInversion = chord;
    double bestDistance = std::numeric_limits<double>::max();
    int bestInversionValue = 0;  // Track the actual inversion value (can be negative)

    for (const auto& inversion : inversions) {
        double inversionCentroid = calculateCentroid(inversion);
        double distance = std::abs(inversionCentroid - targetCentroid);

        // Apply octave penalty: prefer inversions in the same octave as the target
        int targetOctaveRange = static_cast<int>(targetCentroid / 12);
        int inversionOctaveRange = static_cast<int>(inversionCentroid / 12);
        double octavePenalty = std::abs(targetOctaveRange - inversionOctaveRange) *
                               8.0;  // Stronger penalty for octave jumps

        // Prioritize voice leading: heavily penalize large centroid distances
        double voiceLeadingPenalty =
            distance * 1.5;  // Amplify the importance of close voice leading

        // Add bias based on inversionStrength parameter
        // inversionStrength 0.0 = prefer root position (inversion 0)
        // inversionStrength 1.0 = prefer higher inversions (positive values)
        double inversionBias = 0.0;
        if (inversionStrength > 0.0f) {
            // Calculate desired inversion level based on strength
            int desiredInversionLevel =
                static_cast<int>(inversionStrength * 2.0f);  // 0.5 -> 1, 1.0 -> 2
            double inversionDifference = std::abs(inversion.inversion - desiredInversionLevel);
            inversionBias =
                inversionDifference * 3.0;  // Penalty for being far from desired inversion level
        }

        double totalDistance = voiceLeadingPenalty + octavePenalty + inversionBias;

        if (totalDistance < bestDistance) {
            bestDistance = totalDistance;
            bestInversion = inversion;
            bestInversionValue = inversion.inversion;  // Use the inversion property directly
        }
    }

    // Use the best inversion directly when we have voice leading context
    Chord result;
    if (inversionStrength >= 0.3f) {  // Lower threshold - use voice leading for most cases
        // Use voice leading optimization
        result = bestInversion;
        result.inversion = bestInversionValue;
    } else {
        // Only interpolate for very low inversion strength (prefer root position)
        // For simplicity, we'll use a discrete approach:
        // - strength 0.0-0.33: root position
        // - strength 0.34-0.66: first inversion (if available)
        // - strength 0.67-1.0: best inversion
        float scaledStrength = inversionStrength * 3.0f;  // Scale up the strength
        if (scaledStrength <= 0.33f) {
            result = chord;
        } else if (scaledStrength <= 0.66f) {
            // Try to generate first inversion
            auto invs = generateInversions(chord);
            if (invs.size() > 1) {
                result = invs[1];  // First inversion
            } else {
                result = chord;  // Fallback if no inversions available
            }
        } else {
            result = bestInversion;  // Use the best inversion
        }
        result.inversion =
            static_cast<int>(std::round(bestInversionValue * inversionStrength * 3.0f));
    }

    // Detect the actual inversion based on the bass note (lowest note)
    if (!result.notes.empty()) {
        // Sort notes to find the bass note
        auto sortedNotes = result.notes;
        std::sort(
            sortedNotes.begin(), sortedNotes.end(),
            [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

        int bassNote = sortedNotes[0].noteNumber;
        int bassPitchClass = bassNote % 12;

        // Get the root pitch class from the chord name
        juce::String rootStr = result.name.upToFirstOccurrenceOf(":", false, false);
        int rootPitchClass = -1;

        // Map root names to pitch classes
        if (rootStr == "C")
            rootPitchClass = 0;
        else if (rootStr == "C#" || rootStr == "Db")
            rootPitchClass = 1;
        else if (rootStr == "D")
            rootPitchClass = 2;
        else if (rootStr == "D#" || rootStr == "Eb")
            rootPitchClass = 3;
        else if (rootStr == "E")
            rootPitchClass = 4;
        else if (rootStr == "F")
            rootPitchClass = 5;
        else if (rootStr == "F#" || rootStr == "Gb")
            rootPitchClass = 6;
        else if (rootStr == "G")
            rootPitchClass = 7;
        else if (rootStr == "G#" || rootStr == "Ab")
            rootPitchClass = 8;
        else if (rootStr == "A")
            rootPitchClass = 9;
        else if (rootStr == "A#" || rootStr == "Bb")
            rootPitchClass = 10;
        else if (rootStr == "B")
            rootPitchClass = 11;

        if (rootPitchClass >= 0) {
            // Determine inversion based on bass note relative to root
            int detectedInversion = 0;
            if (bassPitchClass == rootPitchClass) {
                detectedInversion = 0;  // Root position
            } else if (bassPitchClass == (rootPitchClass + 4) % 12) {
                detectedInversion = 1;  // First inversion (3rd in bass)
            } else if (bassPitchClass == (rootPitchClass + 7) % 12) {
                detectedInversion = 2;  // Second inversion (5th in bass)
            } else if (bassPitchClass == (rootPitchClass + 10) % 12 ||
                       bassPitchClass == (rootPitchClass + 11) % 12) {
                detectedInversion = 3;  // Third inversion (7th in bass)
            } else {
                // For extended chords, check 9th, 11th, etc.
                detectedInversion = result.inversion;  // Keep calculated value
            }

            result.inversion = detectedInversion;
        }
    }

    return result;
}

std::vector<Chord> ChordSuggestionEngine::generateInversions(const Chord& chord) const {
    std::vector<Chord> inversions;

    if (chord.notes.size() < 2) {
        Chord singleNote = chord;
        singleNote.inversion = 0;
        inversions.push_back(singleNote);
        return inversions;
    }

    const int numNotes = static_cast<int>(chord.notes.size());

    // Helper function to detect the actual inversion based on bass note
    auto detectInversion = [](const Chord& chord) -> int {
        if (chord.notes.empty())
            return 0;

        // Sort notes to find the bass note
        auto sortedNotes = chord.notes;
        std::sort(
            sortedNotes.begin(), sortedNotes.end(),
            [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

        int bassNote = sortedNotes[0].noteNumber;
        int bassPitchClass = bassNote % 12;

        // Get the root pitch class from the chord name
        juce::String rootStr = chord.name.upToFirstOccurrenceOf(":", false, false);
        int rootPitchClass = -1;

        // Map root names to pitch classes
        if (rootStr == "C")
            rootPitchClass = 0;
        else if (rootStr == "C#" || rootStr == "Db")
            rootPitchClass = 1;
        else if (rootStr == "D")
            rootPitchClass = 2;
        else if (rootStr == "D#" || rootStr == "Eb")
            rootPitchClass = 3;
        else if (rootStr == "E")
            rootPitchClass = 4;
        else if (rootStr == "F")
            rootPitchClass = 5;
        else if (rootStr == "F#" || rootStr == "Gb")
            rootPitchClass = 6;
        else if (rootStr == "G")
            rootPitchClass = 7;
        else if (rootStr == "G#" || rootStr == "Ab")
            rootPitchClass = 8;
        else if (rootStr == "A")
            rootPitchClass = 9;
        else if (rootStr == "A#" || rootStr == "Bb")
            rootPitchClass = 10;
        else if (rootStr == "B")
            rootPitchClass = 11;

        if (rootPitchClass >= 0) {
            // Determine inversion based on bass note relative to root
            if (bassPitchClass == rootPitchClass) {
                return 0;  // Root position
            } else if (bassPitchClass == (rootPitchClass + 4) % 12) {
                return 1;  // First inversion (3rd in bass)
            } else if (bassPitchClass == (rootPitchClass + 7) % 12) {
                return 2;  // Second inversion (5th in bass)
            } else if (bassPitchClass == (rootPitchClass + 10) % 12 ||
                       bassPitchClass == (rootPitchClass + 11) % 12) {
                return 3;  // Third inversion (7th in bass)
            } else {
                // For extended chords, check 9th, 11th, etc.
                // This is a simplified approach - in practice, you might want more sophisticated
                // detection
                return 4;  // Extended inversion
            }
        }

        return 0;  // Default to root position if we can't determine
    };

    // CRITICAL FIX: Always start from a canonical root position chord
    // First, we need to determine what the root position should be
    Chord canonicalRootPosition = chord;

    // Sort notes to find the bass note
    auto sortedNotes = canonicalRootPosition.notes;
    std::sort(sortedNotes.begin(), sortedNotes.end(),
              [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

    // Find the root pitch class from the chord name
    juce::String rootStr = chord.name.upToFirstOccurrenceOf(":", false, false);
    int rootPitchClass = -1;

    // Map root names to pitch classes
    if (rootStr == "C")
        rootPitchClass = 0;
    else if (rootStr == "C#" || rootStr == "Db")
        rootPitchClass = 1;
    else if (rootStr == "D")
        rootPitchClass = 2;
    else if (rootStr == "D#" || rootStr == "Eb")
        rootPitchClass = 3;
    else if (rootStr == "E")
        rootPitchClass = 4;
    else if (rootStr == "F")
        rootPitchClass = 5;
    else if (rootStr == "F#" || rootStr == "Gb")
        rootPitchClass = 6;
    else if (rootStr == "G")
        rootPitchClass = 7;
    else if (rootStr == "G#" || rootStr == "Ab")
        rootPitchClass = 8;
    else if (rootStr == "A")
        rootPitchClass = 9;
    else if (rootStr == "A#" || rootStr == "Bb")
        rootPitchClass = 10;
    else if (rootStr == "B")
        rootPitchClass = 11;

    if (rootPitchClass >= 0) {
        // Find the root note in the current chord
        int rootNoteIndex = -1;
        for (size_t i = 0; i < sortedNotes.size(); ++i) {
            if (sortedNotes[i].noteNumber % 12 == rootPitchClass) {
                rootNoteIndex = static_cast<int>(i);
                break;
            }
        }

        if (rootNoteIndex >= 0) {
            // Rearrange notes so the root is the lowest
            std::vector<ChordNote> rearrangedNotes;

            // Start with the root note
            rearrangedNotes.push_back(sortedNotes[rootNoteIndex]);

            // Add all other notes, maintaining their relative order
            for (size_t i = 0; i < sortedNotes.size(); ++i) {
                if (i != static_cast<size_t>(rootNoteIndex)) {
                    rearrangedNotes.push_back(sortedNotes[i]);
                }
            }

            // Ensure the root is in a reasonable octave (around C4-C5)
            int targetOctave = 4;  // C4
            int currentRootOctave = rearrangedNotes[0].noteNumber / 12;
            int octaveShift = targetOctave - currentRootOctave;

            // Apply octave shift to all notes
            for (auto& note : rearrangedNotes) {
                note.noteNumber += (octaveShift * 12);
            }

            // Sort by pitch to ensure proper ordering
            std::sort(
                rearrangedNotes.begin(), rearrangedNotes.end(),
                [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

            canonicalRootPosition.notes = rearrangedNotes;
            canonicalRootPosition.inversion = 0;
        }
    }

    // Now generate inversions from the canonical root position
    // Generate negative inversions (moving highest notes down)
    for (int inv = -numNotes + 1; inv < 0; ++inv) {
        Chord inversion = canonicalRootPosition;

        // Sort notes by pitch
        std::sort(
            inversion.notes.begin(), inversion.notes.end(),
            [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

        // Move the highest 'abs(inv)' notes down an octave
        int notesToMove = std::abs(inv);
        for (int i = numNotes - notesToMove; i < numNotes; ++i) {
            if (i >= 0 && i < static_cast<int>(inversion.notes.size())) {
                inversion.notes[i].noteNumber -= 12;
            }
        }

        // Sort again to maintain order
        std::sort(
            inversion.notes.begin(), inversion.notes.end(),
            [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

        // Now detect the actual inversion based on the bass note
        inversion.inversion = detectInversion(inversion);
        inversions.push_back(inversion);
    }

    // Root position (canonical)
    inversions.push_back(canonicalRootPosition);

    // Generate positive inversions (moving lowest notes up)
    for (int inv = 1; inv < numNotes; ++inv) {
        Chord inversion = canonicalRootPosition;

        // Sort notes by pitch
        std::sort(
            inversion.notes.begin(), inversion.notes.end(),
            [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

        // Move the lowest 'inv' notes up an octave
        for (int i = 0; i < inv && i < static_cast<int>(inversion.notes.size()); ++i) {
            inversion.notes[i].noteNumber += 12;
        }

        // Sort again to maintain order
        std::sort(
            inversion.notes.begin(), inversion.notes.end(),
            [](const ChordNote& a, const ChordNote& b) { return a.noteNumber < b.noteNumber; });

        // Now detect the actual inversion based on the bass note
        inversion.inversion = detectInversion(inversion);
        inversions.push_back(inversion);
    }

    return inversions;
}

double ChordSuggestionEngine::calculateCentroid(const Chord& chord) const {
    if (chord.notes.empty()) {
        return 60.0;  // Default to C4
    }

    double total = 0.0;
    int count = 0;

    for (const auto& note : chord.notes) {
        if (note.noteNumber >= 12 && note.noteNumber <= 127) {
            total += note.noteNumber;
            count++;
        }
    }

    return count > 0 ? (total / count) : 60.0;
}

bool ChordSuggestionEngine::chordsAreEquivalent(const Chord& a, const Chord& b) const {
    // First check if they have the same pitch classes (notes)
    if (!magda::music::chordsAreEquivalent(a, b)) {
        return false;
    }

    // If pitch classes match, also consider them equivalent if they differ only by '?' marker
    // E.g., "C maj" and "C maj?" should be considered the same chord
    juce::String nameA = a.name.replace("?", "").trim();
    juce::String nameB = b.name.replace("?", "").trim();

    return nameA.equalsIgnoreCase(nameB);
}

std::vector<ChordSuggestionEngine::SuggestionItem> ChordSuggestionEngine::processNewChord(
    const Chord& chord, double currentTimeSeconds, const SuggestionParams& params) {
    // Update histogram
    updateWithChord(chord, currentTimeSeconds);

    // Check for duplicates and manage context
    bool isDuplicate = false;
    std::vector<Chord> contextCopy;
    {
        std::lock_guard<std::mutex> lock(chordContextMutex);

        if (!recentChords_.empty()) {
            const Chord& last = recentChords_.back();
            isDuplicate = chordsAreEquivalent(chord, last);
        }

        if (!isDuplicate) {
            // Add to recent chords
            recentChords_.push_back(chord);
            while (recentChords_.size() > maxChordContext) {
                recentChords_.pop_front();
            }

            // Auto-refresh context if enabled and context is getting stale
            // Clear context when we've cycled through 2x maxChordContext to keep suggestions fresh
            if (params.autoRefreshContext && recentChords_.size() >= maxChordContext) {
                static int chordsProcessedSinceLastClear = 0;
                chordsProcessedSinceLastClear++;

                if (chordsProcessedSinceLastClear >= static_cast<int>(maxChordContext)) {
                    // Keep only the most recent few chords to maintain some context
                    auto toKeep = std::min(static_cast<size_t>(3), recentChords_.size());
                    std::deque<Chord> refreshedContext;
                    for (size_t i = recentChords_.size() - toKeep; i < recentChords_.size(); ++i) {
                        refreshedContext.push_back(recentChords_[i]);
                    }
                    recentChords_ = std::move(refreshedContext);
                    chordsProcessedSinceLastClear = 0;
                }
            }

            // Copy context for thread-safe access outside the lock
            if (recentChords_.size() >= 1) {
                contextCopy = std::vector<Chord>(recentChords_.begin(), recentChords_.end());
            }
        }
    }

    // Generate suggestions if we have enough context (outside the mutex lock to avoid deadlock)
    if (!isDuplicate && !contextCopy.empty()) {
        // Generate suggestions (no longer inside mutex lock)
        return generateSuggestions(contextCopy, params);
    }

    // Return empty if duplicate or insufficient context
    return {};
}

std::vector<Chord> ChordSuggestionEngine::getRecentChords() const {
    std::lock_guard<std::mutex> lock(chordContextMutex);
    return std::vector<Chord>(recentChords_.begin(), recentChords_.end());
}

juce::String ChordSuggestionEngine::getContextTailString(int maxChords) const {
    std::lock_guard<std::mutex> lock(chordContextMutex);

    juce::String contextTail;
    int count = 0;
    for (auto it = recentChords_.rbegin(); it != recentChords_.rend() && count < maxChords;
         ++it, ++count) {
        contextTail = it->name + (contextTail.isEmpty() ? "" : ", ") + contextTail;
    }

    return contextTail;
}

std::vector<ChordSuggestionEngine::SuggestionItem> ChordSuggestionEngine::filterRecentChords(
    const std::vector<SuggestionItem>& candidates, const std::vector<Chord>& recentChords) const {
    std::vector<SuggestionItem> filtered;

    // Get last 2 chords to avoid (reduced from 3 to give more variety and prevent stale
    // suggestions)
    const int recentLookback = std::min(2, static_cast<int>(recentChords.size()));

    for (const auto& candidate : candidates) {
        bool isRecent = false;

        // Check if this candidate matches any of the recent chords
        for (int i = static_cast<int>(recentChords.size()) - recentLookback;
             i < static_cast<int>(recentChords.size()); ++i) {
            if (i >= 0) {
                const auto& recentChord = recentChords[i];

                // Compare chord symbols (rough comparison)
                // Extract root and quality from candidate chord symbol (e.g., "G:maj" -> "G maj")
                auto candidateRoot = candidate.chord.name.upToFirstOccurrenceOf(":", false, false);
                auto candidateQuality =
                    candidate.chord.name.fromFirstOccurrenceOf(":", false, false);

                // Build comparison string similar to recent chord name format
                juce::String candidateName = candidateRoot;
                if (candidateQuality == "maj") {
                    candidateName += " maj";
                } else if (candidateQuality == "min") {
                    candidateName += " min";
                } else {
                    candidateName += " " + candidateQuality;
                }

                // Compare with recent chord name (case insensitive)
                if (candidateName.toLowerCase() == recentChord.name.toLowerCase() ||
                    candidateName.toLowerCase().replace(" ", "") ==
                        recentChord.name.toLowerCase().replace(" ", "")) {
                    isRecent = true;
                    break;
                }
            }
        }

        if (!isRecent) {
            filtered.push_back(candidate);
        }
    }

    return filtered;
}

// Infer key/mode using comprehensive scale detection system
std::optional<std::pair<juce::String, juce::String>>
ChordSuggestionEngine::inferKeyModeFromScaleDetection() const {
    std::lock_guard<std::mutex> lock(chordContextMutex);

    if (recentChords_.empty()) {
        return std::nullopt;
    }

    // Limit to recent chords for performance (last 4-6 chords should be enough)
    const int maxChordsForScaleDetection = 6;
    std::vector<Chord> chordsForDetection;
    const int startIdx =
        std::max(0, static_cast<int>(recentChords_.size()) - maxChordsForScaleDetection);
    for (size_t i = startIdx; i < recentChords_.size(); ++i) {
        chordsForDetection.push_back(recentChords_[i]);
    }

    try {
        // Extract pitch classes directly from chord notes
        std::set<int> pitchClasses;
        for (const auto& chord : chordsForDetection) {
            for (const auto& note : chord.notes) {
                if (note.noteNumber >= 0) {
                    pitchClasses.insert(note.noteNumber % 12);
                }
            }
        }

        if (pitchClasses.empty()) {
            return std::nullopt;
        }

        // Generate all possible scales (limit to common ones for performance)
        const auto& allScales = getAllScalesWithChordsCached();

        // Use pitch-class-based detection
        auto detectedScales = detectScalesFromPitchClasses(pitchClasses, allScales);

        if (detectedScales.empty()) {
            return std::nullopt;
        }

        // Get the best matching scale with good confidence
        const auto& [bestScale, matchScore] = detectedScales[0];

        // Only use scale detection if we have good confidence (multiple note matches)
        if (matchScore.matchedNotes.size() < 3) {
            return std::nullopt;  // Fall back to histogram method
        }

        // Extract key and mode from scale name
        juce::String key = NOTE_NAMES[bestScale.rootNote % 12];
        juce::String mode = juce::String(bestScale.name);

        // Normalize mode names for compatibility with existing suggestion logic
        if (mode.contains("Ionian") || mode.contains("Major")) {
            mode = "major";
        } else if (mode.contains("Aeolian") || mode.contains("Minor")) {
            mode = "minor";
        } else if (mode.contains("Dorian")) {
            mode = "dorian";
        } else if (mode.contains("Phrygian")) {
            mode = "phrygian";
        } else if (mode.contains("Lydian")) {
            mode = "lydian";
        } else if (mode.contains("Mixolydian")) {
            mode = "mixolydian";
        } else if (mode.contains("Locrian")) {
            mode = "locrian";
        } else if (mode.contains("Pentatonic")) {
            mode = "pentatonic";
        } else if (mode.contains("Blues")) {
            mode = "blues";
        } else if (mode.contains("Harmonic")) {
            mode = "harmonic";
        } else if (mode.contains("Melodic")) {
            mode = "melodic";
        }

        return std::make_pair(key, mode);

    } catch (const std::exception& e) {
        // Fallback to histogram method if scale detection fails
        return std::nullopt;
    }
}

juce::String ChordSuggestionEngine::getDetectedScalesString(float /*novelty*/) const {
    std::lock_guard<std::mutex> lock(chordContextMutex);

    if (recentChords_.empty()) {
        return "";
    }

    try {
        // Limit to recent chords for performance
        const int maxChordsForScaleDetection = 6;
        std::vector<Chord> chordsForDetection;
        const int startIdx =
            std::max(0, static_cast<int>(recentChords_.size()) - maxChordsForScaleDetection);
        for (size_t i = startIdx; i < recentChords_.size(); ++i) {
            chordsForDetection.push_back(recentChords_[i]);
        }

        // Extract pitch classes directly from chord notes
        std::set<int> pitchClasses;
        for (const auto& chord : chordsForDetection) {
            for (const auto& note : chord.notes) {
                if (note.noteNumber >= 0) {
                    pitchClasses.insert(note.noteNumber % 12);
                }
            }
        }

        if (pitchClasses.empty()) {
            return "";
        }

        // Generate all possible scales
        const auto& allScales = getAllScalesWithChordsCached();

        // Use pitch-class-based detection with improved scoring
        auto detectedScales = detectScalesFromPitchClasses(pitchClasses, allScales);

        // Reweight scales to prioritize complete scales over simpler ones
        for (auto& [scale, matchScore] : detectedScales) {
            // Give bonus to 7-note scales (major, minor, modes) over simpler scales
            if (scale.pitches.size() == 7) {
                matchScore.score += 20;  // Bonus for complete diatonic scales
            }

            // Penalize pentatonic scales when we have enough notes for full scales
            if (scale.name.find("Pentatonic") != std::string::npos && pitchClasses.size() >= 4) {
                matchScore.score -= 15;  // Reduce score for pentatonic when we have rich harmony
            }

            // Bonus for exact chord-scale relationships
            if (scale.name.find("Major") != std::string::npos &&
                scale.name.find("Pentatonic") == std::string::npos) {
                // Check if we have clear major chord tones
                bool hasThird = pitchClasses.count((scale.rootNote + 4) % 12) > 0;
                bool hasFifth = pitchClasses.count((scale.rootNote + 7) % 12) > 0;
                if (hasThird && hasFifth) {
                    matchScore.score += 25;  // Strong bonus for clear major tonality
                }
            }
        }

        // Re-sort after reweighting
        std::sort(detectedScales.begin(), detectedScales.end(),
                  [](const auto& a, const auto& b) { return a.second.score > b.second.score; });

        if (detectedScales.empty()) {
            return "";
        }

        // Format top 9 scales for display (3x3 grid with color gradation)
        juce::StringArray scaleNames;
        const int maxScalesToShow = 9;

        for (int i = 0; i < std::min(maxScalesToShow, static_cast<int>(detectedScales.size()));
             ++i) {
            const auto& [scale, matchScore] = detectedScales[i];

            // Only show scales with good confidence
            if (matchScore.matchedNotes.size() >= 3) {
                // Format scale name without duplicating root note
                juce::String rootNote = NOTE_NAMES[scale.rootNote % 12];
                juce::String scaleName = juce::String(scale.name);

                // Check if scale name already starts with the root note
                if (!scaleName.startsWith(rootNote + " ")) {
                    // Add root note to scale name
                    scaleName = rootNote + " " + scaleName;
                }

                // Add confidence indicator
                int confidence = static_cast<int>(matchScore.coverage * 100);
                scaleName += " (" + juce::String(confidence) + "%)";

                scaleNames.add(scaleName);
            }
        }

        if (scaleNames.isEmpty()) {
            return "";
        }

        return scaleNames.joinIntoString(", ");

    } catch (const std::exception& e) {
        return "";
    }
}

std::vector<std::pair<juce::String, juce::String>> ChordSuggestionEngine::getTopDetectedScales(
    int maxScales, float novelty) const {
    std::vector<std::pair<juce::String, juce::String>> topScales;

    if (recentChords_.empty()) {
        return topScales;
    }

    try {
        // Limit to recent chords for performance
        const int maxChordsForScaleDetection = 6;
        std::vector<Chord> chordsForDetection;
        const int startIdx =
            std::max(0, static_cast<int>(recentChords_.size()) - maxChordsForScaleDetection);
        for (size_t i = startIdx; i < recentChords_.size(); ++i) {
            chordsForDetection.push_back(recentChords_[i]);
        }

        // Extract pitch classes directly from chord notes
        std::set<int> pitchClasses;
        for (const auto& chord : chordsForDetection) {
            for (const auto& note : chord.notes) {
                if (note.noteNumber >= 0) {
                    pitchClasses.insert(note.noteNumber % 12);
                }
            }
        }

        if (pitchClasses.empty()) {
            return topScales;
        }

        // Generate all possible scales
        const auto& allScales = getAllScalesWithChordsCached();

        // Use pitch-class-based detection with improved scoring
        auto detectedScales = detectScalesFromPitchClasses(pitchClasses, allScales);

        // Apply novelty-aware reweighting
        float diatonicBias = 1.0f - novelty;  // High novelty = less diatonic bias
        float chromaticBoost = novelty;       // High novelty = more chromatic scales

        for (auto& [scale, matchScore] : detectedScales) {
            // Diatonic scale bonuses (reduced with high novelty)
            if (scale.pitches.size() == 7) {
                matchScore.score += static_cast<int>(20 * diatonicBias);
            }

            // Pentatonic penalty (reduced with high novelty)
            if (scale.name.find("Pentatonic") != std::string::npos && pitchClasses.size() >= 4) {
                matchScore.score -= static_cast<int>(15 * diatonicBias);
            }

            // Major scale bonus (reduced with high novelty)
            if (scale.name.find("Major") != std::string::npos &&
                scale.name.find("Pentatonic") == std::string::npos) {
                bool hasThird = pitchClasses.count((scale.rootNote + 4) % 12) > 0;
                bool hasFifth = pitchClasses.count((scale.rootNote + 7) % 12) > 0;
                if (hasThird && hasFifth) {
                    matchScore.score += static_cast<int>(25 * diatonicBias);
                }
            }

            // Boost non-traditional scales with high novelty
            if (novelty > 0.5f) {
                if (scale.name.find("Harmonic") != std::string::npos ||
                    scale.name.find("Melodic") != std::string::npos ||
                    scale.name.find("Blues") != std::string::npos ||
                    scale.name.find("Chromatic") != std::string::npos) {
                    matchScore.score += static_cast<int>(30 * chromaticBoost);
                }

                // Boost exotic modes with high novelty
                if (scale.name.find("Phrygian") != std::string::npos ||
                    scale.name.find("Locrian") != std::string::npos ||
                    scale.name.find("Lydian") != std::string::npos) {
                    matchScore.score += static_cast<int>(20 * chromaticBoost);
                }
            }
        }

        // Re-sort after reweighting
        std::sort(detectedScales.begin(), detectedScales.end(),
                  [](const auto& a, const auto& b) { return a.second.score > b.second.score; });

        // Extract top scales with good confidence
        for (int i = 0; i < std::min(maxScales, static_cast<int>(detectedScales.size())); ++i) {
            const auto& [scale, matchScore] = detectedScales[i];

            if (matchScore.matchedNotes.size() >= 3) {
                juce::String rootNote = NOTE_NAMES[scale.rootNote % 12];
                juce::String scaleName = juce::String(scale.name);

                // Normalize mode names
                if (scaleName.contains("Ionian")) {
                    scaleName = scaleName.replace("Ionian", "Major");
                } else if (scaleName.contains("Aeolian")) {
                    scaleName = scaleName.replace("Aeolian", "Minor");
                }

                topScales.push_back({rootNote, scaleName});
            }
        }

    } catch (const std::exception& e) {
        // Return empty vector on error
    }

    return topScales;
}

}  // namespace magda::music
