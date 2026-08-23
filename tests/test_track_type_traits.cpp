#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/core/TrackTypes.hpp"

/**
 * What each track type is (#2172 and what it exposed).
 *
 * The enum used to be answered by exclusion: sixteen call sites across the app
 * each kept their own list of types that could not do a thing, no two lists
 * agreed, and a file drop and a clip drag ended up holding different views of
 * where a clip was allowed to go. The table in TrackTypes.hpp replaces those
 * lists, so this pins the table.
 *
 * Written as the facts rather than as a copy of traitsOf(): asserting the
 * function against itself would pass whatever it was changed to. Each
 * expectation below is a sentence somebody could disagree with.
 */

using namespace magda;

TEST_CASE("A Media track is the one that carries material", "[track][types]") {
    const auto media = traitsOf(TrackType::Media);

    // The hybrid track, and the only one a user puts clips on directly.
    CHECK(media.hasTimeline);
    CHECK(media.acceptsUserClips);
    CHECK(media.takesExternalInput);
    CHECK(media.hostsInstrument);
    CHECK(media.occupiesArrangementRow);
    CHECK(media.isRendered);

    // It is an ordinary track: the user makes as many as they like, and
    // nothing else owns its lifetime.
    CHECK_FALSE(media.isSingleton);
    CHECK_FALSE(media.isDeviceOwned);
    CHECK_FALSE(media.canHaveChildren);

    // Nothing about the enum says what kind of material it holds, because it
    // holds both. This is the fact the old name denied: `Audio` was the
    // survivor of Instrument and MIDI collapsing into one track, and the
    // ordinals those two used are still skipped rather than reused.
    //
    // The ordinals themselves are pinned in test_persisted_enum_pins.cpp,
    // which fails the build rather than the test. What is asserted here is
    // where the retired two land, since that is a decision about meaning
    // rather than about the wire.
    CHECK(trackTypeFromInt(1) == TrackType::Media);
    CHECK(trackTypeFromInt(2) == TrackType::Media);
}

TEST_CASE("Buses carry signal from elsewhere and originate none", "[track][types]") {
    for (const auto type : {TrackType::Group, TrackType::Aux, TrackType::Master}) {
        const auto bus = traitsOf(type);
        INFO("track type " << getTrackTypeName(type));

        CHECK_FALSE(bus.hasTimeline);
        CHECK_FALSE(bus.acceptsUserClips);
        CHECK_FALSE(bus.hostsInstrument);
    }

    // Where the three differ. A group sums children it owns; an aux is fed by
    // sends and has no arrangement row of its own because it lives in the aux
    // strip; the master is the one output and takes input like a media track.
    CHECK(traitsOf(TrackType::Group).canHaveChildren);
    CHECK_FALSE(traitsOf(TrackType::Aux).canHaveChildren);

    CHECK(traitsOf(TrackType::Group).occupiesArrangementRow);
    CHECK_FALSE(traitsOf(TrackType::Aux).occupiesArrangementRow);

    CHECK(traitsOf(TrackType::Master).isSingleton);
    CHECK(traitsOf(TrackType::Master).takesExternalInput);
}

TEST_CASE("A timeline is not permission to put a clip on it", "[track][types]") {
    // The distinction the old exclusion lists kept getting wrong. Both of these
    // have lanes with clips on them, and neither takes a clip the user aims at
    // it: a chord track's clips are progressions, so an ordinary MIDI clip
    // landing there would change what the track means, and a multi-out lane is
    // erased with its device's output pair without its clips going too.
    for (const auto type : {TrackType::Chord, TrackType::MultiOut}) {
        INFO("track type " << getTrackTypeName(type));
        CHECK(traitsOf(type).hasTimeline);
        CHECK_FALSE(traitsOf(type).acceptsUserClips);
    }

    CHECK(traitsOf(TrackType::MultiOut).isDeviceOwned);
    CHECK(traitsOf(TrackType::Chord).isSingleton);

    // Monitor-only: it auditions chords and is left out of the mix.
    CHECK_FALSE(traitsOf(TrackType::Chord).isRendered);
    CHECK(traitsOf(TrackType::Media).isRendered);
}

TEST_CASE("Exactly one track type accepts a clip the user aims at it", "[track][types]") {
    // Stated as a count rather than per type, because this is the property the
    // drop, the drag and the nudge each have to agree on, and the way they
    // disagreed before was by one of them admitting one type too many.
    int accepting = 0;
    for (const auto type : {TrackType::Media, TrackType::Group, TrackType::Aux, TrackType::Master,
                            TrackType::MultiOut, TrackType::Chord})
        if (traitsOf(type).acceptsUserClips)
            ++accepting;

    CHECK(accepting == 1);
}

TEST_CASE("A track answers for itself what its type declares", "[track][types]") {
    // The predicates on TrackInfo are the same table, so a track and its type
    // can never give different answers.
    TrackInfo track;
    track.type = TrackType::Aux;
    CHECK_FALSE(track.takesExternalInput());
    CHECK_FALSE(track.canHostInstrument());
    CHECK_FALSE(track.acceptsUserClips());

    track.type = TrackType::Media;
    CHECK(track.takesExternalInput());
    CHECK(track.canHostInstrument());
    CHECK(track.acceptsUserClips());
}
