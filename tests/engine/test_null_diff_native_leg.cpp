#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <string>

#include "NullDiffGain.hpp"
#include "NullDiffNativeLeg.hpp"

/**
 * The native leg of the null-diff corpus, on its own (#2040).
 *
 * A leg that returns silence makes every case it touches compare two silences,
 * and two silences null perfectly. So before either leg is compared against the
 * other, each has to be caught lying about itself. This is that check for the
 * native one: every case renders, renders something, and renders it without the
 * engine reporting that it dropped anything on the way.
 *
 * It runs in the model-only target because the native engine needs no Edit. The
 * incumbent leg gets the same treatment in the JUCE target, where it can be
 * caught rendering before its proxies arrived.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_null_diff_native_leg");
    root.createDirectory();
    return root;
}

double peakOf(const juce::AudioBuffer<float>& buffer) {
    return buffer.getNumSamples() > 0
               ? static_cast<double>(buffer.getMagnitude(0, buffer.getNumSamples()))
               : 0.0;
}

}  // namespace

TEST_CASE("Every case renders through the native engine", "[nulldiff][native]") {
    for (const auto& value : sharedCorpus(scratch())) {
        INFO(value.name);

        const auto rendered = renderNative(value);

        CHECK(rendered.failure.empty());

        // Nothing the engine could not do, beyond what the case declared it
        // expects. A snapshot that dropped a clip and a render that matched it
        // are two bugs rather than none, so a diagnostic fails here whatever the
        // audio did; a case that is about a model the compiler is right to
        // refuse names that refusal on itself (NullDiffCase.hpp), and each one
        // it names has to have been reported.
        auto diagnostics = rendered.diagnostics;
        for (const auto& expected : value.expectedDiagnostics) {
            const auto found = std::find_if(diagnostics.begin(), diagnostics.end(),
                                            [&](const std::string& reported) {
                                                return reported.find(expected) != std::string::npos;
                                            });

            INFO("expected diagnostic: " << expected);
            CHECK(found != diagnostics.end());
            if (found != diagnostics.end())
                diagnostics.erase(found);
        }

        for (const auto& diagnostic : diagnostics)
            INFO(diagnostic);
        CHECK(diagnostics.empty());

        CHECK(rendered.starvedVoices == 0);
        CHECK(rendered.droppedMidiEvents == 0);

        const auto expectedSamples = static_cast<std::int64_t>(std::llround(
            (value.endBeat - value.startBeat) * 60.0 / value.startBpm() * value.sampleRate));

        if (value.capturesMidi()) {
            // A MIDI case renders no audio by construction: the device standing
            // in for a synth records rather than sounds. What it must not do is
            // record nothing.
            CHECK_FALSE(rendered.midi.empty());
        } else {
            CHECK(rendered.audio.getNumSamples() > 0);
            CHECK(peakOf(rendered.audio) > 0.01);

            // Only for a case at one tempo: with a change in it, the range is
            // shorter than the arithmetic above says and the map is the one
            // that knows by how much.
            if (value.tempo.size() == 1)
                CHECK(std::abs(rendered.audio.getNumSamples() - expectedSamples) <= 1);
        }
    }
}

TEST_CASE("A routed MIDI source is not captured", "[nulldiff][native]") {
    // The one place a connected MIDI slot and a MIDI-consuming chain part
    // company. emitTrack compiles a ClipMidi op for a track whose MIDI another
    // track routes from, even when nothing on that track consumes MIDI, and
    // every device is wired to whatever chain MIDI exists. So a source track
    // carrying only an audio effect has a device with a live MIDI input while
    // consuming none itself.
    //
    // The incumbent asks the other question, and skips it. A tap chosen on the
    // wiring alone would hand back a stream for a track the incumbent never
    // captured, and the runner would report it as captured by one leg only.
    Case value;
    value.name = "routed.source";
    value.covers = "a track whose MIDI another track reads";
    value.endBeat = 4.0;

    TrackInfo source;
    source.id = 1;
    source.type = TrackType::Media;
    source.name = "Source";
    source.audioOutputDevice = "master";
    {
        DeviceInfo effect;
        effect.id = 800;
        effect.name = "Effect";
        effect.deviceType = DeviceType::Effect;
        effect.isInstrument = false;
        effect.canReceiveMidi = false;
        source.chain.fxChainElements.emplace_back(std::move(effect));
    }

    TrackInfo destination;
    destination.id = 2;
    destination.type = TrackType::Media;
    destination.name = "Synth";
    destination.audioOutputDevice = "master";
    // What makes the source a source: an internal MIDI route, live only while
    // the destination is monitoring its input.
    destination.midiInputDevice = "track:1";
    destination.inputMonitor = InputMonitorMode::In;
    {
        DeviceInfo instrument;
        instrument.id = 801;
        instrument.name = "Capture";
        instrument.deviceType = DeviceType::Instrument;
        instrument.isInstrument = true;
        instrument.canReceiveMidi = true;
        destination.chain.fxChainElements.emplace_back(std::move(instrument));
    }

    value.tracks = {source, destination};
    value.master = [] {
        TrackInfo master;
        master.id = MASTER_TRACK_ID;
        master.type = TrackType::Master;
        master.name = "Master";
        return master;
    }();

    const auto midiClip = [](ClipId id, TrackId trackId, int pitch) {
        ClipInfo clip;
        clip.id = id;
        clip.trackId = trackId;
        clip.name = "midi";
        clip.view = ClipView::Arrangement;
        clip.setMidiContent();
        clip.setPlacementBeats(0.0, 4.0);
        clip.deriveTimesFromBeats(120.0);

        MidiNote note;
        note.noteNumber = pitch;
        note.velocity = 100;
        note.startBeat = 0.0;
        note.lengthBeats = 1.0;
        clip.midiNotes.push_back(note);
        return clip;
    };

    value.clips = {midiClip(1, 1, 48), midiClip(2, 2, 60)};
    value.compareMidiStreams = true;

    const auto rendered = renderNative(value);
    REQUIRE(rendered.failure.empty());

    // The destination is captured, because its chain consumes MIDI.
    CHECK(rendered.midiByTrack.count(2) == 1);

    // The source is not, however its devices are wired.
    CHECK(rendered.midiByTrack.count(1) == 0);
}

TEST_CASE("An eligible track with nothing to play is still captured", "[nulldiff][native]") {
    // The mirror of the routed source. This track consumes MIDI, so the
    // incumbent puts a capture on it and indexes the result by track whether
    // that capture heard anything or not. It has no clip, so no ClipMidi op is
    // compiled and none of its devices has a MIDI input to be nominated by.
    //
    // Without an entry the two legs disagree about which tracks exist rather
    // than about what they received, and the runner reports a track only one leg
    // saw. It is not a corpus case because that would want a fourth track in
    // project.mixed, and four audio tracks in one Edit trip the fork's
    // node-identity assertion (#2085).
    Case value;
    value.name = "eligible.empty";
    value.covers = "an instrument track with no clip beside one with";
    value.endBeat = 4.0;

    const auto instrumentTrack = [](TrackId trackId, DeviceId deviceId, const char* name) {
        TrackInfo track;
        track.id = trackId;
        track.type = TrackType::Media;
        track.name = name;
        track.audioOutputDevice = "master";

        DeviceInfo device;
        device.id = deviceId;
        device.name = "Capture";
        device.deviceType = DeviceType::Instrument;
        device.isInstrument = true;
        device.canReceiveMidi = true;
        track.chain.fxChainElements.emplace_back(std::move(device));
        return track;
    };

    value.tracks = {instrumentTrack(1, 810, "Playing"), instrumentTrack(2, 811, "Silent")};
    value.master = [] {
        TrackInfo master;
        master.id = MASTER_TRACK_ID;
        master.type = TrackType::Master;
        master.name = "Master";
        return master;
    }();

    ClipInfo clip;
    clip.id = 1;
    clip.trackId = 1;
    clip.name = "midi";
    clip.view = ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(0.0, 4.0);
    clip.deriveTimesFromBeats(120.0);

    MidiNote note;
    note.noteNumber = 60;
    note.velocity = 100;
    note.startBeat = 0.0;
    note.lengthBeats = 1.0;
    clip.midiNotes.push_back(note);

    value.clips = {clip};
    value.compareMidiStreams = true;

    const auto rendered = renderNative(value);
    REQUIRE(rendered.failure.empty());

    REQUIRE(rendered.midiByTrack.count(1) == 1);
    CHECK_FALSE(rendered.midiByTrack.at(1).empty());

    // Present, and empty. Both halves matter: the entry is what the incumbent
    // has, and its being empty is what says nothing was invented to fill it.
    REQUIRE(rendered.midiByTrack.count(2) == 1);
    CHECK(rendered.midiByTrack.at(2).empty());
}

namespace {

/// A master track with nothing on its chain.
///
/// Built by hand rather than defaulted: a Case's master carries the id and the
/// type the compiler looks for, and one that carried neither would be a
/// different bug from the one each case below is about.
TrackInfo bareMaster() {
    TrackInfo master;
    master.id = MASTER_TRACK_ID;
    master.type = TrackType::Master;
    master.name = "Master";
    return master;
}

/// The same, with @p device on its insert chain.
///
/// Moved in one at a time. A ChainElement holds a unique_ptr for the rack case,
/// so a braced list of them would copy and does not compile.
TrackInfo masterWith(DeviceInfo device) {
    auto master = bareMaster();
    master.chain.fxChainElements.emplace_back(std::move(device));
    return master;
}

/// An instrument track that plays one note, so a case has something audible to
/// send through whatever is downstream of it.
TrackInfo impulseTrack(TrackId trackId, DeviceId deviceId) {
    TrackInfo track;
    track.id = trackId;
    track.type = TrackType::Media;
    track.name = "Instrument";
    track.audioOutputDevice = "master";
    track.chain.fxChainElements.emplace_back(synthDevice(deviceId));
    return track;
}

ClipInfo oneNoteClip(ClipId clipId, TrackId trackId) {
    ClipInfo clip;
    clip.id = clipId;
    clip.trackId = trackId;
    clip.name = "midi";
    clip.view = ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(0.0, 4.0);
    clip.deriveTimesFromBeats(120.0);

    MidiNote note;
    note.noteNumber = 60;
    note.velocity = 100;
    note.startBeat = 0.0;
    note.lengthBeats = 1.0;
    clip.midiNotes.push_back(note);
    return clip;
}

/// One note into one instrument, with @p device on the master chain.
Case throughMaster(const char* name, DeviceInfo device) {
    Case value;
    value.name = name;
    value.covers = "the master chain's own devices";
    value.endBeat = 4.0;
    value.tracks.push_back(impulseTrack(1, 900));
    value.master = masterWith(std::move(device));
    value.clips.push_back(oneNoteClip(1, 1));
    return value;
}

}  // namespace

TEST_CASE("A device on the master chain is the one that renders", "[nulldiff][native]") {
    // The master's chain is as much of the project as any track's, and the
    // compiler emits Device ops for it. A leg that collected only the tracks'
    // devices left every one of them looked up and not found, which binds the
    // stand-in: the case still renders, still compares, and is measuring a
    // project with no master chain in it.
    //
    // A gain of zero is what makes that visible. Bound, it silences the render;
    // as a stand-in it passes the signal through and the case cannot tell the
    // difference between a master device that worked and one that was never
    // there.
    const auto silenced = renderNative(throughMaster("master.silenced", gainDevice(901, 0.0f)));
    REQUIRE(silenced.failure.empty());
    CHECK(silenced.diagnostics.empty());
    CHECK(peakOf(silenced.audio) == 0.0);

    // The control, so the assertion above is about the master device rather
    // than about a case that renders nothing either way.
    const auto passed = renderNative(throughMaster("master.passed", gainDevice(901, 1.0f)));
    REQUIRE(passed.failure.empty());
    CHECK(peakOf(passed.audio) > 0.0);
}

TEST_CASE("A plugin the machine does not have is said out loud", "[nulldiff][native]") {
    // The rule #2175 turns on: a project rendered without its plugin is not
    // that project, and a case that compared anyway would pass by having tested
    // less than it claims. The runner makes any diagnostic a case did not
    // declare unmeasurable, so what this leg owes is to report one.
    //
    // Rendered with no scan at all, which is what a machine that has never
    // scanned looks like and what every CI runner looks like.
    Case value;
    value.name = "external.absent";
    value.covers = "a project naming a plugin this machine cannot resolve";
    value.endBeat = 4.0;

    TrackInfo track;
    track.id = 1;
    track.type = TrackType::Media;
    track.name = "Hosting";
    track.audioOutputDevice = "master";

    DeviceInfo plugin;
    plugin.id = 910;
    plugin.name = "Something Nobody Has";
    plugin.deviceType = DeviceType::Effect;
    plugin.format = PluginFormat::VST3;
    track.chain.fxChainElements.emplace_back(std::move(plugin));

    value.tracks.push_back(std::move(track));
    value.master = bareMaster();

    const auto rendered = renderNative(value);

    // It still renders. The stand-in is bound because the executor refuses an
    // unbound Device op, and a leg that failed outright would report the case
    // as a broken harness rather than as a project it could not measure.
    REQUIRE(rendered.failure.empty());

    REQUIRE_FALSE(rendered.diagnostics.empty());
    const auto reported = rendered.diagnostics.front();
    CHECK(reported.find("Something Nobody Has") != std::string::npos);
}

TEST_CASE("A plugin on the master chain is reported too", "[nulldiff][native]") {
    // The two halves of this change meet here: a master device is looked up at
    // all, and an external one that cannot be resolved says so. A master bus
    // limiter is where a real project puts a plugin, and it is the case the old
    // lookup would have dropped in silence.
    DeviceInfo plugin;
    plugin.id = 911;
    plugin.name = "Master Bus Limiter";
    plugin.deviceType = DeviceType::Effect;
    plugin.format = PluginFormat::VST3;

    const auto rendered = renderNative(throughMaster("external.master", std::move(plugin)));

    REQUIRE(rendered.failure.empty());
    REQUIRE_FALSE(rendered.diagnostics.empty());
    CHECK(rendered.diagnostics.front().find("Master Bus Limiter") != std::string::npos);
}
