#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ChordEngine.hpp"
#include "ChordTypes.hpp"
#include "ScaleDetector.hpp"

namespace magda::music {

class ChordSuggestionEngine {
  public:
    using SuggestionItem = ChordEngine::SuggestionItem;
    using SuggestionParams = ChordEngine::SuggestionParams;

    ChordSuggestionEngine();
    ~ChordSuggestionEngine();

    static juce::String getNoteName(int pitchClass);

    void updateWithChord(const Chord& chord, double currentTimeSeconds);
    bool addChordToContext(const Chord& chord, double currentTimeSeconds,
                           const SuggestionParams& params);
    void reset();
    void clearContext();

    std::vector<SuggestionItem> processNewChord(const Chord& chord, double currentTimeSeconds,
                                                const SuggestionParams& params);

    std::vector<SuggestionItem> generateSuggestions(const std::vector<Chord>& recentChords,
                                                    const SuggestionParams& params);

    std::vector<SuggestionItem> generateSuggestions(const std::vector<Chord>& recentChords,
                                                    const SuggestionParams& params,
                                                    const juce::String& key,
                                                    const juce::String& mode);

    std::vector<Chord> getRecentChords() const;
    juce::String getContextTailString(int maxChords = 8) const;

    std::optional<std::pair<juce::String, juce::String>> inferKeyModeFromHistogram() const;
    std::pair<juce::String, juce::String> inferKeyModeFromContext(
        const std::vector<Chord>& recentChords) const;
    std::optional<std::pair<juce::String, juce::String>> inferKeyModeFromScaleDetection() const;

    juce::String getDetectedScalesString(float novelty = 0.3f) const;
    std::vector<std::pair<juce::String, juce::String>> getTopDetectedScales(
        int maxScales = 3, float novelty = 0.3f) const;

    int calculateTargetOctave(const std::vector<Chord>& recentChords) const;

    Chord buildChordObject(const juce::String& root, const juce::String& quality, int targetOctave,
                           float inversionStrength, const std::vector<Chord>& recentChords) const;
    Chord buildChordInRootPosition(const juce::String& root, const juce::String& quality,
                                   int targetOctave) const;

  private:
    std::array<double, 12> pcsHistogram{};
    double lastPcsUpdateTime{0.0};
    double pcsHistogramTau{5.0};
    mutable std::mutex pcsHistogramMutex;

    std::deque<Chord> recentChords_;
    static constexpr size_t maxChordContext = 6;
    mutable std::mutex chordContextMutex;

    static const std::array<double, 12> KRUMHANSL_MAJOR_PROFILE;
    static const std::array<double, 12> KRUMHANSL_MINOR_PROFILE;
    static const std::array<double, 12> KRUMHANSL_DORIAN_PROFILE;
    static const std::array<double, 12> KRUMHANSL_PHRYGIAN_PROFILE;
    static const std::array<double, 12> KRUMHANSL_LYDIAN_PROFILE;
    static const std::array<double, 12> KRUMHANSL_MIXOLYDIAN_PROFILE;
    static const std::array<double, 12> KRUMHANSL_LOCRIAN_PROFILE;
    static const std::array<juce::String, 12> NOTE_NAMES;

    std::array<double, 12> getDecayedHistogram(double currentTimeSeconds) const;
    double dotProduct(const std::array<double, 12>& a, const std::array<double, 12>& b) const;
    std::array<double, 12> rotateProfile(const std::array<double, 12>& profile, int shift) const;

    std::vector<SuggestionItem> filterRecentChords(const std::vector<SuggestionItem>& candidates,
                                                   const std::vector<Chord>& recentChords) const;

    std::vector<SuggestionItem> generateDiatonicCandidates(
        const juce::String& key, const juce::String& mode, bool add7ths, bool add9ths,
        bool add11ths, bool add13ths, int targetOctave, float inversionStrength,
        const std::vector<Chord>& recentChords) const;

    std::vector<SuggestionItem> generateNonDiatonicCandidates(
        const juce::String& key, const juce::String& mode, bool addAlterations, bool addSlashChords,
        int targetOctave, float inversionStrength, const std::vector<Chord>& recentChords) const;

    std::vector<SuggestionItem> mixCandidates(const std::vector<SuggestionItem>& diatonic,
                                              const std::vector<SuggestionItem>& nonDiatonic,
                                              float novelty, int topK) const;

    juce::String noteAtSemitone(const juce::String& root, int semitones) const;
    int noteToSemitone(const juce::String& note) const;
    bool chordsAreEquivalent(const Chord& a, const Chord& b) const;

    Chord optimizeVoicing(const Chord& chord, float inversionStrength,
                          const std::vector<Chord>& recentChords) const;
    std::vector<Chord> generateInversions(const Chord& chord) const;
    double calculateCentroid(const Chord& chord) const;
};

}  // namespace magda::music
