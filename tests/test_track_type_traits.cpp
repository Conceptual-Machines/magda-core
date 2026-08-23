#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipPlacementPolicy.hpp"
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
    CHECK(media.userClips == UserClipAcceptance::Any);
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
        CHECK(bus.userClips == UserClipAcceptance::None);
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

TEST_CASE("A lane can be typed without being closed", "[track][types]") {
    // The distinction the old exclusion lists could not express, so they
    // refused both of these outright. Each has a lane, and each takes one
    // particular thing rather than anything aimed at it.

    // MIDI on a multi-out lane plays the parent instrument, which is what
    // MidiInputRouter already does with live input arriving there. Audio has
    // nothing to do on one.
    CHECK(traitsOf(TrackType::MultiOut).hasTimeline);
    CHECK(traitsOf(TrackType::MultiOut).userClips == UserClipAcceptance::MidiOnly);
    CHECK(traitsOf(TrackType::MultiOut).isDeviceOwned);

    // The chord track holds progressions. Dropping one on it is the obvious
    // gesture and has to work; an ordinary MIDI clip would change what the
    // track means.
    CHECK(traitsOf(TrackType::Chord).hasTimeline);
    CHECK(traitsOf(TrackType::Chord).userClips == UserClipAcceptance::Progressions);
    CHECK(traitsOf(TrackType::Chord).isSingleton);

    // Monitor-only: it auditions chords and is left out of the mix.
    CHECK_FALSE(traitsOf(TrackType::Chord).isRendered);
    CHECK(traitsOf(TrackType::Media).isRendered);
}

TEST_CASE("Only the Media track takes whatever is aimed at it", "[track][types]") {
    // The property the drop, the drag and the nudge each have to agree on. The
    // way they disagreed before was by one of them admitting a type the others
    // refused, so this is stated over every type rather than for one.
    for (const auto type : {TrackType::Media, TrackType::Group, TrackType::Aux, TrackType::Master,
                            TrackType::MultiOut, TrackType::Chord}) {
        INFO("track type " << getTrackTypeName(type));
        const bool takesEverything = traitsOf(type).userClips == UserClipAcceptance::Any;
        CHECK(takesEverything == (type == TrackType::Media));
    }
}

TEST_CASE("A track answers for itself what its type declares", "[track][types]") {
    // The predicates on TrackInfo are the same table, so a track and its type
    // can never give different answers.
    TrackInfo track;
    track.type = TrackType::Aux;
    CHECK_FALSE(track.takesExternalInput());
    CHECK_FALSE(track.canHostInstrument());
    CHECK_FALSE(track.acceptsAnyUserClip());
    CHECK_FALSE(track.acceptsUserClip(ClipType::Audio));
    CHECK_FALSE(track.acceptsUserClip(ClipType::MIDI));

    track.type = TrackType::Media;
    CHECK(track.takesExternalInput());
    CHECK(track.canHostInstrument());
    CHECK(track.acceptsUserClip(ClipType::Audio));
    CHECK(track.acceptsUserClip(ClipType::MIDI));

    // The kind is the question, not the track. A multi-out lane takes the MIDI
    // that plays its parent instrument and refuses the audio that has nowhere
    // to go, and answering yes or no for the whole track could say neither.
    track.type = TrackType::MultiOut;
    CHECK(track.acceptsUserClip(ClipType::MIDI));
    CHECK_FALSE(track.acceptsUserClip(ClipType::Audio));

    // A progression is a MIDI clip, so the chord lane says yes to MIDI. What
    // makes the lane typed is what the import does with it -- a .mid without
    // CHORD: markers has its chords detected on the way in -- rather than a
    // refusal at the door.
    track.type = TrackType::Chord;
    CHECK(track.acceptsUserClip(ClipType::MIDI));
    CHECK_FALSE(track.acceptsUserClip(ClipType::Audio));
}

TEST_CASE("Moving a clip asks what the clip is, not only what kind", "[track][types]") {
    // The gap a kind-only check leaves. A chord lane accepts MIDI by kind,
    // because a progression is a MIDI clip -- so a check that stopped at the
    // kind would let any MIDI clip be dragged or nudged onto the chord track,
    // where nothing would turn it into harmony. Detection happens on import,
    // and a move is not an import.
    TrackInfo chordTrack;
    chordTrack.type = TrackType::Chord;

    ClipInfo plainMidi;
    plainMidi.content = MidiClipModel{};
    CHECK(chordTrack.acceptsUserClip(ClipType::MIDI));     // by kind, yes
    CHECK_FALSE(trackAcceptsClip(chordTrack, plainMidi));  // with the clip in hand, no

    ClipInfo progression;
    progression.content = MidiClipModel{};
    progression.chordAnnotations.push_back({});
    CHECK(trackAcceptsClip(chordTrack, progression));

    // A multi-out lane has no such distinction: MIDI is MIDI there.
    TrackInfo multiOut;
    multiOut.type = TrackType::MultiOut;
    CHECK(trackAcceptsClip(multiOut, plainMidi));

    ClipInfo audio;
    audio.content = AudioClipModel{};
    CHECK_FALSE(trackAcceptsClip(multiOut, audio));

    // And a bus takes neither, whatever the clip turns out to be.
    TrackInfo group;
    group.type = TrackType::Group;
    CHECK_FALSE(trackAcceptsClip(group, audio));
    CHECK_FALSE(trackAcceptsClip(group, progression));
}
