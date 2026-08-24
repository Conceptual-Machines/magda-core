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
 * long enough that the clip reading it does not run out, and no longer: the
 * original was eleven or twenty-nine seconds of somebody's Splice library, and
 * writing thirty seconds of tone to stand in for a clip that plays four bars of
 * it would cost the run a second of disk per fixture for nothing.
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

    {
        // A real arrangement, and the first thing in the corpus that nobody
        // wrote down: four tracks, nine clips at 92 bpm, and eight sources off
        // three different dead volumes -- an external SSD, a home Music folder,
        // and a bounces directory under a project that no longer exists. Two of
        // the eight are bounces of other clips in the same project, which is a
        // shape a case built in code would never have thought to have.
        //
        // Reused rather than saved: it earns its place by being an arrangement
        // rather than a demonstration, and being a migration fixture costs this
        // case nothing it wanted.
        MgdFixture fixture;
        fixture.file = "legacy/projects/0.13.0-retrovid.mgd";
        fixture.savedBy = "0.13.0-rc1";
        fixture.isMigrationFixture = true;
        fixture.declaration = declarationFor(
            "project.retrovid", "a four-track arrangement at 92 bpm over eight sources", 32.0);

        // Impulses almost throughout: these clips play at the project's own
        // tempo and what the corpus wants off them is where they land. The two
        // exceptions are the ones the project stretched, and a stretched clip
        // fed impulses reports the distance between two interpolators.
        //
        // The intervals differ per source on purpose. Equal grids sum exactly
        // whatever order they are added in, so eight identical sources would
        // agree by arithmetic rather than by the two engines placing the same
        // things in the same places.
        fixture.sources = {
            {.fileName = "SA_MU_118_electric_guitar_loop_funky_wah_Amaj.wav",
             .material = toneFor(18.0, 220.0),
             .covers = "a guitar loop the project stretched from 118 to 92"},
            {.fileName = "mdh_drm120_touch_stp.wav",
             .material = impulsesFor(6.0, 0.25),
             .covers = "a drum loop at its own rate"},
            {.fileName = "SLS_O_65_guitar_soul_serenade_Cmin.wav",
             .material = toneFor(32.0, 330.0),
             .covers = "a 65 bpm loop stretched to 92, the widest ratio in the project"},
            {.fileName = "BS_NCS3_140_bass_growl_leap_Dbmin.wav",
             .material = impulsesFor(8.0, 0.3),
             .covers = "a bass one-shot"},
            {.fileName = "TAMUZ_TD_90_drum_best_simple_trashy.wav",
             .material = impulsesFor(23.0, 0.5),
             .covers = "the longest loop, near enough the project tempo to play unstretched"},
            {.fileName = "DS_OT_fx_riser_dark_20260701_153831.wav",
             .material = impulsesFor(9.0, 0.7),
             .covers =
                 "a bounce of the riser below, which is what makes two sources name one sound"},
            {.fileName = "mdh_drm120_touch_stp_(copy)_20260701_145457.wav",
             .material = impulsesFor(19.0, 0.35),
             .covers = "a bounce of the drum loop above, under a name with brackets in it"},
            {.fileName = "DS_OT_fx_riser_dark.wav",
             .material = impulsesFor(3.0, 0.2),
             .covers = "the riser itself, a short one-shot"},
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
