#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ChordEnums.hpp"
#include "ChordTypes.hpp"

namespace magda::music {

class ChordEngine {
  public:
    struct SuggestionItem {
        Chord chord;
        float score;
        juce::String degree;
        juce::String source;
    };

    struct SuggestionParams {
        float novelty = 0.3f;
        bool add7ths = true;
        bool add9ths = false;
        bool add11ths = false;
        bool add13ths = false;
        bool addAlterations = false;
        bool addSlashChords = false;
        int topK = 18;
        bool autoRefreshContext = true;
        bool useScaleFiltering = true;
        float inversions = 0.5f;
        // When non-empty, filter suggestions to these pitch classes
        // instead of auto-detecting scales. Set by UI scale selection.
        std::set<int> explicitScalePitchClasses;
    };

    static ChordEngine& getInstance();
    static void cleanup();

    ChordEngine(const ChordEngine&) = delete;
    ChordEngine& operator=(const ChordEngine&) = delete;
    ~ChordEngine();

    // Detection
    [[nodiscard]] Chord detect(const std::vector<ChordNote>& heldNotes) const;
    Chord smartDetect(const std::vector<ChordNote>& notes) const;
    Chord detectPolychord(const std::vector<ChordNote>& notes) const;
    bool isPolychordCandidate(const std::vector<ChordNote>& notes) const;

    // Creation
    Chord buildChordInRootPosition(ChordRoot root, ChordQuality quality, int octave = 4) const;
    std::vector<Chord> buildChordInversions(ChordRoot root, ChordQuality quality,
                                            int octave = 4) const;
    Chord buildChordInversion(ChordRoot root, ChordQuality quality, int inversion,
                              int octave = 4) const;

    // Utility
    static std::vector<int> getChordIntervals(ChordQuality quality);
    static int getMaxInversions(ChordQuality quality);
    static int getChordNoteCount(ChordQuality quality);
    static ChordSpec parseChordName(const juce::String& chordString);
    static juce::String chordSpecToString(const ChordSpec& spec, bool includeInversion = true);
    std::vector<std::pair<juce::String, float>> findChordsFromNotes(
        const std::vector<int>& pitchClasses) const;

    // Finalize chord (sort notes, detect inversion, set displayName)
    static void finalizeChord(Chord& c);

  private:
    std::map<std::vector<int>, ChordSpec> chordShapes;

    ChordEngine();
    static ChordEngine* instance;
    static std::mutex instanceMutex;
};

}  // namespace magda::music
