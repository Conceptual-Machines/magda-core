#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/MidiNoteCommands.hpp"

using namespace magda;
using Catch::Approx;

static MidiNote makeNote(int pitch, double start, double length, EventId id) {
    MidiNote note;
    note.noteNumber = pitch;
    note.startBeat = start;
    note.lengthBeats = length;
    note.id = id;
    return note;
}

TEST_CASE("Split a note at a beat", "[midi][split]") {
    MidiEventState state;
    state.notes = {makeNote(60, 1.0, 2.0, 1)};
    state.notes[0].pitchExpression = {{0.5, 1.0}, {1.5, -1.0}};
    state.nextEventId = 2;

    SECTION("inside the note gives a head and a tail") {
        auto after = splitMidiNoteAt(state, 0, 2.0);
        REQUIRE(after.has_value());
        REQUIRE(after->notes.size() == 2);
        CHECK(after->notes[0].startBeat == Approx(1.0));
        CHECK(after->notes[0].lengthBeats == Approx(1.0));
        CHECK(after->notes[1].startBeat == Approx(2.0));
        CHECK(after->notes[1].lengthBeats == Approx(1.0));
        CHECK(after->notes[1].id == 2);
        CHECK(after->nextEventId == 3);
        REQUIRE(after->notes[0].pitchExpression.size() == 1);
        REQUIRE(after->notes[1].pitchExpression.size() == 1);
        CHECK(after->notes[1].pitchExpression[0].beat == Approx(0.5));
    }

    SECTION("on an edge or outside does nothing") {
        CHECK_FALSE(splitMidiNoteAt(state, 0, 1.0).has_value());
        CHECK_FALSE(splitMidiNoteAt(state, 0, 3.0).has_value());
        CHECK_FALSE(splitMidiNoteAt(state, 0, 4.0).has_value());
        CHECK_FALSE(splitMidiNoteAt(state, 5, 2.0).has_value());
    }
}

TEST_CASE("Glue a note to the next one of the same pitch", "[midi][glue]") {
    MidiEventState state;
    state.notes = {makeNote(60, 3.0, 1.0, 1), makeNote(62, 1.5, 1.0, 2), makeNote(60, 0.0, 1.0, 3),
                   makeNote(60, 6.0, 1.0, 4)};

    SECTION("joins the nearest later same-pitch note") {
        auto after = glueMidiNoteToNext(state, 2);
        REQUIRE(after.has_value());
        REQUIRE(after->notes.size() == 3);
        const auto joinedIt = std::ranges::find(after->notes, EventId{3}, &MidiNote::id);
        REQUIRE(joinedIt != after->notes.end());
        const auto& joined = *joinedIt;
        CHECK(joined.startBeat == Approx(0.0));
        CHECK(joined.lengthBeats == Approx(4.0));
        CHECK(std::ranges::none_of(after->notes, [](const MidiNote& n) { return n.id == 1; }));
    }

    SECTION("the last note of a pitch has nothing to join") {
        CHECK_FALSE(glueMidiNoteToNext(state, 3).has_value());
        CHECK_FALSE(glueMidiNoteToNext(state, 1).has_value());
    }
}
