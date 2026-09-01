#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "clip/ClipSnapshot.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/ClipSnapshotDump.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "core/ClipFades.hpp"
#include "core/TimeStretchModes.hpp"
#include "transport/TempoMap.hpp"

using Catch::Approx;
using magda::AudioEvent;
using magda::ClipInfo;
using magda::ClipView;
using magda::FadeCurve;
using magda::engine::ClipLane;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSourceInfo;
using magda::engine::compileClipSnapshot;
using magda::engine::dumpClipSnapshot;
using magda::engine::TempoMap;

namespace {

constexpr magda::TrackId kTrack = 1;
constexpr magda::SourceId kSource = 7;

/// 120 bpm in 4/4 from the beginning: one beat is half a second, which is what
/// every seconds assertion below is counted in.
TempoMap makeTempoMap(double bpm = 120.0) {
    return TempoMap({{0.0, bpm, 0.0f}}, {{0.0, 4, 4}});
}

std::vector<ClipSourceInfo> makeSources() {
    return {ClipSourceInfo{kSource, "/tmp/magda-fixtures/loop.wav", 48000.0, 4.0}};
}

ClipInfo makeAudioClip(magda::ClipId id, double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = kTrack;
    clip.view = ClipView::Arrangement;
    clip.name = "Clip " + juce::String(id);
    clip.setAudioContent();

    AudioEvent event;
    event.sourceId = kSource;
    event.interpBpm = 120.0;
    clip.audio().addEvent(event);

    clip.setPlacementBeats(startBeat, lengthBeats);
    return clip;
}

ClipInfo makeMidiClip(magda::ClipId id, double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = kTrack;
    clip.view = ClipView::Arrangement;
    clip.name = "Clip " + juce::String(id);
    clip.setMidiContent();
    clip.setPlacementBeats(startBeat, lengthBeats);
    return clip;
}

AudioEvent& eventOf(ClipInfo& clip) {
    return clip.audio().events.front();
}

ClipSnapshot compile(std::vector<ClipInfo> clips, const TempoMap& tempoMap) {
    return compileClipSnapshot({ClipLane{kTrack, std::move(clips)}}, makeSources(), tempoMap);
}

/// What the fixture in the golden test has to compile to, line for line. A diff
/// here is a change in what a track plays.
const std::string kGoldenDump =
    "magda-clip-snapshot v1\n"
    "tempo=a95350905fc122eb tracks=1\n"
    "track 1 audio=2 midi=1\n"
    "  audio clip=1 span=0.000..8.000b 0.000..4.000s fade=0.000/1.000 curve=lin/lin "
    "behaviour=0/0 gain=0.0 pan=0.00 launch=256\n"
    "    event 1 src=7 file=loop.wav rate=48000 span=0.000..8.000b 0.000..4.000s anchor=0 "
    "loop=off stretch=0 speed=1.000 bpm=120.0\n"
    "  audio clip=2 span=6.000..14.000b 3.000..7.000s fade=1.000/0.000 curve=lin/lin "
    "behaviour=0/0 gain=0.0 pan=0.00 launch=256\n"
    "    event 1 src=7 file=loop.wav rate=48000 span=6.000..14.000b 3.000..7.000s anchor=0 "
    "loop=off stretch=0 speed=1.000 bpm=120.0 reversed\n"
    "  midi clip=3 span=0.000..4.000b 0.000..2.000s events=2 on=1 off=1 ctl=0 loop=off "
    "offset=0.000 trim=0.000\n";

const magda::engine::AudioClipPlayback* audioClip(const ClipSnapshot& snapshot, magda::ClipId id) {
    const auto* track = snapshot.find(kTrack);
    if (track == nullptr)
        return nullptr;
    for (const auto& clip : track->audio)
        if (clip.clipId == id)
            return &clip;
    return nullptr;
}

}  // namespace

TEST_CASE("A clip on its own compiles to its placement, in both domains", "[engine][clip]") {
    const auto tempoMap = makeTempoMap();
    const auto snapshot = compile({makeAudioClip(1, 4.0, 8.0)}, tempoMap);

    REQUIRE(snapshot.tracks.size() == 1);
    const auto* clip = audioClip(snapshot, 1);
    REQUIRE(clip != nullptr);

    CHECK(clip->span.startBeat == Approx(4.0));
    CHECK(clip->span.endBeat == Approx(12.0));
    CHECK(clip->span.startSeconds == Approx(2.0));
    CHECK(clip->span.endSeconds == Approx(6.0));
    CHECK(clip->silenced.empty());

    REQUIRE(clip->events.size() == 1);
    const auto& event = clip->events.front();
    CHECK(event.sourceId == kSource);
    CHECK(event.sourceSampleRate == Approx(48000.0));
    CHECK(event.span.startSeconds == Approx(2.0));
    CHECK(event.span.endSeconds == Approx(6.0));
}

TEST_CASE("The seconds come from the tempo map, and say which one", "[engine][clip]") {
    const auto slow = makeTempoMap(60.0);
    const auto fast = makeTempoMap(120.0);

    const auto atSixty = compile({makeAudioClip(1, 4.0, 8.0)}, slow);
    const auto atOneTwenty = compile({makeAudioClip(1, 4.0, 8.0)}, fast);

    CHECK(audioClip(atSixty, 1)->span.startSeconds == Approx(4.0));
    CHECK(audioClip(atOneTwenty, 1)->span.startSeconds == Approx(2.0));

    // The beats did not move, only what they are worth.
    CHECK(audioClip(atSixty, 1)->span.startBeat == Approx(4.0));
    CHECK(atSixty.tempoFingerprint != atOneTwenty.tempoFingerprint);
    CHECK(atSixty.tempoFingerprint == slow.fingerprint());
}

TEST_CASE("A covered clip plays what the lane leaves it", "[engine][clip]") {
    SECTION("covered end to end, it is not in the snapshot at all") {
        auto under = makeAudioClip(1, 0.0, 4.0);
        auto over = makeAudioClip(2, 0.0, 8.0);
        over.stackOrder = 1;

        const auto snapshot = compile({under, over}, makeTempoMap());
        CHECK(audioClip(snapshot, 1) == nullptr);
        CHECK(audioClip(snapshot, 2) != nullptr);
        // Being covered is what covering means, not a failure to compile.
        CHECK(snapshot.diagnostics.empty());
    }

    SECTION("covered at an edge, the edge pulls in") {
        auto under = makeAudioClip(1, 0.0, 8.0);
        auto over = makeAudioClip(2, 4.0, 8.0);
        over.stackOrder = 1;

        const auto snapshot = compile({under, over}, makeTempoMap());
        const auto* clip = audioClip(snapshot, 1);
        REQUIRE(clip != nullptr);
        CHECK(clip->span.startBeat == Approx(0.0));
        CHECK(clip->span.endBeat == Approx(4.0));
        CHECK(clip->span.endSeconds == Approx(2.0));
        CHECK(clip->silenced.empty());
    }

    SECTION("covered in the middle, an audio clip keeps a hole") {
        auto under = makeAudioClip(1, 0.0, 16.0);
        auto over = makeAudioClip(2, 4.0, 4.0);
        over.stackOrder = 1;

        const auto snapshot = compile({under, over}, makeTempoMap());
        const auto* clip = audioClip(snapshot, 1);
        REQUIRE(clip != nullptr);
        CHECK(clip->span.startBeat == Approx(0.0));
        CHECK(clip->span.endBeat == Approx(16.0));
        REQUIRE(clip->silenced.size() == 1);
        CHECK(clip->silenced.front().startBeat == Approx(4.0));
        CHECK(clip->silenced.front().endBeat == Approx(8.0));
        CHECK(clip->silenced.front().startSeconds == Approx(2.0));
        CHECK(clip->silenced.front().endSeconds == Approx(4.0));

        // The event is NOT cropped with it: the anchor is heard at the event's
        // own start, and moving that would move every read derived from it.
        REQUIRE(clip->events.size() == 1);
        CHECK(clip->events.front().span.startBeat == Approx(0.0));
        CHECK(clip->events.front().span.endBeat == Approx(16.0));
    }

    SECTION("play-through leaves both clips whole") {
        auto under = makeAudioClip(1, 0.0, 8.0);
        auto over = makeAudioClip(2, 4.0, 8.0);
        over.stackOrder = 1;
        under.overlapPlaysBoth = true;  // either side asking is enough

        const auto snapshot = compile({under, over}, makeTempoMap());
        CHECK(audioClip(snapshot, 1)->span.endBeat == Approx(8.0));
        CHECK(audioClip(snapshot, 2)->span.startBeat == Approx(4.0));
    }
}

TEST_CASE("Fades arrive resolved, and a crossfade is two of them", "[engine][clip]") {
    auto left = makeAudioClip(1, 0.0, 8.0);
    auto right = makeAudioClip(2, 6.0, 8.0);
    right.stackOrder = 1;
    left.overlapPlaysBoth = true;
    left.autoCrossfade = true;
    right.autoCrossfade = true;

    const auto snapshot = compile({left, right}, makeTempoMap());

    // Two beats of overlap at 120 bpm is one second, and each clip plays the
    // one curve that is its own.
    CHECK(audioClip(snapshot, 1)->fadeOutSeconds == Approx(1.0));
    CHECK(audioClip(snapshot, 1)->fadeInSeconds == Approx(0.0));
    CHECK(audioClip(snapshot, 2)->fadeInSeconds == Approx(1.0));
    CHECK(audioClip(snapshot, 2)->fadeOutSeconds == Approx(0.0));

    // The same answer the arrangement draws from.
    const std::vector<ClipInfo> lane{left, right};
    CHECK(magda::effectiveFadesIn(lane, 2, 120.0).fadeInSeconds ==
          Approx(audioClip(snapshot, 2)->fadeInSeconds));
}

TEST_CASE("Both halves of a crossfade agree across a tempo change", "[engine][clip]") {
    // 120 bpm until beat 8, 60 bpm after it: the two clips start under
    // different tempos, and the overlap they share sits across the change.
    const TempoMap tempoMap({{0.0, 120.0, 0.0f}, {8.0, 60.0, 0.0f}}, {{0.0, 4, 4}});

    auto left = makeAudioClip(1, 0.0, 10.0);
    auto right = makeAudioClip(2, 6.0, 8.0);
    right.stackOrder = 1;
    left.overlapPlaysBoth = true;
    left.autoCrossfade = true;
    right.autoCrossfade = true;

    const auto snapshot = compile({left, right}, tempoMap);

    // One overlap, one length: beats 6 to 10 through this map, and not
    // whatever the bpm at either clip's start would have priced it at.
    const double overlapSeconds = tempoMap.beatToTime(10.0) - tempoMap.beatToTime(6.0);
    CHECK(audioClip(snapshot, 1)->fadeOutSeconds == Approx(overlapSeconds));
    CHECK(audioClip(snapshot, 2)->fadeInSeconds == Approx(overlapSeconds));

    // Which is not what one bpm reading gives: the fade crosses the change.
    CHECK(overlapSeconds != Approx(4.0 * 60.0 / 120.0));
    CHECK(overlapSeconds != Approx(4.0 * 60.0 / 60.0));

    // And it agrees with the spans it sits inside.
    CHECK(audioClip(snapshot, 2)->span.startSeconds == Approx(tempoMap.beatToTime(6.0)));
    CHECK(audioClip(snapshot, 1)->span.endSeconds == Approx(tempoMap.beatToTime(10.0)));
}

TEST_CASE("Fades that outrun their clip are scaled to fit it", "[engine][clip]") {
    auto clip = makeAudioClip(1, 0.0, 2.0);  // one second at 120 bpm
    eventOf(clip).fadeInSeconds = 3.0;
    eventOf(clip).fadeOutSeconds = 1.0;

    const auto snapshot = compile({clip}, makeTempoMap());
    const auto* compiled = audioClip(snapshot, 1);
    REQUIRE(compiled != nullptr);
    CHECK(compiled->fadeInSeconds + compiled->fadeOutSeconds == Approx(1.0));
    CHECK(compiled->fadeInSeconds == Approx(0.75));
    CHECK(compiled->fadeOutSeconds == Approx(0.25));
}

TEST_CASE("Every event keeps its own fades, not just the primary", "[engine][clip]") {
    auto clip = makeAudioClip(1, 0.0, 8.0);
    eventOf(clip).lengthBeats = 4.0;
    eventOf(clip).fadeInSeconds = 0.5;
    eventOf(clip).fadeOutSeconds = 0.25;
    eventOf(clip).fadeInType = static_cast<int>(FadeCurve::Concave);

    AudioEvent second;
    second.sourceId = kSource;
    second.interpBpm = 120.0;
    second.startBeat = 4.0;
    second.lengthBeats = 4.0;
    second.fadeInSeconds = 0.125;
    second.fadeOutSeconds = 1.5;
    second.fadeInType = static_cast<int>(FadeCurve::SCurve);
    second.fadeOutBehaviour = 1;  // speed ramp
    clip.audio().addEvent(second);

    const auto snapshot = compile({clip}, makeTempoMap());
    const auto* compiled = audioClip(snapshot, 1);
    REQUIRE(compiled != nullptr);
    REQUIRE(compiled->events.size() == 2);

    CHECK(compiled->events[0].fadeInSeconds == Approx(0.5));
    CHECK(compiled->events[0].fadeOutSeconds == Approx(0.25));
    CHECK(compiled->events[0].fadeInCurve == FadeCurve::Concave);

    // The event the clip-level pair says nothing about.
    CHECK(compiled->events[1].fadeInSeconds == Approx(0.125));
    CHECK(compiled->events[1].fadeOutSeconds == Approx(1.5));
    CHECK(compiled->events[1].fadeInCurve == FadeCurve::SCurve);
    CHECK(compiled->events[1].fadeOutBehaviour == 1);

    // The clip's own edges still play the primary event's, resolved.
    CHECK(compiled->fadeInSeconds == Approx(0.5));
    CHECK(compiled->fadeOutSeconds == Approx(0.25));
    CHECK(compiled->fadeInCurve == FadeCurve::Concave);
}

TEST_CASE("A clip that does not belong to the lane cannot cover one that does", "[engine][clip]") {
    SECTION("a session clip sitting on top of an arrangement clip") {
        auto arrangement = makeAudioClip(1, 0.0, 8.0);
        auto session = makeAudioClip(2, 0.0, 8.0);
        session.view = ClipView::Session;
        session.stackOrder = 1;

        const auto snapshot = compile({arrangement, session}, makeTempoMap());
        const auto* clip = audioClip(snapshot, 1);
        REQUIRE(clip != nullptr);
        CHECK(clip->span.startBeat == Approx(0.0));
        CHECK(clip->span.endBeat == Approx(8.0));
        CHECK(clip->silenced.empty());
        CHECK(snapshot.diagnostics.size() == 1);
    }

    SECTION("a clip from another track sitting on top of it") {
        auto mine = makeAudioClip(1, 0.0, 16.0);
        auto stranger = makeAudioClip(2, 4.0, 4.0);
        stranger.trackId = kTrack + 1;
        stranger.stackOrder = 1;

        const auto snapshot = compile({mine, stranger}, makeTempoMap());
        const auto* clip = audioClip(snapshot, 1);
        REQUIRE(clip != nullptr);
        CHECK(clip->span.endBeat == Approx(16.0));
        // The hole it would have punched is not there.
        CHECK(clip->silenced.empty());
        CHECK(snapshot.diagnostics.size() == 1);
    }
}

TEST_CASE("A clip's own fade and its curve survive the compile", "[engine][clip]") {
    auto clip = makeAudioClip(1, 0.0, 8.0);
    eventOf(clip).fadeInSeconds = 0.75;
    eventOf(clip).fadeOutSeconds = 0.25;
    eventOf(clip).fadeInType = static_cast<int>(FadeCurve::SCurve);
    eventOf(clip).fadeOutType = static_cast<int>(FadeCurve::Convex);
    eventOf(clip).fadeOutBehaviour = 1;  // speed ramp

    const auto snapshot = compile({clip}, makeTempoMap());
    const auto* compiled = audioClip(snapshot, 1);
    REQUIRE(compiled != nullptr);
    CHECK(compiled->fadeInSeconds == Approx(0.75));
    CHECK(compiled->fadeOutSeconds == Approx(0.25));
    CHECK(compiled->fadeInCurve == FadeCurve::SCurve);
    CHECK(compiled->fadeOutCurve == FadeCurve::Convex);
    CHECK(compiled->fadeOutBehaviour == 1);
}

TEST_CASE("Clip volume and gain reach the engine summed, as the incumbent applies them",
          "[engine][clip]") {
    auto clip = makeAudioClip(1, 0.0, 8.0);
    clip.volumeDB = -6.0f;
    clip.gainDB = 2.0f;
    clip.pan = -0.5f;
    eventOf(clip).gainDB = 1.5f;

    const auto snapshot = compile({clip}, makeTempoMap());
    const auto* compiled = audioClip(snapshot, 1);
    REQUIRE(compiled != nullptr);
    CHECK(compiled->gainDb == Approx(-4.0f));
    CHECK(compiled->pan == Approx(-0.5f));
    // The event's own trim stays under the clip's, rather than being folded in.
    CHECK(compiled->events.front().gainDb == Approx(1.5f));
}

TEST_CASE("The stretch mode compiled is the one that runs", "[engine][clip]") {
    auto clip = makeAudioClip(1, 0.0, 8.0);
    eventOf(clip).timeStretchMode = magda::time_stretch_mode::kDisabled;
    eventOf(clip).autoTempo = true;

    const auto snapshot = compile({clip}, makeTempoMap());
    const auto* compiled = audioClip(snapshot, 1);
    REQUIRE(compiled != nullptr);
    // Off plus beat mode still stretches, which is what the model already says
    // and what the UI already shows.
    CHECK(compiled->events.front().timeStretchMode == magda::time_stretch_mode::kSignalsmith);
    CHECK(compiled->events.front().autoTempo);
}

TEST_CASE("What will not sound says so, and what is merely covered does not", "[engine][clip]") {
    SECTION("a disabled clip is skipped without comment") {
        auto clip = makeAudioClip(1, 0.0, 8.0);
        clip.enabled = false;

        const auto snapshot = compile({clip}, makeTempoMap());
        CHECK(snapshot.tracks.empty());
        CHECK(snapshot.diagnostics.empty());
    }

    SECTION("a session clip in an arrangement lane is reported") {
        auto clip = makeAudioClip(1, 0.0, 8.0);
        clip.view = ClipView::Session;

        const auto snapshot = compile({clip}, makeTempoMap());
        CHECK(snapshot.tracks.empty());
        REQUIRE(snapshot.diagnostics.size() == 1);
        CHECK(snapshot.diagnostics.front().find("session clip") != std::string::npos);
    }

    SECTION("an unresolvable source is reported, and takes the clip with it") {
        auto clip = makeAudioClip(1, 0.0, 8.0);
        eventOf(clip).sourceId = 99;

        const auto snapshot = compile({clip}, makeTempoMap());
        // Nothing left to read, so nothing to carry: the same shape a clip with
        // no events at all comes out as, rather than an empty entry that would
        // cost a voice to discover.
        CHECK(audioClip(snapshot, 1) == nullptr);
        CHECK(snapshot.tracks.empty());
        REQUIRE(snapshot.diagnostics.size() == 1);
        CHECK(snapshot.diagnostics.front().find("source table") != std::string::npos);
    }

    SECTION("an event that plays backwards over a source of unknown length is reported") {
        // Playing backwards reads the file from its far end, so where that end
        // is has to be known. Forwards does not care what is behind it.
        auto clip = makeAudioClip(1, 0.0, 8.0);
        eventOf(clip).reversed = true;

        const std::vector<ClipSourceInfo> unmeasured{
            ClipSourceInfo{kSource, "/tmp/magda-fixtures/loop.wav", 48000.0, 0.0}};

        const auto snapshot =
            compileClipSnapshot({ClipLane{kTrack, {clip}}}, unmeasured, makeTempoMap());

        CHECK(snapshot.tracks.empty());
        REQUIRE(snapshot.diagnostics.size() == 1);
        CHECK(snapshot.diagnostics.front().find("turn about") != std::string::npos);
    }

    SECTION("an audio clip with no events is reported the same way") {
        auto clip = makeAudioClip(1, 0.0, 8.0);
        clip.audio().events.clear();

        const auto snapshot = compile({clip}, makeTempoMap());
        CHECK(snapshot.tracks.empty());
        REQUIRE(snapshot.diagnostics.size() == 1);
        CHECK(snapshot.diagnostics.front().find("no events") != std::string::npos);
    }

    SECTION("a clip that belongs to another track is reported") {
        auto clip = makeAudioClip(1, 0.0, 8.0);
        clip.trackId = kTrack + 1;

        const auto snapshot = compile({clip}, makeTempoMap());
        CHECK(snapshot.tracks.empty());
        REQUIRE(snapshot.diagnostics.size() == 1);
    }
}

TEST_CASE("A MIDI clip carries its events, its loop and its groove", "[engine][clip]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiNotes.push_back(magda::MidiNote{60, 100, 0.0, 1.0, 0, {}});
    clip.midiNotes.push_back(magda::MidiNote{64, 90, 1.0, 1.0, 0, {}});
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 4.0;
    clip.midiOffset = 0.5;
    clip.midiTrimOffset = 0.25;
    clip.grooveTemplate = "Swing 16";
    clip.grooveStrength = 0.6f;

    const auto snapshot = compile({clip}, makeTempoMap());
    const auto* track = snapshot.find(kTrack);
    REQUIRE(track != nullptr);
    REQUIRE(track->midi.size() == 1);

    const auto& midi = track->midi.front();
    // Two notes are four messages: the model's lists are compiled into what
    // they play rather than carried for the audio thread to interpret.
    CHECK(midi.events.events.size() == 4);
    CHECK(midi.fold.loopEnabled);
    CHECK(midi.fold.loopLengthBeats == Approx(4.0));
    CHECK(midi.fold.offsetBeats == Approx(0.5));
    CHECK(midi.fold.trimOffsetBeats == Approx(0.25));

    // The groove is named but this installation has none, which is an ordinary
    // answer and a diagnostic rather than a failure.
    CHECK(midi.groove.empty());
    REQUIRE(snapshot.diagnostics.size() == 1);
    CHECK(snapshot.diagnostics.front().find("Swing 16") != std::string::npos);
    CHECK(track->audio.empty());
}

TEST_CASE("A MIDI clip covered in the middle keeps the hole", "[engine][clip]") {
    auto under = makeMidiClip(1, 0.0, 16.0);
    auto over = makeMidiClip(2, 4.0, 4.0);
    over.stackOrder = 1;

    const auto snapshot = compile({under, over}, makeTempoMap());
    const auto* track = snapshot.find(kTrack);
    REQUIRE(track != nullptr);
    REQUIRE(track->midi.size() == 2);

    const auto& covered = track->midi.front();
    CHECK(covered.clipId == 1);
    REQUIRE(covered.silenced.size() == 1);
    CHECK(covered.silenced.front().startBeat == Approx(4.0));
    CHECK(covered.silenced.front().endBeat == Approx(8.0));
}

TEST_CASE("Compiling is deterministic whatever order the lane is held in", "[engine][clip]") {
    const auto tempoMap = makeTempoMap();
    auto first = makeAudioClip(1, 8.0, 4.0);
    auto second = makeAudioClip(2, 0.0, 4.0);
    auto third = makeMidiClip(3, 4.0, 4.0);

    const auto forwards = compile({first, second, third}, tempoMap);
    const auto backwards = compile({third, second, first}, tempoMap);

    CHECK(dumpClipSnapshot(forwards) == dumpClipSnapshot(backwards));

    // Sorted by where they start, whatever order they arrived in.
    const auto* track = forwards.find(kTrack);
    REQUIRE(track != nullptr);
    REQUIRE(track->audio.size() == 2);
    CHECK(track->audio[0].clipId == 2);
    CHECK(track->audio[1].clipId == 1);
}

TEST_CASE("A track with nothing to play is not in the snapshot", "[engine][clip]") {
    const auto snapshot = compileClipSnapshot({ClipLane{kTrack, {}}, ClipLane{kTrack + 1, {}}},
                                              makeSources(), makeTempoMap());
    CHECK(snapshot.tracks.empty());
    CHECK(snapshot.find(kTrack) == nullptr);
}

TEST_CASE("Tracks are found by id, not by position", "[engine][clip]") {
    const auto tempoMap = makeTempoMap();
    std::vector<ClipLane> lanes;
    for (magda::TrackId id : {5, 2, 9}) {
        auto clip = makeAudioClip(id * 10, 0.0, 4.0);
        clip.trackId = id;
        lanes.push_back(ClipLane{id, {clip}});
    }

    const auto snapshot = compileClipSnapshot(lanes, makeSources(), tempoMap);
    REQUIRE(snapshot.tracks.size() == 3);
    CHECK(snapshot.tracks[0].trackId == 2);
    CHECK(snapshot.find(9) != nullptr);
    CHECK(snapshot.find(9)->audio.front().clipId == 90);
    CHECK(snapshot.find(7) == nullptr);
}

TEST_CASE("The feed hands the audio thread what was last published", "[engine][clip]") {
    magda::engine::ClipSnapshotFeed feed;

    {
        magda::engine::ClipSnapshotFeed::Reader reader(feed);
        // Nothing published: a track with nothing to play, not an error.
        CHECK_FALSE(static_cast<bool>(reader));
        CHECK(reader.get() == nullptr);
    }

    feed.publish(std::make_shared<const ClipSnapshot>(
        compile({makeAudioClip(1, 0.0, 4.0)}, makeTempoMap())));
    {
        magda::engine::ClipSnapshotFeed::Reader reader(feed);
        REQUIRE(static_cast<bool>(reader));
        REQUIRE(reader->find(kTrack) != nullptr);
        CHECK(reader->find(kTrack)->audio.front().clipId == 1);
    }

    feed.publish(std::make_shared<const ClipSnapshot>(
        compile({makeAudioClip(2, 0.0, 4.0)}, makeTempoMap())));
    {
        magda::engine::ClipSnapshotFeed::Reader reader(feed);
        REQUIRE(static_cast<bool>(reader));
        CHECK(reader->find(kTrack)->audio.front().clipId == 2);
    }
}

TEST_CASE("The dump is the snapshot's golden surface", "[engine][clip]") {
    auto left = makeAudioClip(1, 0.0, 8.0);
    auto right = makeAudioClip(2, 6.0, 8.0);
    right.stackOrder = 1;
    left.overlapPlaysBoth = true;
    left.autoCrossfade = true;
    right.autoCrossfade = true;
    eventOf(right).reversed = true;

    auto midi = makeMidiClip(3, 0.0, 4.0);
    midi.midiNotes.push_back(magda::MidiNote{60, 100, 0.0, 1.0, 0, {}});

    const auto snapshot = compile({left, right, midi}, makeTempoMap());
    const auto text = dumpClipSnapshot(snapshot);

    // Nothing in here may depend on the machine that ran it: the file is its
    // name rather than its path, and every number is fixed precision.
    CHECK(text.find("/tmp/") == std::string::npos);

    INFO(text);
    CHECK(text == kGoldenDump);
}

namespace {

/// A lane whose session slots are @p slots, with nothing in its arrangement.
ClipSnapshot compileSession(std::vector<ClipInfo> slots, const TempoMap& tempoMap) {
    ClipLane lane;
    lane.trackId = kTrack;
    lane.session = std::move(slots);
    return compileClipSnapshot({lane}, makeSources(), tempoMap);
}

/// An audio clip in a scene, with the leftover placement a session clip really
/// carries: nothing writes startBeat for one, so it is whatever it was.
ClipInfo makeSessionClip(magda::ClipId id, int sceneIndex, double startBeat, double lengthBeats) {
    auto clip = makeAudioClip(id, startBeat, lengthBeats);
    clip.view = ClipView::Session;
    clip.sceneIndex = sceneIndex;
    return clip;
}

}  // namespace

TEST_CASE("A session slot is compiled at the origin, whatever its placement says",
          "[engine][clip][session]") {
    // The leftover beat is the point: the scene index is a session clip's
    // position and nothing writes the placement, so a slot compiled from what
    // the field happens to hold would start wherever the clip last was in the
    // arrangement (#2301).
    const auto snapshot = compileSession({makeSessionClip(1, 2, 18.5, 8.0)}, makeTempoMap());

    REQUIRE(snapshot.tracks.size() == 1);
    const auto& track = snapshot.tracks.front();

    CHECK(track.audio.empty());
    REQUIRE(track.session.size() == 1);

    const auto* slot = track.slot(2);
    REQUIRE(slot != nullptr);
    CHECK(slot->sceneIndex == 2);
    CHECK(slot->lengthBeats == Catch::Approx(8.0));

    REQUIRE(slot->audio.size() == 1);
    CHECK(slot->audio.front().span.startBeat == Catch::Approx(0.0));
    CHECK(slot->audio.front().span.endBeat == Catch::Approx(8.0));
}

TEST_CASE("A session slot compiles through the arrangement's own path", "[engine][clip][session]") {
    // The reason slots are compiled by recursing rather than by a second
    // implementation: a clip dragged from a slot onto the timeline has to sound
    // the same, so there is one answer about fades, events and stretch.
    const auto tempoMap = makeTempoMap();

    const auto asSlot = compileSession({makeSessionClip(1, 0, 18.5, 8.0)}, tempoMap);
    const auto asClip = compile({makeAudioClip(1, 0.0, 8.0)}, tempoMap);

    REQUIRE(asSlot.tracks.size() == 1);
    REQUIRE(asClip.tracks.size() == 1);

    const auto* slot = asSlot.tracks.front().slot(0);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->audio.size() == 1);
    REQUIRE(asClip.tracks.front().audio.size() == 1);

    const auto& fromSlot = slot->audio.front();
    const auto& fromLane = asClip.tracks.front().audio.front();

    CHECK(fromSlot.span.startBeat == Catch::Approx(fromLane.span.startBeat));
    CHECK(fromSlot.span.endBeat == Catch::Approx(fromLane.span.endBeat));
    CHECK(fromSlot.events.size() == fromLane.events.size());
    REQUIRE(!fromSlot.events.empty());
    CHECK(fromSlot.events.front().span.startSeconds ==
          Catch::Approx(fromLane.events.front().span.startSeconds));
}

TEST_CASE("Slots are found by scene and kept in scene order", "[engine][clip][session]") {
    const auto snapshot =
        compileSession({makeSessionClip(3, 5, 0.0, 4.0), makeSessionClip(1, 0, 0.0, 4.0),
                        makeSessionClip(2, 2, 0.0, 4.0)},
                       makeTempoMap());

    REQUIRE(snapshot.tracks.size() == 1);
    const auto& track = snapshot.tracks.front();
    REQUIRE(track.session.size() == 3);

    CHECK(track.session[0].sceneIndex == 0);
    CHECK(track.session[1].sceneIndex == 2);
    CHECK(track.session[2].sceneIndex == 5);

    CHECK(track.slot(2) != nullptr);
    CHECK(track.slot(5) != nullptr);

    // A scene nobody filled is not a slot, rather than an empty one a launch
    // would have to check before binding.
    CHECK(track.slot(1) == nullptr);
    CHECK(track.slot(9) == nullptr);
}

TEST_CASE("What a session lane will not play says so", "[engine][clip][session]") {
    SECTION("an arrangement clip in a session lane is reported") {
        auto clip = makeAudioClip(1, 0.0, 8.0);  // left as Arrangement

        const auto snapshot = compileSession({clip}, makeTempoMap());
        CHECK(snapshot.tracks.empty());
        REQUIRE(snapshot.diagnostics.size() == 1);
        CHECK(snapshot.diagnostics.front().find("arrangement clip") != std::string::npos);
    }

    SECTION("a session clip in no scene is reported") {
        const auto snapshot = compileSession({makeSessionClip(1, -1, 0.0, 8.0)}, makeTempoMap());
        CHECK(snapshot.tracks.empty());
        REQUIRE(snapshot.diagnostics.size() == 1);
        CHECK(snapshot.diagnostics.front().find("no scene") != std::string::npos);
    }

    SECTION("a disabled slot is skipped without comment") {
        auto clip = makeSessionClip(1, 0, 0.0, 8.0);
        clip.enabled = false;

        const auto snapshot = compileSession({clip}, makeTempoMap());
        CHECK(snapshot.tracks.empty());
        CHECK(snapshot.diagnostics.empty());
    }
}

TEST_CASE("A track carrying both views keeps them apart", "[engine][clip][session]") {
    ClipLane lane;
    lane.trackId = kTrack;
    lane.clips.push_back(makeAudioClip(1, 0.0, 8.0));
    lane.session.push_back(makeSessionClip(2, 0, 0.0, 4.0));

    const auto snapshot = compileClipSnapshot({lane}, makeSources(), makeTempoMap());

    REQUIRE(snapshot.tracks.size() == 1);
    const auto& track = snapshot.tracks.front();

    // Both, because they are not alternatives: which one sounds is decided at
    // launch rather than at compile.
    REQUIRE(track.audio.size() == 1);
    CHECK(track.audio.front().clipId == 1);
    REQUIRE(track.session.size() == 1);
    REQUIRE(track.slot(0)->audio.size() == 1);
    CHECK(track.slot(0)->audio.front().clipId == 2);
    CHECK(snapshot.diagnostics.empty());
}
