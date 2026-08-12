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

TEST_CASE("A stretched clip survives a block shorter than a cell", "[nulldiff][native]") {
    // A device at 64 samples still drives whole 128-sample cells through the
    // stretcher, so everything sized against one has to be sized against the
    // cell rather than the block. Sized against the block, SoundTouch fills
    // half a cell and zero-fills the rest, and the reading clamp truncates
    // every cell's read: alternating material and silence, at exactly the block
    // sizes a low-latency interface uses, and no error anywhere.
    for (const auto& value : sharedCorpus(scratch())) {
        if (value.capturesMidi() || value.name.rfind("stretch", 0) != 0)
            continue;

        INFO(value.name);

        auto small = value;
        small.blockSize = 64;
        const auto rendered = renderNative(small);

        REQUIRE(rendered.failure.empty());
        CHECK(rendered.diagnostics.empty());
        CHECK(peakOf(rendered.audio) > 0.01);

        // The failure this exists for is not silence everywhere, it is silence
        // every other cell. So the material is checked in cell-sized pieces
        // across a stretch of the clip rather than as one peak.
        // From a second in, past the priming a stretcher needs before it has
        // anything to give: that opening quiet is the engine working, and
        // counting it here would measure latency rather than the hole this is
        // looking for.
        const auto from = static_cast<int>(value.sampleRate);
        auto quietCells = 0;
        const auto cells = std::min(64, std::max(0, (rendered.audio.getNumSamples() - from) / 128));
        REQUIRE(cells > 0);

        for (auto cell = 0; cell < cells; ++cell) {
            auto peak = 0.0f;
            for (auto sample = 0; sample < 128; ++sample)
                peak = std::max(peak,
                                std::abs(rendered.audio.getSample(0, from + cell * 128 + sample)));
            if (peak < 1.0e-6f)
                ++quietCells;
        }

        INFO(quietCells << " of " << cells << " cells were silent");
        CHECK(quietCells == 0);
    }
}

TEST_CASE("The native leg renders the same at any block size", "[nulldiff][native]") {
    // What RenderContext requires of every op, now over real material rather
    // than over a test device: block size is an I/O batching concept and never
    // a precision one.
    //
    // Two kinds of clip do not hold to it today, and both were found by running
    // this over the corpus. They are named rather than skipped by a predicate,
    // so that the list cannot quietly grow: a case that starts failing this has
    // to be added here by somebody, with a reason.
    //
    // This used to hold five cases and now holds one, which is the corpus
    // paying for itself. A rate that varies within a block was resolved from
    // that block's own two ends, so a speed ramp, auto tempo across a tempo
    // change and a warped clip approximated their curve differently at every
    // block size; and a stretcher framed whatever sizes it was handed. Both are
    // gone: a clip that consumes its reading at a rate is now fed on a grid
    // anchored to where its event begins (ClipVoice::renderThroughCells), so
    // what reaches the stretcher is a function of position on the timeline.
    //
    // What is left is one SoundTouch mode. Its sibling, the same material and
    // the same feed, holds; so does Signalsmith. Whatever is left in there is
    // internal to that mode rather than to how it is fed, and it is worth its
    // own issue rather than a guess here.
    // Empty, and it took three fixes to get there. A rate that varies within a
    // block was resolved from that block's own two ends; a stretcher framed
    // whatever sizes it was handed; and a cell reading past its block's end got
    // its beats from a straight line through that block rather than from the
    // tempo map, so a cell straddling a step tempo change ran at the wrong
    // ratio. The first two were the cell grid, the third was the block's beat
    // mapping (RenderContext.hpp).
    //
    // Asserted in both directions, so this cannot silently refill and a case
    // that comes off has to be taken off by somebody.
    const std::set<std::string> knownDependent{};

    std::set<std::string> failed;

    for (const auto& value : sharedCorpus(scratch())) {
        if (value.capturesMidi())
            continue;

        INFO(value.name);

        // Deliberately not multiples of the cell size. At 128 and 1024 the
        // event-anchored grid coincides with the block grid, so the holdover
        // buffer, the head-drop and the mid-cell resume never run and the
        // invariance claim would be weakest exactly where the machinery does
        // the most work.
        auto small = value;
        small.blockSize = 96;
        auto large = value;
        large.blockSize = 480;

        const auto first = renderNative(small);
        const auto second = renderNative(large);

        REQUIRE(first.failure.empty());
        REQUIRE(second.failure.empty());
        REQUIRE(first.audio.getNumSamples() == second.audio.getNumSamples());

        AudioCompareOptions options;
        options.sampleRate = value.sampleRate;
        const auto residual = compareAudio(first.audio, second.audio, options);

        INFO("peak " << formatDb(residual.peakDb) << " at sample " << residual.firstDivergence);

        if (!residual.withinFloor())
            failed.insert(value.name);

        if (knownDependent.count(value.name) == 0)
            CHECK(residual.withinFloor());
    }

    // The list is exactly the cases that fail, in both directions. One that
    // stops failing has to come off the list, so that a fix is recorded rather
    // than absorbed.
    CHECK(failed == knownDependent);
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
    source.type = TrackType::Audio;
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
    destination.type = TrackType::Audio;
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
        track.type = TrackType::Audio;
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
