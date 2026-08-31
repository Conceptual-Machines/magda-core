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
 * Twelve of the twenty-two are out, and each for a reason that is a fact about
 * the project or about the engine rather than a preference:
 *
 *  - **envfollower hosts a plugin and is still out**, which is the one entry
 *    here that reads like an exception and is not. Its two Retrospect instances
 *    are both on muted tracks, so an arrangement render of it is one internal
 *    filter and no plugin at all; and it carries 4OSC besides. It belongs with
 *    the 4OSC exclusions below rather than with the three plugin projects, which
 *    are in (#2175).
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

/// A steady band-limited tone, for a source whose case is judged on what its
/// render is rather than on how close two renders are.
///
/// What the invariants tier reads off it is the largest step from one sample to
/// the next, and that number is only meaningful over material that has a
/// meaningful one: impulses step by full scale legitimately, so a project fed
/// them has no step bound worth declaring and the check certifies nothing.
MaterialSpec toneFor(double seconds, double frequency) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Tone;
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

    // The fixtures down to the Faust one are internal-device projects and the
    // three after them host a plugin. That is a division rather than an
    // ordering: an internal-device project is held to a null, and a project
    // hosting a plugin cannot be, so the second group declares the invariants
    // tier and does not run at all on a machine that has not scanned its plugin.
    //
    // The rig checks it rather than trusting this comment. A plugin in a project
    // that the manifest does not name is refused, a name no device claims is
    // refused, and a fixture hosting one that did not declare the tier is
    // refused (MgdFixture.cpp, refuseUndeclaredPlugins).

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

    // ---------------------------------------------------------------------
    // The projects that host a plugin (#2175)
    // ---------------------------------------------------------------------
    //
    // Everything above is an internal-device project and was a requirement while
    // nothing hosted a VST3 in the engine. #1893 ended that, and these three are
    // what the requirement was keeping out: the projects most worth having, and
    // the ones a corpus that quietly dropped every project with a plugin in it
    // would have dropped.
    //
    // All three declare the invariants tier, and the rig refuses them otherwise.
    // A plugin is entitled to frame its own work -- to dither, to hold state
    // from however it was last called -- so a residual measured across one is a
    // number about the plugin, and a tolerance wide enough to pass it is wide
    // enough to pass anything. What is asked instead is what a plugin can be
    // held to: both renders finite, the same length, continuous within a
    // declared step, and decayed by the end.
    //
    // And none of them runs on a machine that has not scanned its plugin. That
    // is not a gap: it is the gate (NullDiffCase.hpp, absentPluginsIn). A
    // project rendered without the plugin it names is a different project, and
    // two legs rendering it without the plugin null perfectly.

    {
        // Two audio tracks each through their own Retrospect, one of them with
        // an internal Limiter after it. The first project in this corpus where a
        // hosted plugin makes the sound, and the mixed chain is the half worth
        // having: an external device and an internal one in one chain is where a
        // plan that binds the two kinds differently shows it.
        //
        // Both instances carry saved state, which is the other half. #2244 made
        // the chunk authoritative over the project's parameter array, and this
        // is a project whose chunk was written by a released build rather than
        // by a test.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.13.0-retrovid.mgd";
        fixture.savedBy = "0.13.0-rc1";
        fixture.isMigrationFixture = true;
        fixture.declaration =
            declarationFor("project.retrospect",
                           "two audio tracks through hosted Retrospect instances at 92 bpm, one "
                           "of them behind an internal limiter",
                           0.0, 8.0);

        fixture.declaration.tier = AudioTier::Invariants;
        fixture.hostedPlugins = {"Retrospect"};

        // Measured on the first run with the plugin installed: -3.0 dB, which
        // is 0.71 of full scale in one sample. Named rather than rounded up to a
        // number that would pass anything, and it is the mechanism that says why
        // it is this large. Retrospect is a repeat effect: it splices, and a
        // splice between two phases of a tone steps by the distance between
        // them, which for a full-scale tone is up to twice its peak. What the
        // bound has to refuse is a step larger than a splice, which is what a
        // graph transition without a ramp leaves behind.
        fixture.declaration.maxStepPerSample = 0.75;

        // Eight beats of an arrangement whose clips run to thirty-two, so the
        // render stops in the middle of the music. Nothing has decayed at the
        // end because nothing has stopped.
        fixture.declaration.rendersPastItsMaterial = false;

        // Which is why the liveness floor has to be here: with no tail to ask
        // about and no residual in this tier, every remaining check passes on
        // silence. The quieter of the two legs peaks at +4 dBFS on this project,
        // a float render of two tones through a repeat effect and a limiter; -40
        // is far below that and far above anything a dead leg produces.
        fixture.declaration.minPeakDb = -40.0;

        // Retrospect is an LV2 plugin behind a VST3 shell and hands JUCE its own
        // URI, "https://conceptualmachines.com/plugins/retrospect", where a path
        // is expected. Once per instantiation, so twice here.
        //
        // Named rather than waived, and asserted in both directions: the day
        // Retrospect stops doing it this line fails and comes out, which is what
        // keeps it from quietly forgiving something else.
        fixture.declaration.expectedHostedAssertions = {"juce_File.cpp:219"};

        fixture.declaration.mechanism =
            "two hosted Retrospect instances, which owe neither engine a sample";

        // Tones throughout, at frequencies far enough apart that two sources
        // cannot be mistaken for each other in a render. A tone rather than
        // impulses because the step bound above is the whole assertion and
        // impulses have no step worth bounding.
        //
        // Two of the eight sound in the rendered window: the two arrangement
        // clips on the two unmuted tracks. The rest are the project's session
        // clips and its two muted tracks, and they are declared because the
        // manifest has to name every source the project references -- a source
        // the manifest misses reaches a leg pointing at a path that does not
        // exist, renders silence, and silence nulls against silence.
        fixture.sources = {
            {.fileName = "mdh_drm120_touch_stp.wav",
             .material = toneFor(6.0, 220.0),
             .covers = "the first track's arrangement clip, looped and stretched from 120 to 92 "
                       "under the Retrospect and the limiter"},
            {.fileName = "mdh_drm120_touch_stp_(copy)_20260701_145457.wav",
             .material = toneFor(6.0, 330.0),
             .covers = "the second track's arrangement clip, played at its own rate under the "
                       "second Retrospect"},
            {.fileName = "DS_OT_fx_riser_dark.wav",
             .material = toneFor(6.0, 440.0),
             .covers = "a riser on the third track, which the project muted"},
            {.fileName = "DS_OT_fx_riser_dark_20260701_153831.wav",
             .material = toneFor(6.0, 550.0),
             .covers = "its bounce on the fourth track, muted with it"},
            {.fileName = "TAMUZ_TD_90_drum_best_simple_trashy.wav",
             .material = toneFor(2.0, 660.0),
             .covers = "a session clip in the first scene, which an arrangement render never "
                       "launches"},
            {.fileName = "SLS_O_65_guitar_soul_serenade_Cmin.wav",
             .material = toneFor(2.0, 770.0),
             .covers = "a session clip on the second track"},
            {.fileName = "BS_NCS3_140_bass_growl_leap_Dbmin.wav",
             .material = toneFor(2.0, 880.0),
             .covers = "a session clip in the fourth scene"},
            {.fileName = "SA_MU_118_electric_guitar_loop_funky_wah_Amaj.wav",
             .material = toneFor(2.0, 990.0),
             .covers = "a session clip in the fifth scene"},
        };

        fixtures.push_back(std::move(fixture));
    }

    {
        // A hosted instrument rather than a hosted effect, which is the other
        // half of what #1893 built and a different path through the plan: MIDI
        // is routed to it, and whether it is routed at all comes from the role
        // resolution corrects against the scan rather than from what the project
        // guessed (#2252).
        //
        // Two MIDI clips back to back on one track, four notes each. No audio
        // sources at all, which makes this the cheapest case in the corpus and
        // the one that isolates the instrument: whatever comes out came out of
        // Pianoteq, and the notes that went in are compared as a stream beside
        // it.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.18.0-overlaps.mgd";
        fixture.savedBy = "0.18.0-6-g3c042289";
        fixture.isMigrationFixture = true;
        fixture.declaration =
            declarationFor("project.hostedinstrument",
                           "two MIDI clips back to back into a hosted Pianoteq", 0.0, 8.0);

        fixture.declaration.tier = AudioTier::Invariants;
        fixture.hostedPlugins = {"Pianoteq 8"};

        // A piano is a decaying acoustic model: nothing in its output steps, and
        // a bound loose enough for a repeat effect would say nothing here.
        fixture.declaration.maxStepPerSample = 0.1;

        // The second clip's notes are still ringing when the render stops. A
        // piano decays over seconds and the window is four of them, so the tail
        // here is the instrument sounding rather than a device left running.
        fixture.declaration.rendersPastItsMaterial = false;

        // And with no tail asked, liveness is what stops a silent leg passing.
        // Eight notes into a piano is not a quiet render on either engine.
        fixture.declaration.minPeakDb = -40.0;

        fixture.declaration.mechanism =
            "a hosted Pianoteq, which is a physical model and settles from its own state";

        // Asserted beside the audio, and independent of it. What reaches the
        // instrument is a fact both engines owe each other exactly, whatever
        // either instrument then does with it, and it is the assertion that
        // survives the plugin being entitled to its own sound.
        fixture.declaration.compareMidiStreams = true;

        fixtures.push_back(std::move(fixture));
    }

    {
        // Twenty-three stems, three group tracks and a limiter on two of them:
        // a real multitrack session, and the only project in this corpus with a
        // track hierarchy in it. Two things arrive with it that nothing else
        // here has -- group tracks summing their children, and the same plugin
        // hosted twice in one project, once on a track and once on the master.
        //
        // Rendered over eight beats of a project whose clips are five hundred
        // and ninety-seven, for the reason the corpus comment gives: what a
        // fixture is worth is what it contains, not how much of it is played,
        // and a forty-minute arrangement rendered twice is not a test budget.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.10.2-groups.mgd";
        fixture.savedBy = "0.10.2-6-g9cfbb3b32";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.multitrack",
            "twenty-three stems under three group tracks, limited on a track and on the master",
            0.0, 8.0);

        fixture.declaration.tier = AudioTier::Invariants;
        fixture.hostedPlugins = {"Pro-L 2"};

        // A limiter's job is to not step: it is the one device here whose output
        // is bounded by construction, and a bound this tight is what says the
        // twenty-three summed stems went through it rather than around it.
        fixture.declaration.maxStepPerSample = 0.1;

        // Eight beats of a five-hundred-and-ninety-seven-beat arrangement. Every
        // one of the twenty-three stems is still playing when the render stops.
        fixture.declaration.rendersPastItsMaterial = false;

        // Twenty-three tones summed into a limiter. The floor is the same one
        // the other two carry, and it is nowhere near what this renders: what it
        // is for is the difference between a render and nothing.
        fixture.declaration.minPeakDb = -40.0;

        fixture.declaration.mechanism =
            "two hosted Pro-L 2 instances, one on the snare track and one on the master";

        // One tone per stem, spaced so that no two sum to a beat frequency
        // inside the band and no two can be mistaken for each other. Every one
        // of them sounds: this project has no session clips and no muted tracks,
        // which is what makes it the summing case.
        fixture.sources = {
            {.fileName = "BACKING VOX (female) - M81.wav",
             .material = toneFor(5.0, 110.0),
             .covers = "a vocal stem under the VOX group"},
            {.fileName = "BACKING VOX (male) - M80.wav",
             .material = toneFor(5.0, 123.0),
             .covers = "a second vocal stem under the VOX group"},
            {.fileName = "BARITONE - M81-SH.wav",
             .material = toneFor(5.0, 139.0),
             .covers = "a horn stem routed straight to the master"},
            {.fileName = "BASS AMP - AK-47mkII.wav",
             .material = toneFor(5.0, 156.0),
             .covers = "an amped bass stem under the BASS group"},
            {.fileName = "BASS DI.wav",
             .material = toneFor(5.0, 175.0),
             .covers = "the DI beside it, under the same group"},
            {.fileName = "CHAMBER - AR-70 STEREO.L.wav",
             .material = toneFor(5.0, 196.0),
             .covers = "the left half of a stereo room pair, carried as two mono tracks"},
            {.fileName = "CHAMBER - AR-70 STEREO.R.wav",
             .material = toneFor(5.0, 220.0),
             .covers = "its right half"},
            {.fileName = "DRUM HIHAT - M60 FET hypercardioid.wav",
             .material = toneFor(5.0, 247.0),
             .covers = "a hi-hat stem under the DRUMS group"},
            {.fileName = "DRUM KICK - SENN 421  {our M82 died _( }.wav",
             .material = toneFor(5.0, 277.0),
             .covers = "a kick stem, whose file name carries the braces and spaces a real "
                       "session leaves in one"},
            {.fileName = "DRUM OVERHEADS - AR-51.L.wav",
             .material = toneFor(5.0, 311.0),
             .covers = "the left overhead"},
            {.fileName = "DRUM OVERHEADS - AR-51.R.wav",
             .material = toneFor(5.0, 330.0),
             .covers = "the right overhead"},
            {.fileName = "DRUM SNARE - M80-SH.wav",
             .material = toneFor(5.0, 370.0),
             .covers = "the snare, and the one track in the project with a plugin on it"},
            {.fileName = "DRUM TOM - M81-SH.wav",
             .material = toneFor(5.0, 415.0),
             .covers = "a tom stem"},
            {.fileName = "DRUM TOM 2 - M81-SH.wav",
             .material = toneFor(5.0, 466.0),
             .covers = "a second tom stem"},
            {.fileName = "E GUITAR - M81-SH.wav",
             .material = toneFor(5.0, 523.0),
             .covers = "a guitar stem routed straight to the master"},
            {.fileName = "KEY AMP - M81-SH.wav",
             .material = toneFor(5.0, 587.0),
             .covers = "a keyboard amp stem"},
            {.fileName = "LEAD VOX (female) - M81.wav",
             .material = toneFor(5.0, 659.0),
             .covers = "the lead vocal, under the VOX group"},
            {.fileName = "LESLIE BOTTOM - M81.wav",
             .material = toneFor(5.0, 740.0),
             .covers = "the bottom of a Leslie pair"},
            {.fileName = "LESLIE TOP - M81.wav",
             .material = toneFor(5.0, 831.0),
             .covers = "its top"},
            {.fileName = "ROOM - M81 (ORTF).L.wav",
             .material = toneFor(5.0, 932.0),
             .covers = "the left half of an ORTF room pair"},
            {.fileName = "ROOM - M81 (ORTF).R.wav",
             .material = toneFor(5.0, 1046.0),
             .covers = "its right half"},
            {.fileName = "TENOR - M81-SH.wav",
             .material = toneFor(5.0, 1174.0),
             .covers = "a tenor horn stem"},
            {.fileName = "TRUMPET - M81-SH.wav",
             .material = toneFor(5.0, 1318.0),
             .covers = "a trumpet stem"},
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
