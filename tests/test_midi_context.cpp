#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/agents/midi_context.hpp"

using namespace magda;

namespace {

ClipInfo makeMidiClip(ClipId id, TrackId trackId, juce::String name, double startBeat) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = trackId;
    clip.name = std::move(name);
    clip.setMidiContent();
    clip.setPlacementBeats(startBeat, 4.0);
    return clip;
}

MidiNote note(int number, int velocity, double startBeat, double lengthBeats) {
    MidiNote value;
    value.noteNumber = number;
    value.velocity = velocity;
    value.startBeat = startBeat;
    value.lengthBeats = lengthBeats;
    return value;
}

}  // namespace

TEST_CASE("MIDI context is ordered by project track and clip order", "[agents][midi-context]") {
    test::MockMagdaApi api;

    TrackInfo lead;
    lead.id = 20;
    lead.name = "Lead";
    TrackInfo bass;
    bass.id = 10;
    bass.name = "Bass";
    api.tracks_.tracks = {lead, bass};

    auto leadClip = makeMidiClip(201, lead.id, "Lead phrase", 4.0);
    leadClip.midiNotes.push_back(note(64, 90, 1.5, 0.5));
    auto bassClip = makeMidiClip(101, bass.id, "Bass phrase", 0.0);
    bassClip.midiNotes.push_back(note(36, 110, 0.0, 1.0));
    api.clips_.clips.emplace(leadClip.id, leadClip);
    api.clips_.clips.emplace(bassClip.id, bassClip);
    api.clips_.clipsOnTrack[lead.id] = {leadClip.id};
    api.clips_.clipsOnTrack[bass.id] = {bassClip.id};

    const auto context = buildMidiContext(api, {bassClip.id, leadClip.id});

    REQUIRE(context.contains("[MIDI_CONTEXT]"));
    CHECK(context.indexOf("name=\"Lead\"") < context.indexOf("name=\"Bass\""));
    CHECK(context.contains("pitch=E4 midi=64 beat=1.5 length=0.5 velocity=90"));
    CHECK(context.contains("pitch=C2 midi=36 beat=0 length=1 velocity=110"));
}

TEST_CASE("MIDI context ignores missing and audio clips", "[agents][midi-context]") {
    test::MockMagdaApi api;
    TrackInfo track;
    track.id = 1;
    track.name = "Track";
    api.tracks_.tracks.push_back(track);

    ClipInfo audio;
    audio.id = 10;
    audio.trackId = track.id;
    audio.name = "Audio";
    audio.setAudioContent();
    api.clips_.clips.emplace(audio.id, audio);
    api.clips_.clipsOnTrack[track.id] = {audio.id};

    CHECK(buildMidiContext(api, {audio.id, 999}).isEmpty());
}

TEST_CASE("MIDI context reports note truncation", "[agents][midi-context]") {
    test::MockMagdaApi api;
    TrackInfo track;
    track.id = 1;
    track.name = "Keys";
    api.tracks_.tracks.push_back(track);

    auto clip = makeMidiClip(10, track.id, "Dense clip", 0.0);
    clip.midiNotes.push_back(note(60, 100, 0.0, 0.25));
    clip.midiNotes.push_back(note(62, 100, 0.25, 0.25));
    clip.midiNotes.push_back(note(64, 100, 0.5, 0.25));
    api.clips_.clips.emplace(clip.id, clip);
    api.clips_.clipsOnTrack[track.id] = {clip.id};

    MidiContextOptions options;
    options.maxNotesPerClip = 2;
    options.maxTotalNotes = 2;
    const auto context = buildMidiContext(api, {clip.id}, options);

    CHECK(context.contains("NOTES_TRUNCATED omitted=1"));
    CHECK_FALSE(context.contains("pitch=E4"));
}
