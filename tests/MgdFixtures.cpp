#include "MgdFixture.hpp"

/**
 * @file MgdFixtures.cpp
 * @brief Which real projects the corpus carries, and what each declares
 *        (#2173, #2081).
 *
 * Separate from the rig for the reason NullDiffCorpus.cpp is separate from
 * NullDiffCase.hpp: the table is a description of what is being tested and the
 * rig is machinery, and a reader who wants to know what the corpus covers
 * should not have to read a source installer to find out.
 *
 * ## Reuse or save new
 *
 * #2173 left this decision to whoever wrote the slice, and the answer is both,
 * recorded per fixture in `isMigrationFixture`.
 *
 * Twenty-two real projects are already checked in under `corpus/legacy/projects`
 * for #2079, saved by builds from the oldest openable format through 0.18.0, and
 * reusing one costs nothing to store. What it costs instead is the freedom to
 * change it: those bytes are migration fixtures and must never be rewritten, so
 * a render case built on one can never ask it for one bar more, for a source
 * that suits it better, or for a device it would rather exercise. It gets the
 * project as somebody actually saved it, which is the whole point, and it gets
 * nothing else.
 *
 * So: reuse where a project earns its place by being a real arrangement nobody
 * would have written down, and save new ones where the material has to be
 * designed. Everything here is a reuse. A project saved for this corpus goes
 * under `corpus/render/projects` with `isMigrationFixture` false, and needs
 * nothing from the rig that is not already here -- the file path and the
 * manifest entry are the whole addition.
 *
 * ## What each fixture renders
 *
 * A window rather than the whole arrangement, chosen so that everything the
 * project has on at once is inside it. That is not a convenience: a range which
 * misses the project's audio clip leaves the case asserting a project without
 * one, and the fixture that had this wrong first is the demo, whose only audio
 * clip starts at beat 96 and whose declaration rendered the first thirty-two.
 *
 * ## Choosing material for a source nobody still has
 *
 * Every path in these files points at a machine that is gone, so every source
 * is a judgement rather than a recovery. The judgement is the corpus's usual
 * one (#2040): what stands between the two engines on this source's path.
 *
 * A clip that plays at its own rate and lands where it was placed wants
 * impulses, because placement is the whole assertion and there is nothing to
 * interpolate. A clip the project stretched wants a tone, because impulses
 * through two different stretchers measure the distance between two
 * interpolators and say nothing about the clip. A clip whose level is what the
 * case is reading wants steps.
 *
 * The durations are not the originals and are not trying to be. A source is
 * long enough that the clip reading it does not run out, and no longer. The
 * original was eleven seconds of somebody's Splice library; writing thirty
 * seconds of tone to stand in for a clip that plays four bars of it would cost
 * the run a second of disk per fixture for nothing.
 *
 * ## The projects that are not here
 *
 * Fifteen of the twenty-two are out, and each for a reason that is a fact about
 * the project or about the engine rather than a preference:
 *
 *  - **Half of them host an external plugin.** retrovid and envfollower carry
 *    Retrospect, groups carries Pro-L 2, overlaps carries Pianoteq. The rig
 *    refuses those outright and #2175 is the slice that takes them, with the
 *    invariant tier and a gate that calls an absent plugin unmeasurable rather
 *    than equal.
 *  - **Seven of them contain 4OSC**, which has no engine device yet, so a
 *    Device op for one binds to a passthrough and the case would be comparing
 *    a project neither engine really rendered: dupes, macrolinkmod, retired-fx,
 *    dafunk, rack, fxshowcase and automation. They arrive with the port.
 *  - **Two of them render silence.** master-eq has no tracks at all, and
 *    sessiondemo's thirty-two clips are all session clips, so an arrangement
 *    render of it plays nothing. Two silences null perfectly and assert
 *    nothing, which the runner refuses as unmeasurable rather than passing.
 *  - **automation-clips has no instrument on its one track**, so its MIDI clip
 *    and the automation lane over it reach a track that makes no sound. Same
 *    refusal, and the same fix: it needs a project saved with a device on it,
 *    which is a new fixture rather than a rewrite of this one.
 *  - **drumgrid-rack** stops on a rack whose device has a gap in its parameter
 *    indices, which the parameter table does not carry yet.
 */

namespace magda::nulldiff {
namespace {

MaterialSpec impulsesFor(double seconds, double interval) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Impulses;
    spec.sampleRate = 44100.0;
    spec.durationSeconds = seconds;
    spec.intervalSeconds = interval;
    return spec;
}

/// A tone with an envelope to correlate, for a clip a stretcher stands in front
/// of: a stretched case is judged on its envelope and its spectrum, and a
/// steady tone has no envelope, so an envelope test over one measures the noise
/// floor's preferences.
MaterialSpec pulsedToneFor(double seconds, double frequency) {
    MaterialSpec spec;
    spec.kind = MaterialKind::PulsedTone;
    spec.sampleRate = 44100.0;
    spec.durationSeconds = seconds;
    spec.frequency = frequency;
    return spec;
}

Case declarationFor(const char* name, const char* covers, double startBeat, double endBeat) {
    Case value;
    value.name = name;
    value.covers = covers;
    value.startBeat = startBeat;
    value.endBeat = endBeat;
    value.tier = AudioTier::Exact;
    return value;
}

std::vector<MgdFixture> build() {
    std::vector<MgdFixture> fixtures;

    // Every fixture here is an internal-device project, and that is a
    // requirement rather than what was to hand. A project hosting a VST3 cannot
    // be held to a null: without the plugin installed both legs render a
    // passthrough and pass by agreeing about nothing, and with it installed the
    // incumbent hosts it while the native leg cannot, so the verdict becomes a
    // fact about the machine. #2175 reserves those projects for the invariant
    // tier and an absent-plugin gate, and they belong there rather than here.
    //
    // The rig checks it rather than trusting this comment: `format`, which is
    // VST3 at zero and Internal at three.

    {
        // Two audio tracks playing bounces off an SSD that is gone, and a Drum
        // Grid on a third with nothing to play. The oldest shape in the corpus:
        // a v1 project, whose clips carry their source inline rather than
        // through a pooled table, and whose placement is in seconds rather than
        // beats. That the load produces the same model values out of that as
        // out of a v2 file is the thing worth rendering it for.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.4.8-drumgrid.mgd";
        fixture.savedBy = "0.4.8-10-g83eb35e0";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.v1bounces", "two v1 audio clips with their sources inline, at 120", 0.0, 16.0);

        // The grid's pads, said out loud rather than substituted in silence.
        //
        // A Drum Grid fills every pad with a magdasampler when it restores
        // (DrumGridPads.cpp), and that device has not moved to the SDK, so the
        // native engine cannot build one (#2271). Until #2175 the harness never
        // looked inside a pad and the substitution was invisible; it looks now,
        // so the gap is named here.
        //
        // Declared rather than skipped, because the grid on this project's third
        // track has nothing to play and the two v1 audio clips are what the
        // fixture exists for. Declaring it keeps those live and asserts the gap
        // in both directions: the day the sampler runs natively (#2271), this
        // line fails and comes out.
        fixture.declaration.expectedDiagnostics = {
            "no native device for magdasampler",
        };

        // Impulses on both: neither clip is stretched -- the source's own bpm is
        // the project's -- so where each sample lands is the whole assertion.
        //
        // Two sources rather than one, and that is the point of this fixture
        // beside the others: two clips on two tracks reading two different
        // files is where a source id naming the wrong file stops being
        // invisible.
        fixture.sources = {
            {.fileName = "HiHat Closed Bell-bounce-1.wav",
             .material = impulsesFor(7.0, 0.25),
             .covers = "a hi-hat bounce on the first track, played unstretched"},
            {.fileName = "HiHat Closed Bell-bounce-3.wav",
             .material = impulsesFor(7.0, 1.0 / 3.0),
             .covers = "a second bounce on the second track, at a different interval so the "
                       "two cannot be mistaken for each other"},
        };

        fixtures.push_back(std::move(fixture));
    }

    {
        // One audio clip on one track at 172 bpm, and nothing else at all: no
        // devices, no automation, no master chain. The simplest real project
        // there is, and it is here because the tempo is not the corpus default.
        // Every code-built case renders at 120, so a beat that lands at a
        // different second than the fork thinks it does would null across the
        // whole of that corpus and show up here.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.9.0-analysis.mgd";
        fixture.savedBy = "0.9.0-rc3-1-gde7a0b7c3";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.tempo172", "one audio clip on a bare track at 172 bpm", 0.0, 16.0);

        // Impulses: the clip's interpretation is unlocked and its own bpm is
        // the file's, so it plays at its own rate and where each impulse lands
        // is what the two engines owe each other.
        fixture.sources = {
            {.fileName = "Sub Focus - Timewarp (Dimension Remix).wav",
             .material = impulsesFor(9.0, 0.25),
             .covers = "a track bounce, played unstretched under a 172 bpm timeline"},
        };

        fixtures.push_back(std::move(fixture));
    }

    {
        // An FM instrument under four effects: filter, reverb, Clouds and a
        // limiter. Five devices in a row on one track, which is a chain nobody
        // writes down in a case and somebody making a demo patch does, and it
        // is where a device order or a latency the two hosts disagree about
        // would show.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.12.1-fm0demo.mgd";
        fixture.savedBy = "0.12.1-31-g5d7dc457b";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.fmchain", "an FM instrument under filter, reverb, Clouds and a limiter", 0.0,
            16.0);

        fixtures.push_back(std::move(fixture));
    }

    {
        // A Poly Synth with a sidechain device on it and a kick on the track
        // that feeds it. Two tracks whose audio is coupled through a device
        // rather than through routing, which is the first thing in this corpus
        // that is not a chain read top to bottom.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.14.0-sidechain.mgd";
        fixture.savedBy = "0.14.0";
        fixture.isMigrationFixture = true;
        fixture.declaration =
            declarationFor("project.sidechain",
                           "a synth ducked by the kick track through a sidechain device", 0.0, 8.0);

        fixtures.push_back(std::move(fixture));
    }

    {
        // A second project, and the point of it is that there are several. A
        // Case carries source ids and nothing else, so two of them loaded at
        // once is the arrangement under which those ids either stay meaningful
        // or quietly start naming each other's audio.
        //
        // What it adds beyond that is the reverse feature's own project: one
        // audio clip whose length in beats is exactly its length in seconds at
        // the project tempo, so nothing stretches and nothing interpolates.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.16.0-reverse.mgd";
        fixture.savedBy = "0.16.0-4-gf300f6aa";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.render", "a rendered bounce played back at the rate it was made", 0.0, 16.0);

        fixture.sources = {
            {.fileName = "Untitled_20260722_062806.wav",
             .material = impulsesFor(9.0, 0.5),
             .covers = "a render of the project's own output, played unstretched"},
        };

        fixtures.push_back(std::move(fixture));
    }

    {
        // The arrangement: eight tracks and sixty-nine clips at 120 bpm, five of
        // them in the arrangement and sixty-four in the session, with an
        // automation lane over a Poly Synth parameter and a limiter on the
        // master. That shape is the argument for loading a project at all.
        // Nobody writing a case in code produces sixty-four session clips
        // alongside an arrangement to see what happens; somebody making a demo
        // does.
        //
        // Rendered from beat 96, which is where the fifth clip starts. The
        // window before it holds drums and keys only, so the first declaration
        // of this fixture asserted a project without its audio clip in it.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.15.0-demo.mgd";
        fixture.savedBy = "0.15.0-9-gf444267f";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor("project.demo",
                                             "eight tracks, five arrangement clips and a stretched "
                                             "guitar loop, under a master limiter",
                                             96.0, 112.0);

        // Stretched, so the tier is spectral and the material has an envelope
        // to correlate: the project plays a loop recorded at 82.55 bpm inside
        // an arrangement at 120 with its length locked, and impulses through
        // two different stretchers report the distance between two
        // interpolators rather than anything about the clip.
        fixture.declaration.tier = AudioTier::Spectral;
        fixture.declaration.mechanism =
            "the guitar loop is stretched from 82.55 to 120, and two phase vocoders primed "
            "differently never converge on one waveform";

        fixture.sources = {
            {.fileName = "SA_MU_89_electric_guitar_loop_arp_chillin_vibrato_Emin.wav",
             .material = pulsedToneFor(14.0, 220.0),
             .covers = "a guitar loop the arrangement stretched from 82.55 to 120"},
        };

        fixtures.push_back(std::move(fixture));
    }

    {
        // A Faust device on an audio track, and a Poly Synth on a track whose
        // input monitoring is on. Two things no code-built case has: a device
        // whose DSP was compiled from a .dsp file, and a track that is listening
        // to an input nothing is bound to.
        //
        // The clip is warped, and its markers are what a real transient pass
        // produced rather than what somebody would write down: one of them
        // repeats a position, so the compiler drops it and says so. Declared
        // below, because a diagnostic that is the project rather than a refusal
        // is the parity rather than an obstacle to it.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.17.0-faust.mgd";
        fixture.savedBy = "0.17.0-16-gb9ae990b";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.faust", "a Faust device over a warped break, beside a monitored synth track",
            0.0, 16.0);

        fixture.declaration.tier = AudioTier::Spectral;
        fixture.declaration.mechanism =
            "the break is warped, which forces a stretcher on, and the two stretchers prime "
            "from different material";

        fixture.declaration.expectedDiagnostics = {
            "warp marker(s) that do not run forwards",
            "no live MIDI input bound for track 2",
        };

        fixture.sources = {
            {.fileName = "Addictive Drums Key Map FX-bounce-1.wav",
             .material = pulsedToneFor(6.0, 196.0),
             .covers = "a break the clip warps, recorded at 135 under a 120 timeline"},
        };

        fixtures.push_back(std::move(fixture));
    }

    return fixtures;
}

}  // namespace

const std::vector<MgdFixture>& mgdFixtures() {
    static const std::vector<MgdFixture> fixtures = build();
    return fixtures;
}

}  // namespace magda::nulldiff
