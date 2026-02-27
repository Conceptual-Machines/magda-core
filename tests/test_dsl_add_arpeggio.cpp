#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/agents/dsl_interpreter.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

/**
 * Integration tests for the notes.add_arpeggio DSL command.
 *
 * Each test creates a track + clip via DSL, then executes notes.add_arpeggio
 * and verifies the resulting MIDI notes in ClipManager.
 */

using namespace magda;

static void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

static const ClipInfo* getFirstClip(TrackId trackId) {
    auto& cm = ClipManager::getInstance();
    auto clipIds = cm.getClipsOnTrack(trackId);
    if (clipIds.empty())
        return nullptr;
    return cm.getClip(clipIds.front());
}

// Helper: get notes sorted by start beat, then by pitch
static std::vector<std::pair<int, double>> getNotesByBeat(const ClipInfo* clip) {
    std::vector<std::pair<int, double>> result;
    for (const auto& note : clip->midiNotes)
        result.push_back({note.noteNumber, note.startBeat});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.second - b.second) < 0.001)
            return a.first < b.first;
        return a.second < b.second;
    });
    return result;
}

// ============================================================================
// Basic patterns
// ============================================================================

TEST_CASE("add_arpeggio - up pattern (default)", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto notes = getNotesByBeat(clip);
    // C4=60 @ 0, E4=64 @ 0.5, G4=67 @ 1.0
    REQUIRE(notes[0].first == 60);
    REQUIRE(notes[0].second == Catch::Approx(0.0));
    REQUIRE(notes[1].first == 64);
    REQUIRE(notes[1].second == Catch::Approx(0.5));
    REQUIRE(notes[2].first == 67);
    REQUIRE(notes[2].second == Catch::Approx(1.0));

    // Default note_length equals step
    for (const auto& note : clip->midiNotes)
        REQUIRE(note.lengthBeats == Catch::Approx(0.5));
}

TEST_CASE("add_arpeggio - down pattern", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.25, pattern=down)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto notes = getNotesByBeat(clip);
    // G4@0, E4@0.25, C4@0.5
    REQUIRE(notes[0].first == 67);
    REQUIRE(notes[0].second == Catch::Approx(0.0));
    REQUIRE(notes[1].first == 64);
    REQUIRE(notes[1].second == Catch::Approx(0.25));
    REQUIRE(notes[2].first == 60);
    REQUIRE(notes[2].second == Catch::Approx(0.5));
}

TEST_CASE("add_arpeggio - updown pattern", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    // C min7 = C4(60), Eb4(63), G4(67), Bb4(70)
    // updown skips endpoints: C Eb G Bb G Eb → 6 notes
    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=C4, quality=min7, beat=0, step=0.5, pattern=updown)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 6);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].first == 60);  // C4@0
    REQUIRE(notes[1].first == 63);  // Eb4@0.5
    REQUIRE(notes[2].first == 67);  // G4@1.0
    REQUIRE(notes[3].first == 70);  // Bb4@1.5
    REQUIRE(notes[4].first == 67);  // G4@2.0
    REQUIRE(notes[5].first == 63);  // Eb4@2.5
}

// ============================================================================
// Inversion
// ============================================================================

TEST_CASE("add_arpeggio - first inversion", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    // C major first inversion: E4(64), G4(67), C5(72)
    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, inversion=1)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto notes = getNotesByBeat(clip);
    // Sorted ascending: E4@0, G4@0.5, C5@1.0
    REQUIRE(notes[0].first == 64);
    REQUIRE(notes[0].second == Catch::Approx(0.0));
    REQUIRE(notes[1].first == 67);
    REQUIRE(notes[1].second == Catch::Approx(0.5));
    REQUIRE(notes[2].first == 72);
    REQUIRE(notes[2].second == Catch::Approx(1.0));
}

TEST_CASE("add_arpeggio - second inversion down", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    // C major second inversion: G4(67), C5(72), E5(76) — down: E5, C5, G4
    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, "
                           "inversion=2, pattern=down)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].first == 76);  // E5@0
    REQUIRE(notes[1].first == 72);  // C5@0.5
    REQUIRE(notes[2].first == 67);  // G4@1.0
}

// ============================================================================
// note_length parameter
// ============================================================================

TEST_CASE("add_arpeggio - custom note_length", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, note_length=1.0)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    // note_length=1.0, step=0.5 → overlapping notes (legato)
    for (const auto& note : clip->midiNotes)
        REQUIRE(note.lengthBeats == Catch::Approx(1.0));
}

TEST_CASE("add_arpeggio - staccato (note_length < step)", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=1.0, note_length=0.25)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    for (const auto& note : clip->midiNotes)
        REQUIRE(note.lengthBeats == Catch::Approx(0.25));

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].second == Catch::Approx(0.0));
    REQUIRE(notes[1].second == Catch::Approx(1.0));
    REQUIRE(notes[2].second == Catch::Approx(2.0));
}

// ============================================================================
// Beat offset and velocity
// ============================================================================

TEST_CASE("add_arpeggio - beat offset and velocity", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=A3, quality=minor, beat=4.0, step=0.5, velocity=80)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    auto notes = getNotesByBeat(clip);
    // A3@4.0, C4@4.5, E4@5.0
    REQUIRE(notes[0].first == 57);
    REQUIRE(notes[0].second == Catch::Approx(4.0));
    REQUIRE(notes[1].first == 60);
    REQUIRE(notes[1].second == Catch::Approx(4.5));
    REQUIRE(notes[2].first == 64);
    REQUIRE(notes[2].second == Catch::Approx(5.0));

    for (const auto& note : clip->midiNotes)
        REQUIRE(note.velocity == 80);
}

// ============================================================================
// Fill mode
// ============================================================================

TEST_CASE("add_arpeggio - fill covers entire clip", "[dsl][arpeggio][fill]") {
    resetState();
    dsl::Interpreter interp;

    // 2-bar clip at 120 BPM = 8 beats, step=0.5 → 16 notes cycling C E G
    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=2)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, fill=true)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 16);

    auto notes = getNotesByBeat(clip);
    // Verify cyclic pattern: C E G C E G ...
    const int expected[] = {60, 64, 67};
    for (size_t i = 0; i < notes.size(); i++)
        REQUIRE(notes[i].first == expected[i % 3]);

    // First note at 0, last at 7.5
    REQUIRE(notes.front().second == Catch::Approx(0.0));
    REQUIRE(notes.back().second == Catch::Approx(7.5));
}

TEST_CASE("add_arpeggio - fill with updown pattern", "[dsl][arpeggio][fill]") {
    resetState();
    dsl::Interpreter interp;

    // C major updown = C E G E (4 notes per cycle), 1-bar clip = 4 beats, step=0.5 → 8 notes
    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=1)"
                           ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, "
                           "pattern=updown, fill=true)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 8);

    auto notes = getNotesByBeat(clip);
    // updown cycle: C E G E C E G E
    const int expected[] = {60, 64, 67, 64};
    for (size_t i = 0; i < notes.size(); i++)
        REQUIRE(notes[i].first == expected[i % 4]);
}

TEST_CASE("add_arpeggio - fill with down pattern", "[dsl][arpeggio][fill]") {
    resetState();
    dsl::Interpreter interp;

    // 1-bar clip = 4 beats, step=0.5, down: G E C → 8 notes cycling
    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=1)"
        ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, pattern=down, fill=true)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 8);

    auto notes = getNotesByBeat(clip);
    const int expected[] = {67, 64, 60};
    for (size_t i = 0; i < notes.size(); i++)
        REQUIRE(notes[i].first == expected[i % 3]);
}

// ============================================================================
// Beats parameter (partial fill)
// ============================================================================

TEST_CASE("add_arpeggio - beats parameter for exact duration", "[dsl][arpeggio][fill]") {
    resetState();
    dsl::Interpreter interp;

    // Fill only 2 beats of a 4-bar clip — step=0.5 → 4 notes
    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=4)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, beats=2)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 4);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].second == Catch::Approx(0.0));
    REQUIRE(notes[3].second == Catch::Approx(1.5));  // last note within 2 beats
}

TEST_CASE("add_arpeggio - chord progression with beats", "[dsl][arpeggio][progression]") {
    resetState();
    dsl::Interpreter interp;

    // E minor for 4 beats, then C major for 4 beats in a 2-bar clip
    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=2)"
                       ".notes.add_arpeggio(root=E4, quality=min, beat=0, step=0.5, beats=4)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=4, step=0.5, beats=4)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);

    // 4 beats / 0.5 step = 8 notes each, 16 total
    REQUIRE(clip->midiNotes.size() == 16);

    auto notes = getNotesByBeat(clip);

    // First half: E minor (E4=64, G4=67, B4=71)
    const int eMinor[] = {64, 67, 71};
    for (size_t i = 0; i < 8; i++) {
        REQUIRE(notes[i].first == eMinor[i % 3]);
        REQUIRE(notes[i].second == Catch::Approx(i * 0.5));
    }

    // Second half: C major (C4=60, E4=64, G4=67)
    const int cMajor[] = {60, 64, 67};
    for (size_t i = 0; i < 8; i++) {
        REQUIRE(notes[i + 8].first == cMajor[i % 3]);
        REQUIRE(notes[i + 8].second == Catch::Approx(4.0 + i * 0.5));
    }
}

TEST_CASE("add_arpeggio - four-chord progression", "[dsl][arpeggio][progression]") {
    resetState();
    dsl::Interpreter interp;

    // Em - C - G - D, each for 4 beats in a 4-bar clip
    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=4)"
                       ".notes.add_arpeggio(root=E4, quality=min, beat=0, step=0.5, beats=4)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=4, step=0.5, beats=4)"
                       ".notes.add_arpeggio(root=G3, quality=major, beat=8, step=0.5, beats=4)"
                       ".notes.add_arpeggio(root=D4, quality=major, beat=12, step=0.5, beats=4)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);

    // 4 chords × 8 notes each = 32
    REQUIRE(clip->midiNotes.size() == 32);

    auto notes = getNotesByBeat(clip);

    // Verify boundaries between chords
    // Em starts at 0
    REQUIRE(notes[0].first == 64);  // E4
    REQUIRE(notes[0].second == Catch::Approx(0.0));
    // C major starts at beat 4
    REQUIRE(notes[8].first == 60);  // C4
    REQUIRE(notes[8].second == Catch::Approx(4.0));
    // G major starts at beat 8
    REQUIRE(notes[16].first == 55);  // G3
    REQUIRE(notes[16].second == Catch::Approx(8.0));
    // D major starts at beat 12
    REQUIRE(notes[24].first == 62);  // D4
    REQUIRE(notes[24].second == Catch::Approx(12.0));
    // Last note at beat 15.5
    REQUIRE(notes[31].second == Catch::Approx(15.5));
}

// ============================================================================
// Extended chord qualities
// ============================================================================

TEST_CASE("add_arpeggio - 9th chord", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    // C dom9 = C4(60), E4(64), G4(67), Bb4(70), D5(74) — 5 notes
    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_arpeggio(root=C4, quality=dom9, beat=0, step=0.5)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 5);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].first == 60);  // C4
    REQUIRE(notes[1].first == 64);  // E4
    REQUIRE(notes[2].first == 67);  // G4
    REQUIRE(notes[3].first == 70);  // Bb4
    REQUIRE(notes[4].first == 74);  // D5
}

TEST_CASE("add_arpeggio - min7b5 (half diminished)", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    // B half-dim = B3(59), D4(62), F4(65), A4(69)
    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_arpeggio(root=B3, quality=half_dim, beat=0, step=0.5)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 4);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].first == 59);  // B3
    REQUIRE(notes[1].first == 62);  // D4
    REQUIRE(notes[2].first == 65);  // F4
    REQUIRE(notes[3].first == 69);  // A4
}

// ============================================================================
// Undo/redo
// ============================================================================

TEST_CASE("add_arpeggio - undo removes all notes", "[dsl][arpeggio][undo]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=4)"
                           ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    UndoManager::getInstance().undo();
    REQUIRE(clip->midiNotes.empty());

    UndoManager::getInstance().redo();
    REQUIRE(clip->midiNotes.size() == 3);
}

TEST_CASE("add_arpeggio - undo progression removes last chord only", "[dsl][arpeggio][undo]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=2)"
                       ".notes.add_arpeggio(root=E4, quality=min, beat=0, step=0.5, beats=4)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=4, step=0.5, beats=4)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 16);

    // Undo the C major arpeggio
    UndoManager::getInstance().undo();
    REQUIRE(clip->midiNotes.size() == 8);

    // Remaining notes should all be E minor
    for (const auto& note : clip->midiNotes) {
        bool isEminor = (note.noteNumber == 64 || note.noteNumber == 67 || note.noteNumber == 71);
        REQUIRE(isEminor);
    }

    // Undo the E minor arpeggio
    UndoManager::getInstance().undo();
    REQUIRE(clip->midiNotes.empty());
}

// ============================================================================
// Error cases
// ============================================================================

TEST_CASE("add_arpeggio - missing root", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_arpeggio(quality=major, beat=0, step=0.5)");
    REQUIRE_FALSE(result);
}

TEST_CASE("add_arpeggio - missing quality", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_arpeggio(root=C4, beat=0, step=0.5)");
    REQUIRE_FALSE(result);
}

TEST_CASE("add_arpeggio - unknown quality", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_arpeggio(root=C4, quality=bogus, beat=0, step=0.5)");
    REQUIRE_FALSE(result);
}

TEST_CASE("add_arpeggio - zero step rejected", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0)");
    REQUIRE_FALSE(result);
}

TEST_CASE("add_arpeggio - negative step rejected", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".clip.new(bar=1, length_bars=4)"
                                 ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=-0.5)");
    REQUIRE_FALSE(result);
}

TEST_CASE("add_arpeggio - negative note_length rejected", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result = interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, note_length=-1)");
    REQUIRE_FALSE(result);
}

TEST_CASE("add_arpeggio - negative beats rejected", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    bool result =
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=4)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, beats=-4)");
    REQUIRE_FALSE(result);
}

TEST_CASE("add_arpeggio - no clip context", "[dsl][arpeggio][error]") {
    resetState();
    dsl::Interpreter interp;

    // Create track but no clip — arpeggio should fail
    bool result = interp.execute("track(name=\"Test\", type=\"midi\")"
                                 ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5)");
    REQUIRE_FALSE(result);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("add_arpeggio - power chord (2 notes)", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    REQUIRE(interp.execute("track(name=\"Test\", type=\"midi\")"
                           ".clip.new(bar=1, length_bars=2)"
                           ".notes.add_arpeggio(root=E2, quality=power, beat=0, step=1.0)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 2);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].first == 40);  // E2
    REQUIRE(notes[1].first == 47);  // B2
}

TEST_CASE("add_arpeggio - fill with beat offset", "[dsl][arpeggio][fill]") {
    resetState();
    dsl::Interpreter interp;

    // Start at beat 2 and fill to end of 1-bar (4-beat) clip
    // Available space: beats 2-4, step=0.5 → 4 notes
    REQUIRE(
        interp.execute("track(name=\"Test\", type=\"midi\")"
                       ".clip.new(bar=1, length_bars=1)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=2, step=0.5, fill=true)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 4);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].second == Catch::Approx(2.0));
    REQUIRE(notes[3].second == Catch::Approx(3.5));
}

TEST_CASE("add_arpeggio - updown with triad (3 notes)", "[dsl][arpeggio]") {
    resetState();
    dsl::Interpreter interp;

    // updown with 3-note chord: C E G E → 4 notes (skip top and bottom on way down)
    REQUIRE(interp.execute(
        "track(name=\"Test\", type=\"midi\")"
        ".clip.new(bar=1, length_bars=4)"
        ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, pattern=updown)"));

    auto tracks = TrackManager::getInstance().getTracks();
    const auto* clip = getFirstClip(tracks[0].id);
    REQUIRE(clip != nullptr);
    // up: C E G, down (skip top G, skip bottom C): just E → total 4
    REQUIRE(clip->midiNotes.size() == 4);

    auto notes = getNotesByBeat(clip);
    REQUIRE(notes[0].first == 60);  // C4
    REQUIRE(notes[1].first == 64);  // E4
    REQUIRE(notes[2].first == 67);  // G4
    REQUIRE(notes[3].first == 64);  // E4
}

// ============================================================================
// track(new=true) — forced creation
// ============================================================================

TEST_CASE("add_arpeggio - new=true creates separate track", "[dsl][arpeggio][track]") {
    resetState();
    dsl::Interpreter interp;

    // Create first track
    REQUIRE(
        interp.execute("track(name=\"Synth\", type=\"midi\", new=true)"
                       ".clip.new(bar=1, length_bars=2)"
                       ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, fill=true)"));

    // Create second track with same name
    REQUIRE(
        interp.execute("track(name=\"Synth\", type=\"midi\", new=true)"
                       ".clip.new(bar=1, length_bars=2)"
                       ".notes.add_arpeggio(root=A3, quality=minor, beat=0, step=0.5, fill=true)"));

    auto tracks = TrackManager::getInstance().getTracks();
    REQUIRE(tracks.size() == 2);

    // Both should have their own clip with notes
    const auto* clip1 = getFirstClip(tracks[0].id);
    const auto* clip2 = getFirstClip(tracks[1].id);
    REQUIRE(clip1 != nullptr);
    REQUIRE(clip2 != nullptr);
    REQUIRE(clip1->midiNotes.size() == 16);
    REQUIRE(clip2->midiNotes.size() == 16);

    // First track is C major, second is A minor
    auto notes1 = getNotesByBeat(clip1);
    REQUIRE(notes1[0].first == 60);  // C4

    auto notes2 = getNotesByBeat(clip2);
    REQUIRE(notes2[0].first == 57);  // A3
}
