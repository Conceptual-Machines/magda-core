#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <string>

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

        // Nothing the engine could not do. A snapshot that dropped a clip and a
        // render that matched it are two bugs rather than none, so a diagnostic
        // fails here whatever the audio did.
        for (const auto& diagnostic : rendered.diagnostics)
            INFO(diagnostic);
        CHECK(rendered.diagnostics.empty());

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
