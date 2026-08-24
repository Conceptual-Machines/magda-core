# The legacy corpus

Real projects and presets saved by released builds of MAGDA, checked in and
loaded on every test run.

Everything else in the validation harness builds its cases in code (#2040),
because a case built in code is reviewable and cannot drift. This corpus is the
deliberate exception: the load is the thing under test, so a case that skips the
file tests nothing.

`projects/` holds twenty-two `.mgd` files, oldest format first. `presets/` holds
device presets from the same era. `tests/LegacyCorpus.hpp` is the index: it
records, per project, what the file contains, read out of the saved JSON by hand
rather than recorded from a load, so a regression that silently drops tracks,
clips, devices or automation shows up as a mismatch instead of being re-recorded
as the new expectation.

## Never rewrite these bytes

Not to tidy a path, not to shrink a file, not to fix a typo in a track name, and
not to resave one through a newer build so it stops needing a migration.

These files exist to prove that a project somebody saved years ago still opens,
and the only way they can prove it is by being what was actually written. A
resaved fixture tests the current writer against the current reader, which is a
thing every other test in the suite already does.

The version in each filename is the build that saved it. `1.0.0` is not a
release: it is the placeholder version string builds carried before the version
was wired to the tag, so those three files are the oldest format still openable.

## The dead paths are permanent

Every audio source in these projects points at a machine that is gone: an
external SSD, somebody's Music folder, a bounces directory under a project that
was deleted years ago. That is not damage to be repaired. Repairing it would
mean rewriting the bytes.

Anything that needs one of these projects to actually make a sound stands in for
its sources rather than finding them. The `.mgd` fixture rig (#2173,
`tests/MgdFixture.hpp`) is where that happens: a manifest names, per source, the
`MaterialSpec` written in its place, and the rig repoints the project before
either engine sees it. A source the manifest fails to name is a hard failure
there, because a source that resolves to nothing renders silence and silence
nulls against silence perfectly well.

## Reuse here, or save new elsewhere

The render corpus (#2081) has to decide, per project, whether to reuse one of
these or save its own. The rule, recorded per fixture in
`MgdFixture::isMigrationFixture`:

**Reuse where a project earns its place by being a real arrangement** that
nobody would have written down. That is the whole value of this directory: four
tracks at 92 bpm with two bounces of its own clips in it is a shape a
hand-written case never has.

**Save new where the material has to be designed.** A fixture reused from here
can never be asked for one bar more, for a source that suits a case better, or
for a device it would rather exercise, because that would mean rewriting bytes
that may not be rewritten. A case that needs any of those needs its own project,
saved for the render corpus and free to be resaved.

A fixture saved for the render corpus does not belong in this directory. It is
not a migration fixture, it carries none of the guarantees above, and putting it
here would make the rule about never rewriting apply to a file the rule was never
meant to cover.
