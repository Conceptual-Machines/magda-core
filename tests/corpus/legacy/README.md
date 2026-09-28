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

The render corpus (#2081) decides, per project, whether to reuse one of these or
save its own. Ten of the twenty-two are reused today and none has yet needed
one saved for it; `tests/MgdFixtures.cpp` carries the table and says why the
other twelve are out. The rule, recorded per fixture in
`MgdFixture::isMigrationFixture`:

**Reuse where a project earns its place by being a real arrangement** that
nobody would have written down. That is the whole value of this directory:
eight tracks carrying sixty-eight MIDI clips, one audio clip and an automation
lane is a shape a hand-written case never has.

**Reuse one that hosts a plugin only with the declaration that goes with it.**
Roughly half of these do: retrovid and envfollower carry Retrospect, groups
carries Pro-L 2, overlaps carries Pianoteq. Three of them are fixtures now
(#2175); what makes that possible is that the native engine hosts a VST3 (#1893)
and that the two things a plugin project cannot pretend about are declared.

A project with a VST3 in it has no null a corpus can hold it to: a plugin frames
its own work, so it renders at `AudioTier::Invariants` and the fixture is refused
if it asks for anything else. And a project rendered without the plugin it names
is a different project -- both legs bind nothing in that slot and null against
each other perfectly -- so `MgdFixture::hostedPlugins` names them, the rig
refuses a plugin the manifest missed and a name no device claims, and the runner
does not render the case at all on a machine that has not scanned one. Every CI
runner is such a machine, which is why those three are reported not run there
rather than green.

**Save new where the material has to be designed.** A fixture reused from here
can never be asked for one bar more, for a source that suits a case better, or
for a device it would rather exercise, because that would mean rewriting bytes
that may not be rewritten. A case that needs any of those needs its own project,
saved for the render corpus and free to be resaved.

A fixture saved for the render corpus does not belong in this directory. It is
not a migration fixture, it carries none of the guarantees above, and putting it
here would make the rule about never rewriting apply to a file the rule was never
meant to cover.
