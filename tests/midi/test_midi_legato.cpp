#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/MidiNoteCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;
using Catch::Approx;

// Build a MIDI clip long enough that legato is never clamped by the clip end,
// with notes at the given {startBeat, lengthBeats} (all same pitch unless noted).
static ClipId makeClip(const std::vector<std::pair<double, double>>& notes, int pitch = 60) {
    auto& clipManager = ClipManager::getInstance();
    ClipId clipId =
        clipManager.createMidiClip(TrackManager::getInstance().createTrack("T", TrackType::Media),
                                   0.0, 64.0, ClipView::Arrangement);
    auto* clip = clipManager.getClip(clipId);
    for (const auto& [start, len] : notes) {
        MidiNote n;
        n.noteNumber = pitch;
        n.startBeat = start;
        n.lengthBeats = len;
        n.velocity = 100;
        clip->midiNotes.push_back(n);
    }
    return clipId;
}

static double lengthOf(ClipId clipId, size_t index) {
    return ClipManager::getInstance().getClip(clipId)->midiNotes[index].lengthBeats;
}

TEST_CASE("Legato lengths", "[midi][legato]") {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();
    clipManager.clearAllClips();
    trackManager.clearAllTracks();

    SECTION("each note stretches to the next onset") {
        // Notes at 0, 1, 2 with short lengths; expect 0->1, 1->2 filled, last untouched.
        ClipId clip = makeClip({{0.0, 0.25}, {1.0, 0.25}, {2.0, 0.25}});
        auto out = computeLegatoNoteLengths(*clipManager.getClip(clip), {0, 1, 2});

        // Two notes change (indices 0 and 1); the trailing note (2) is left alone.
        REQUIRE(out.size() == 2);
        std::sort(out.begin(), out.end());
        CHECK(out[0].first == 0);
        CHECK(out[0].second == Approx(1.0));
        CHECK(out[1].first == 1);
        CHECK(out[1].second == Approx(1.0));
    }

    SECTION("gaps are closed and overlaps are trimmed") {
        // Note 0 too short (gap), note 1 too long (overlaps next onset).
        ClipId clip = makeClip({{0.0, 0.5}, {2.0, 3.0}, {4.0, 1.0}});
        auto out = computeLegatoNoteLengths(*clipManager.getClip(clip), {0, 1, 2});
        std::sort(out.begin(), out.end());
        REQUIRE(out.size() == 2);
        CHECK(out[0].second == Approx(2.0));  // 0 -> next onset at 2
        CHECK(out[1].second == Approx(2.0));  // 1 (was 3.0) -> next onset at 4
    }

    SECTION("chord notes on the same onset all extend to the next onset") {
        // Two notes at beat 0 (a chord), one at beat 2.
        ClipId clip = makeClip({{0.0, 0.25}, {2.0, 0.25}});
        auto* clipPtr = clipManager.getClip(clip);
        MidiNote chordMate;
        chordMate.noteNumber = 64;
        chordMate.startBeat = 0.0;
        chordMate.lengthBeats = 0.25;
        chordMate.velocity = 100;
        clipPtr->midiNotes.insert(clipPtr->midiNotes.begin() + 1, chordMate);
        // Now indices: 0@0, 1@0 (chord), 2@2.
        auto out = computeLegatoNoteLengths(*clipPtr, {0, 1, 2});
        std::sort(out.begin(), out.end());
        REQUIRE(out.size() == 2);
        CHECK(out[0].second == Approx(2.0));  // index 0 -> 2
        CHECK(out[1].second == Approx(2.0));  // index 1 (chord mate) -> 2, not 0
    }

    SECTION("already-legato notes produce no change") {
        ClipId clip = makeClip({{0.0, 1.0}, {1.0, 1.0}, {2.0, 1.0}});
        auto out = computeLegatoNoteLengths(*clipManager.getClip(clip), {0, 1, 2});
        CHECK(out.empty());
    }

    SECTION("fewer than two notes is a no-op") {
        ClipId clip = makeClip({{0.0, 0.25}});
        CHECK(computeLegatoNoteLengths(*clipManager.getClip(clip), {0}).empty());
        CHECK(computeLegatoNoteLengths(*clipManager.getClip(clip), {}).empty());
    }

    SECTION("applied via ResizeMultipleMidiNotesCommand, undoable") {
        ClipId clip = makeClip({{0.0, 0.25}, {1.0, 0.25}});
        auto newLengths = computeLegatoNoteLengths(*clipManager.getClip(clip), {0, 1});
        REQUIRE(newLengths.size() == 1);
        auto cmd = std::make_unique<ResizeMultipleMidiNotesCommand>(clip, std::move(newLengths));
        cmd->execute();
        CHECK(lengthOf(clip, 0) == Approx(1.0));
        cmd->undo();
        CHECK(lengthOf(clip, 0) == Approx(0.25));
    }
}
