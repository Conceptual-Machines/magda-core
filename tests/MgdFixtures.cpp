#include "MgdFixture.hpp"

/**
 * @file MgdFixtures.cpp
 * @brief Which real projects the corpus carries, and what each declares
 *        (#2173).
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
 * designed. This slice reuses, because the rig is what is being proved and
 * saving projects needs the app. #2081 is where the designed ones arrive.
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

MaterialSpec toneFor(double seconds, double frequency) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Tone;
    spec.sampleRate = 44100.0;
    spec.durationSeconds = seconds;
    spec.frequency = frequency;
    return spec;
}

Case declarationFor(const char* name, const char* covers, double endBeat) {
    Case value;
    value.name = name;
    value.covers = covers;
    value.startBeat = 0.0;
    value.endBeat = endBeat;

    // Left at the corpus default deliberately. What tier a real project owes is
    // decided when it has been rendered through both legs and the answer is a
    // number, which is #2081; declaring one here would be choosing the verdict
    // before the measurement. This slice renders nothing.
    value.tier = AudioTier::Exact;
    return value;
}

std::vector<MgdFixture> build() {
    std::vector<MgdFixture> fixtures;

    // Both of these are internal-device projects, and that is a requirement
    // rather than what was to hand. A project hosting a VST3 cannot be held to a
    // null: without the plugin installed both legs render a passthrough and pass
    // by agreeing about nothing, and with it installed the incumbent hosts it
    // while the native leg cannot, so the verdict becomes a fact about the
    // machine. #2175 reserves those projects for the invariant tier and an
    // absent-plugin gate, and they belong there rather than here.
    //
    // Half the legacy corpus is out on that rule -- retrovid and envfollower
    // carry Retrospect, groups carries Pro-L 2, overlaps carries Pianoteq -- and
    // the check is `format`, which is VST3 at zero and Internal at three.

    {
        // The arrangement: eight tracks and sixty-nine clips at 120 bpm, one of
        // them audio and the rest MIDI, with an automation lane over the top.
        // That shape is the argument for loading a project at all. Nobody
        // writing a case in code produces sixty-eight MIDI clips across eight
        // tracks to see what happens; somebody making a demo does.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.15.0-demo.mgd";
        fixture.savedBy = "0.15.0-9-gf444267f";
        fixture.isMigrationFixture = true;
        fixture.declaration =
            declarationFor("project.demo", "eight tracks and sixty-nine clips at 120 bpm", 32.0);

        // A tone, because the clip reading it is stretched: the project plays a
        // loop recorded at one tempo inside an arrangement at another, and
        // impulses through two different stretchers report the distance between
        // two interpolators rather than anything about the clip.
        fixture.sources = {
            {.fileName = "SA_MU_89_electric_guitar_loop_arp_chillin_vibrato_Emin.wav",
             .material = toneFor(14.0, 220.0),
             .covers = "a guitar loop the arrangement stretched from 82 to 120"},
        };

        fixtures.push_back(std::move(fixture));
    }

    {
        // A second project, and the point of it is that there are two. A Case
        // carries source ids and nothing else, so two of them loaded at once is
        // the arrangement under which those ids either stay meaningful or
        // quietly start naming each other's audio. One fixture can never show
        // that.
        //
        // Small on purpose: two tracks, two clips, two automation lanes. What it
        // adds is a second project and an automation lane, not more surface.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.11.1-automation.mgd";
        fixture.savedBy = "0.11.1-119-gbe8377ce0";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.automation", "two tracks under two automation lanes at 120 bpm", 16.0);

        // Impulses: this clip plays at its own rate, so where it lands is the
        // whole of what a render would be asserting.
        fixture.sources = {
            {.fileName = "HeartBreaker Layered Master-bounce-10.wav",
             .material = impulsesFor(4.0, 0.25),
             .covers = "a bounced break, played unstretched"},
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
