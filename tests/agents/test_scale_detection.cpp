#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

#include "../magda/daw/music/ScaleDetector.hpp"
#include "../magda/daw/music/Scales.hpp"

using namespace magda::music;

// Helper: get the name of the top-ranked scale
static std::string topScaleName(const std::set<int>& pitchClasses, int preferredRoot = -1) {
    auto results =
        detectScalesFromPitchClasses(pitchClasses, getAllScalesWithChordsCached(), preferredRoot);
    REQUIRE(!results.empty());
    return results[0].first.name;
}

// Helper: get detected scale names in order
static std::vector<std::string> scaleNames(const std::set<int>& pitchClasses, int limit = 5,
                                           int preferredRoot = -1) {
    auto results =
        detectScalesFromPitchClasses(pitchClasses, getAllScalesWithChordsCached(), preferredRoot);
    std::vector<std::string> names;
    for (int i = 0; i < limit && i < static_cast<int>(results.size()); ++i)
        names.push_back(results[static_cast<size_t>(i)].first.name);
    return names;
}

// Helper: check that `higher` ranks before `lower`
static bool ranksBefore(const std::set<int>& pitchClasses, const std::string& higher,
                        const std::string& lower, int preferredRoot = -1) {
    auto results =
        detectScalesFromPitchClasses(pitchClasses, getAllScalesWithChordsCached(), preferredRoot);
    int higherIdx = -1, lowerIdx = -1;
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        if (results[static_cast<size_t>(i)].first.name == higher && higherIdx < 0)
            higherIdx = i;
        if (results[static_cast<size_t>(i)].first.name == lower && lowerIdx < 0)
            lowerIdx = i;
    }
    REQUIRE(higherIdx >= 0);
    REQUIRE(lowerIdx >= 0);
    return higherIdx < lowerIdx;
}

// Helper: check that a name containing `substr` appears in top N
static bool topNContains(const std::set<int>& pitchClasses, const std::string& substr, int n = 3,
                         int preferredRoot = -1) {
    auto names = scaleNames(pitchClasses, n, preferredRoot);
    for (const auto& name : names) {
        if (name.find(substr) != std::string::npos)
            return true;
    }
    return false;
}

// Pitch class constants
enum PitchClass {
    C = 0,
    Db = 1,
    D = 2,
    Eb = 3,
    E = 4,
    F = 5,
    Gb = 6,
    G = 7,
    Ab = 8,
    A = 9,
    Bb = 10,
    B = 11
};

// ============================================================================
// With detected key root (preferredRootPitchClass)
// These simulate the real scenario where key detection has identified the key.
// ============================================================================

TEST_CASE("Scale detection: C minor with key=C ranks C Minor Aeolian first", "[scale_detection]") {
    std::set<int> cMinor = {C, D, Eb, F, G, Ab, Bb};

    auto top = topScaleName(cMinor, C);
    INFO("Top scale: " << top);
    CHECK(top.find("C ") != std::string::npos);
    CHECK(top.find("Aeolian") != std::string::npos);
}

TEST_CASE("Scale detection: C minor with key=C — Aeolian above D# Major Pentatonic",
          "[scale_detection]") {
    std::set<int> cMinor = {C, D, Eb, F, G, Ab, Bb};

    CHECK(ranksBefore(cMinor, "C Minor (Aeolian)", "D# Major Pentatonic", C));
}

TEST_CASE("Scale detection: C minor with key=C — Aeolian above C Minor Pentatonic",
          "[scale_detection]") {
    std::set<int> cMinor = {C, D, Eb, F, G, Ab, Bb};

    CHECK(ranksBefore(cMinor, "C Minor (Aeolian)", "C Minor Pentatonic", C));
}

TEST_CASE("Scale detection: C major with key=C ranks C Major Ionian first", "[scale_detection]") {
    std::set<int> cMajor = {C, D, E, F, G, A, B};

    auto top = topScaleName(cMajor, C);
    INFO("Top scale: " << top);
    CHECK(top.find("C ") != std::string::npos);
    CHECK(top.find("Ionian") != std::string::npos);
}

TEST_CASE("Scale detection: C major with key=C — Ionian above pentatonic", "[scale_detection]") {
    std::set<int> cMajor = {C, D, E, F, G, A, B};

    CHECK(ranksBefore(cMajor, "C Major (Ionian)", "C Major Pentatonic", C));
}

TEST_CASE("Scale detection: A minor with key=A ranks A Minor Aeolian first", "[scale_detection]") {
    std::set<int> aMinor = {A, B, C, D, E, F, G};

    auto top = topScaleName(aMinor, A);
    INFO("Top scale: " << top);
    CHECK(top.find("A") != std::string::npos);
    CHECK(top.find("Aeolian") != std::string::npos);
}

TEST_CASE("Scale detection: G major with key=G ranks G Major Ionian first", "[scale_detection]") {
    std::set<int> gMajor = {G, A, B, C, D, E, Gb};

    auto top = topScaleName(gMajor, G);
    INFO("Top scale: " << top);
    CHECK(top.find("G ") != std::string::npos);
    CHECK(top.find("Ionian") != std::string::npos);
}

TEST_CASE("Scale detection: Eb major with key=Eb ranks Eb Major Ionian first",
          "[scale_detection]") {
    // Eb major: Eb F G Ab Bb C D
    std::set<int> ebMajor = {Eb, F, G, Ab, Bb, C, D};

    auto top = topScaleName(ebMajor, Eb);
    INFO("Top scale: " << top);
    CHECK(top.find("Ionian") != std::string::npos);
}

// ============================================================================
// Without detected key (no preferred root) — general ranking quality
// ============================================================================

TEST_CASE("Scale detection: without key hint, 7-note scales rank above pentatonic",
          "[scale_detection]") {
    std::set<int> cMinor = {C, D, Eb, F, G, Ab, Bb};

    // Without key context, either C Minor Aeolian or D# Major Ionian could be first
    // (they share pitch classes), but both should rank above pentatonic
    auto top = topScaleName(cMinor);
    INFO("Top scale: " << top);
    // Top result should be a 7-note scale (Ionian or Aeolian), not pentatonic
    CHECK((top.find("Ionian") != std::string::npos || top.find("Aeolian") != std::string::npos));
}

TEST_CASE("Scale detection: C major without key hint has Ionian in top result",
          "[scale_detection]") {
    std::set<int> cMajor = {C, D, E, F, G, A, B};

    // C Major Ionian gets both Ionian name bonus and root-in-set bonus
    auto top = topScaleName(cMajor);
    INFO("Top scale: " << top);
    CHECK(top.find("Ionian") != std::string::npos);
}

// ============================================================================
// Partial pitch sets (fewer than 7 notes)
// ============================================================================

TEST_CASE("Scale detection: 5 notes from C minor with key=C has Aeolian in top 3",
          "[scale_detection]") {
    // C Eb F G Bb — these are exactly C Minor Pentatonic
    std::set<int> partial = {C, Eb, F, G, Bb};

    // With key hint, C Minor Aeolian should appear in top results
    CHECK(topNContains(partial, "Aeolian", 3, C));
}

TEST_CASE("Scale detection: 4 notes from G major has Ionian in top 3", "[scale_detection]") {
    // G B D F# — 4 notes from G major
    std::set<int> partial = {G, B, D, Gb};

    CHECK(topNContains(partial, "Ionian", 3));
}

// ============================================================================
// Empty / minimal input
// ============================================================================

TEST_CASE("Scale detection: empty pitch classes returns empty", "[scale_detection]") {
    std::set<int> empty;
    auto results = detectScalesFromPitchClasses(empty, getAllScalesWithChordsCached());
    REQUIRE(results.empty());
}

TEST_CASE("Scale detection: single pitch class returns results", "[scale_detection]") {
    std::set<int> single = {C};
    auto results = detectScalesFromPitchClasses(single, getAllScalesWithChordsCached());
    REQUIRE(!results.empty());
}

// ============================================================================
// Modes — same pitch classes, different preferred root
// ============================================================================

TEST_CASE("Scale detection: D Dorian with key=D ranks D Dorian in top 3", "[scale_detection]") {
    // D Dorian: D E F G A B C — same notes as C major
    std::set<int> notes = {D, E, F, G, A, B, C};

    CHECK(topNContains(notes, "Dorian", 3, D));
}

TEST_CASE("Scale detection: same notes, different key hints give different top scales",
          "[scale_detection]") {
    // C Ionian / D Dorian / A Aeolian all share the same pitch classes
    std::set<int> notes = {C, D, E, F, G, A, B};

    auto topWithC = topScaleName(notes, C);
    auto topWithA = topScaleName(notes, A);

    INFO("Top with key=C: " << topWithC);
    INFO("Top with key=A: " << topWithA);

    // C hint should prefer C-rooted scale, A hint should prefer A-rooted
    CHECK(topWithC.find("C ") != std::string::npos);
    CHECK(topWithA.find("A ") != std::string::npos);
}
