#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/agents/dsl_interpreter.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

/**
 * Integration tests for the notes.add_chord DSL command.
 *
 * Each test creates a track + clip via DSL, then executes notes.add_chord
 * and verifies the resulting MIDI notes in ClipManager.
 */

using namespace magda;

static void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

// Helper: get the first clip on track
static const ClipInfo* getFirstClip(TrackId trackId) {
    auto& cm = ClipManager::getInstance();
    auto clipIds = cm.getClipsOnTrack(trackId);
    if (clipIds.empty())
        return nullptr;
    return cm.getClip(clipIds.front());
}

// Helper: sort notes by pitch for easier assertions
static std::vector<int> getSortedPitches(const ClipInfo* clip) {
    std::vector<int> pitches;
    for (const auto& note : clip->midiNotes)
        pitches.push_back(note.noteNumber);
    std::sort(pitches.begin(), pitches.end());
    return pitches;
}

// ============================================================================
// Basic chord qualities
// ============================================================================

TEST_CASE("notes.add_chord - C major triad", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=C4, quality=major, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    REQUIRE(tracks.size() == 1);

    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // C4=60, E4=64, G4=67
    REQUIRE(pitches[0] == 60);
    REQUIRE(pitches[1] == 64);
    REQUIRE(pitches[2] == 67);
}

TEST_CASE("notes.add_chord - minor triad", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=A3, quality=minor, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // A3=57, C4=60, E4=64
    REQUIRE(pitches[0] == 57);
    REQUIRE(pitches[1] == 60);
    REQUIRE(pitches[2] == 64);
}

TEST_CASE("notes.add_chord - min7 four-note chord", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=A3, quality=min7, beat=0, length=2)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 4);

    auto pitches = getSortedPitches(clip);
    // A3=57, C4=60, E4=64, G4=67
    REQUIRE(pitches[0] == 57);
    REQUIRE(pitches[1] == 60);
    REQUIRE(pitches[2] == 64);
    REQUIRE(pitches[3] == 67);

    // Verify length
    for (const auto& note : clip->midiNotes)
        REQUIRE(note.lengthBeats == Catch::Approx(2.0));
}

TEST_CASE("notes.add_chord - dom7", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=G3, quality=dom7, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 4);

    auto pitches = getSortedPitches(clip);
    // G3=55, B3=59, D4=62, F4=65
    REQUIRE(pitches[0] == 55);
    REQUIRE(pitches[1] == 59);
    REQUIRE(pitches[2] == 62);
    REQUIRE(pitches[3] == 65);
}

TEST_CASE("notes.add_chord - diminished", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=B3, quality=dim, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // B3=59, D4=62, F4=65
    REQUIRE(pitches[0] == 59);
    REQUIRE(pitches[1] == 62);
    REQUIRE(pitches[2] == 65);
}

TEST_CASE("notes.add_chord - augmented", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=C4, quality=aug, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // C4=60, E4=64, G#4=68
    REQUIRE(pitches[0] == 60);
    REQUIRE(pitches[1] == 64);
    REQUIRE(pitches[2] == 68);
}

TEST_CASE("notes.add_chord - sus2", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=D4, quality=sus2, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // D4=62, E4=64, A4=69
    REQUIRE(pitches[0] == 62);
    REQUIRE(pitches[1] == 64);
    REQUIRE(pitches[2] == 69);
}

TEST_CASE("notes.add_chord - sus4", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=D4, quality=sus4, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // D4=62, G4=67, A4=69
    REQUIRE(pitches[0] == 62);
    REQUIRE(pitches[1] == 67);
    REQUIRE(pitches[2] == 69);
}

TEST_CASE("notes.add_chord - power chord", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=E2, quality=power, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 2);

    auto pitches = getSortedPitches(clip);
    // E2=40, B2=47
    REQUIRE(pitches[0] == 40);
    REQUIRE(pitches[1] == 47);
}

// ============================================================================
// Aliases
// ============================================================================

TEST_CASE("notes.add_chord - quality aliases", "[dsl][chord][alias]") {
    resetState();
    dsl::Interpreter interp;

    SECTION("maj alias") {
        REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                               ".clip.new(bar=1, length_bars=4)"
                               ".notes.add_chord(root=C4, quality=maj, beat=0, length=1)"));

        auto tracks = TrackManager::getInstance().getTracks();
        const auto* clip = getFirstClip(tracks[0].id);
        auto pitches = getSortedPitches(clip);
        REQUIRE(pitches == std::vector<int>{60, 64, 67});
    }

    SECTION("min alias") {
        REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                               ".clip.new(bar=1, length_bars=4)"
                               ".notes.add_chord(root=C4, quality=min, beat=0, length=1)"));

        auto tracks = TrackManager::getInstance().getTracks();
        const auto* clip = getFirstClip(tracks[0].id);
        auto pitches = getSortedPitches(clip);
        // C4=60, Eb4=63, G4=67
        REQUIRE(pitches == std::vector<int>{60, 63, 67});
    }

    SECTION("7 alias for dom7") {
        REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                               ".clip.new(bar=1, length_bars=4)"
                               ".notes.add_chord(root=C4, quality=7, beat=0, length=1)"));

        auto tracks = TrackManager::getInstance().getTracks();
        const auto* clip = getFirstClip(tracks[0].id);
        auto pitches = getSortedPitches(clip);
        // C4=60, E4=64, G4=67, Bb4=70
        REQUIRE(pitches == std::vector<int>{60, 64, 67, 70});
    }

    SECTION("5 alias for power") {
        REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                               ".clip.new(bar=1, length_bars=4)"
                               ".notes.add_chord(root=C4, quality=5, beat=0, length=1)"));

        auto tracks = TrackManager::getInstance().getTracks();
        const auto* clip = getFirstClip(tracks[0].id);
        auto pitches = getSortedPitches(clip);
        // C4=60, G4=67
        REQUIRE(pitches == std::vector<int>{60, 67});
    }
}

// ============================================================================
// Inversions
// ============================================================================

TEST_CASE("notes.add_chord - first inversion", "[dsl][chord][inversion]") {
    resetState();
    dsl::Interpreter interp;

    // C major first inversion: E4, G4, C5
    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=4)"
                       ".notes.add_chord(root=C4, quality=major, beat=0, length=1, inversion=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // C4+12=C5=72, E4=64, G4=67 → sorted: 64, 67, 72
    REQUIRE(pitches[0] == 64);
    REQUIRE(pitches[1] == 67);
    REQUIRE(pitches[2] == 72);
}

TEST_CASE("notes.add_chord - second inversion", "[dsl][chord][inversion]") {
    resetState();
    dsl::Interpreter interp;

    // C major second inversion: G4, C5, E5
    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=4)"
                       ".notes.add_chord(root=C4, quality=major, beat=0, length=1, inversion=2)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto pitches = getSortedPitches(clip);
    // C4+12=72, E4+12=76, G4=67 → sorted: 67, 72, 76
    REQUIRE(pitches[0] == 67);
    REQUIRE(pitches[1] == 72);
    REQUIRE(pitches[2] == 76);
}

// ============================================================================
// Beat position, length, velocity
// ============================================================================

TEST_CASE("notes.add_chord - beat position and velocity", "[dsl][chord]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_chord(root=C4, quality=major, beat=4.5, length=2, velocity=80)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    for (const auto& note : clip->midiNotes) {
        REQUIRE(note.startBeat == Catch::Approx(4.5));
        REQUIRE(note.lengthBeats == Catch::Approx(2.0));
        REQUIRE(note.velocity == 80);
    }
}

// ============================================================================
// Multiple chords in sequence
// ============================================================================

TEST_CASE("notes.add_chord - multiple chords (progression)", "[dsl][chord][progression]") {
    resetState();
    dsl::Interpreter interp;

    // Em - C - G - D progression
    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\").clip.new(bar=1, length_bars=4)\n"
        "track(name=\"Test\").notes.add_chord(root=E3, quality=min, beat=0, length=4)\n"
        "track(name=\"Test\").notes.add_chord(root=C3, quality=major, beat=4, length=4)\n"
        "track(name=\"Test\").notes.add_chord(root=G3, quality=major, beat=8, length=4)\n"
        "track(name=\"Test\").notes.add_chord(root=D3, quality=major, beat=12, length=4)"));

    auto tracks = TrackManager::getInstance().getTracks();
    REQUIRE(tracks.size() == 1);

    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 12);  // 4 triads × 3 notes
}

// ============================================================================
// Auto-placement (bar omitted)
// ============================================================================

TEST_CASE("clip.new without bar places after last clip", "[dsl][chord][autoplace]") {
    resetState();
    dsl::Interpreter interp;

    // Create first clip at bar 1, 4 bars long (ends at bar 5)
    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\").clip.new(bar=1, length_bars=4)"));

    // Create second clip without specifying bar — should auto-place at bar 5
    REQUIRE(interp.execute("track(name=\"Test\").clip.new(length_bars=2)"
                           ".notes.add_chord(root=C4, quality=major, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    auto& cm = ClipManager::getInstance();
    auto clipIds = cm.getClipsOnTrack(tracks[0].id);
    REQUIRE(clipIds.size() == 2);

    // Second clip should start at bar 5 (= 8 seconds at 120 BPM)
    auto* clip2 = cm.getClip(clipIds[1]);
    REQUIRE(clip2 != nullptr);
    REQUIRE(clip2->startTime == Catch::Approx(8.0));

    // Chords should be on the second clip, not the first
    REQUIRE(clip2->midiNotes.size() == 3);
    auto* clip1 = cm.getClip(clipIds[0]);
    REQUIRE(clip1->midiNotes.empty());
}

TEST_CASE("clip.new without bar on empty track places at bar 1", "[dsl][chord][autoplace]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\").clip.new(length_bars=4)"));

    auto tracks = TrackManager::getInstance().getTracks();
    auto& cm = ClipManager::getInstance();
    auto clipIds = cm.getClipsOnTrack(tracks[0].id);
    REQUIRE(clipIds.size() == 1);

    // Should start at bar 1 (= 0 seconds)
    auto* clip = cm.getClip(clipIds[0]);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->startTime == Catch::Approx(0.0));
}

// ============================================================================
// Error cases
// ============================================================================

TEST_CASE("notes.add_chord - missing root", "[dsl][chord][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_chord(quality=major, beat=0, length=1)");
    REQUIRE_FALSE(result);
}

TEST_CASE("notes.add_chord - missing quality", "[dsl][chord][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_chord(root=C4, beat=0, length=1)");
    REQUIRE_FALSE(result);
}

TEST_CASE("notes.add_chord - unknown quality", "[dsl][chord][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_chord(root=C4, quality=bogus, beat=0, length=1)");
    REQUIRE_FALSE(result);
}

// ============================================================================
// Error recovery — failed statement doesn't block subsequent ones
// ============================================================================

TEST_CASE("notes.add_chord - continues after failed statement", "[dsl][chord][recovery]") {
    resetState();
    dsl::Interpreter interp;

    // First statement fails (bad FX), but track is created.
    // Second statement should still add chords to the clip.
    bool result =
        interp.execute("track(name=\"Test\", type=\"midi\").fx.add(name=\"NonExistentPlugin\")"
                       ".clip.new(bar=1, length_bars=4)\n"
                       "track(name=\"Test\").clip.new(bar=1, length_bars=4)"
                       ".notes.add_chord(root=C4, quality=major, beat=0, length=1)");
    REQUIRE(result);

    auto tracks = TrackManager::getInstance().getTracks();
    REQUIRE(tracks.size() == 1);

    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);
}

// ============================================================================
// Undo
// ============================================================================

TEST_CASE("notes.add_chord - undo removes all chord notes", "[dsl][chord][undo]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_chord(root=C4, quality=major, beat=0, length=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    UndoManager::getInstance().undo();
    REQUIRE(clip->midiNotes.empty());

    UndoManager::getInstance().redo();
    REQUIRE(clip->midiNotes.size() == 3);
}

// End of notes.add_chord tests
// Arpeggio tests are in test_dsl_add_arpeggio.cpp
