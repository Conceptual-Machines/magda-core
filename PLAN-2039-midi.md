# Slice 6: MIDI clip playback (#2039)

Branch `feat/2039-midi-clips`, off `dev/0.19.0`.

The source behind a track's `ClipMidi` op. Audio and MIDI share the snapshot, the
span and the interior silences, and share nothing below that: there is no file,
no reader, no stretcher and no pool here. What there is instead is a question
audio never has to answer, which is what a note that has already sounded is owed.

## The shape: a compiled event list, not a model to interpret

Slice 1 put the model's own lists in the snapshot: `notes`, `cc`, `pitchBend`,
plus a groove template *name*. That was the right first move and it is not what
plays. Between a `MidiNote` and a note-on stand the groove, the curve
densification, the MPE channel assignment, the same-pitch overlap rule and the
two offsets, and every one of those is a question about the model with one answer
that never changes until the model does.

So `MidiClipPlayback` carries a compiled event list instead, exactly as
`AudioEventPlayback` carries a compiled `WarpMap` rather than the marker list it
is built from. Everything above resolves once, off the audio thread, in the
compiler that already owns the rest of the resolution. What reaches the callback
is one sorted array of short messages in content beats, and the callback does a
binary search and a walk.

This is also what the incumbent does, by a longer road: TE builds a playback
sequence per clip (`MidiList::createDefaultPlaybackMidiSequence`) and its node
walks that. The difference is where the sequence lives and when it is rebuilt.

### What one event is

    struct MidiClipEvent {
        double beat;          // content beats, the clip's own domain
        std::uint8_t status;  // channel included
        std::uint8_t data1, data2;
        std::int32_t endsAt;  // note-on only: index of its note-off, or -1
    };

Three bytes, because a clip has no SysEx: the model holds notes, CC and pitch
bend and nothing else, which is what keeps the per-block cost bounded by events
rather than by bytes and is worth saying out loud given the port budget below.

`endsAt` is the only field with no counterpart in the model. It is what makes
"which notes are sounding at this instant" a question the list can answer without
a scan of unbounded length, and that question is asked on every locate.

Ordering at equal beats is fixed and part of the compile: controllers, then pitch
bend, then note-offs, then note-ons. Controllers first because a program or bank
change has to land before the note it configures, which is TE's rule too; offs
before ons because two notes of the same pitch meeting exactly is otherwise a
coin toss between a retrigger and a hung note.

### Curves become events

The model holds a curve as a few points with a shape between them; a synth reads
messages. Something has to decide how many. The curve itself is evaluated with
the formulas `ClipSynchronizer::interpolateCCEvents` already uses, tension and
bezier handles included, which are the same ones the curve editor draws with.
What changes is where the messages land.

**On every change of the quantised value, and no closer together than a
millisecond.** Not the incumbent's 1/16-beat grid, and not per sample or per
block either, for three separate reasons.

Per block is not available. `RenderContext.hpp` requires that a shorter block
render identically, because block size is an I/O batching concept and never a
precision one; a curve resolved per callback would make the offline render
disagree with playback and the null-diff corpus would be comparing two things
that were never the same.

Per sample is the wrong axis. The ceiling is not the sample rate, it is the
value resolution: a CC is seven bits, so a full sweep has a hundred and
twenty-eight distinct values however many samples it crosses, and pitch bend is
fourteen, so sixteen thousand three hundred and eighty-four. Emitting on every
change is therefore already the densest message set that means anything, and for
pitch bend it is far too dense to afford: a fast dive is 147 KB against a port
budget of four thousand and ninety-six bytes, which is thirty-six times over.
That is what the floor bounds, and it is why a floor is needed whatever grid is
chosen.

The 1/16 grid is anchored to the wrong axis, which makes it both too dense and
too sparse depending on the curve. A ramp from 64 to 65 over eight bars emits
five hundred and twelve identical-to-within-a-unit messages on the grid and two
here. A pitch-bend dive over a hundred milliseconds gets three grid points at
120 BPM, an audible staircase, and a hundred here. It also moves with tempo,
running at 8 Hz at 30 BPM and 128 Hz at 480 for the same drawn curve, when
smoothness is a wall-clock property.

Emitting on value change is what makes both of those come out right without a
number to tune: every message changes something, and nothing that changes is
missed until the floor bites. The incumbent's constant-segment guard and its
`Step` handling stop being special cases and fall out.

The floor is in seconds, converted through the tempo map the snapshot is already
compiled against. A tempo curve makes it approximate by the tempo ratio, which
costs nothing: it is a bound rather than a rate, and what it bounds is checked
by the port budget below rather than by the conversion being exact.

This is a deliberate divergence, and a wider one than it looks; what the corpus
has to do about it is below. It does not walk back into #1193: a millisecond
floor caps a controller at about ten messages per block, which is where that
hazard came from, and it caps it in wall-clock rather than in beats, which the
grid never did.

### Groove becomes a table

`clip/GrooveTemplate.{hpp,cpp}`: a native port of the fork's template, which is
a lateness table, a note count, a notes-per-beat and one formula
(`GrooveTemplate::beatsTimeToGroovyTime`). Small, exact, and portable, the same
call the transient detector was in slice 5.

Compiled the way the warp map is: the strength is folded into the table at
compile time, since whether strength applies at all is the template's
`parameterized` flag and that is a fact about the template rather than about the
block. An empty or absent template compiles to nothing and the clip is
groove-agnostic, which is what a clip with no groove and one whose latenesses are
all zero both get.

Applied to note edges only, never to controllers, and applied in **timeline**
beats rather than clip beats: the fork adds the clip's content start before
grooving and subtracts the clip's start after (`MidiNote::getPlaybackBeats`), so
the groove grid is anchored to the project rather than to wherever the clip
happens to sit. A clip dragged half a beat does not take its swing with it.

**Applied after the fold, at emit time, per pass.** The table is compiled; the
lookup is not, and this is the one thing in the slice that cannot be resolved
ahead of the block.

Anchoring to the project grid and grooving every pass alike are not the same
property, and they only coincide when the loop length is a whole multiple of the
template's period. A 1.5-beat loop under a per-beat swing starts its second pass
on the off-beat, so timeline anchoring says that pass swings differently and a
compile-time bake says it swings like the first. The fork delivers the anchoring:
`LoopedMidiEventGenerator::setLoopIndex` re-times the whole sequence to
`clipRange.start + loopIndex * loopLength` and grooves it there, once per pass.
Baking before the fold would diverge on every odd-length loop, which the corpus
would find and which nothing in the model forbids anyone from making.

What that costs is small and has to be stated because it is the only unbounded-
looking thing on the audio thread. Grooving moves events, so the search window
over the compiled list widens by the table's largest displacement, which the
compile can hand over as a number: the formula shifts by at most half a
lateness step, so half a beat divided by notes-per-beat, and a compiled table
knows its own worst case. Grooving can also reorder, so the block's candidates
are grooved into a fixed-capacity scratch array and sorted there, which is what
the fork's `currentSequence.sortEvents()` after `applyGrooveToSequence` is doing.
Bounded by the events a widened block window holds, no allocation.

Two guards the fork does not have. A grooved note-off never precedes its own
note-on: both edges are grooved independently, at different beats, so the pair
can invert and the clamp is a line. And the chase below grooves through the same
call over the same widened window, because "which notes are sounding here" is a
question about where they sound rather than where they were written.

Where the templates come from is the caller's business, as everything else the
compiler is handed already is (`ClipSourceInfo`, the tempo map). It takes a
`GrooveTemplateSet`: a name-to-template map, parsed from the same
`<GROOVETEMPLATES>` XML the fork's manager reads out of the user settings. An
empty set is legal and means no clip grooves, which is what the engine gets until
the app is switched over. Not rewired into the app in this slice, for the reason
slice 5 gave for the detector: swapping the app's source of these is part of
switching the engine on rather than part of implementing it.

### MPE

When any note in the clip carries pitch expression, the clip compiles to MPE, as
the incumbent decides the same thing with `setMPEMode(anyPitchExpression)`. Lower
zone, master channel 1, member channels 2 to 16, assigned round-robin with the
fork's avoid-the-last-pitch rule, and freed as notes end.

Expression densifies on value change with the same floor as any other curve, at
TE's fixed +/-48 semitone range. The 1/16 grid it uses today is not TE's rule at
all, it is `addPitchExpressionToTeNote` in MAGDA's own sync layer picking the
same arbitrary number twice, and there is no parity argument for keeping it here:
per-note expression is pitch, so it is the curve the staircase argument bites
hardest on.

Without expression a clip plays on channel 1. The model has no per-clip or
per-track MIDI channel, so there is nothing else it could be.

### Overlaps

A note whose note-off would land after another note of the same pitch has already
started gets no note-off at all (the fork's `useNoteUp = false`). Emitting it
would cut the second note short, and this is the compile-time half of the
lifetime invariant below: a note-on with `endsAt == -1` is a note the clip never
ends, and something else has to.

## The fold: a loop is a coordinate change, not a copy

The fork unrolls: `createLoopRangeDefinesAllRepetitionsSequence` writes out one
copy of the sequence per pass, so a two-bar loop under a sixty-four-bar clip is
thirty-two copies, rebuilt whenever a note moves. Nothing here needs that. A
block is a beat range, and folding a range through a loop gives a handful of
passes, each a sub-range of the same list.

    contentBeat(t) = (t - clipStartBeat) + trimOffset + midiOffset      // not looping
    contentBeat(t) = loopStart + mod(t - clipStartBeat + midiOffset,
                                     loopLength)                       // looping

`trimOffset` is the content origin a left-resize left behind and only exists when
the clip is not looping, which is what the model's own `getMidiVisibleRange`
already says. `midiOffset` is the phase and applies either way; the incumbent's
arranger path drops it when the clip is not looping, which is a gap in the sync
layer rather than a semantic, and the session path does not have it.

Per pass the window is `[loopStart, loopStart + loopLength)` and a note is
clipped to it at both ends, which is the fork's unrolling rule read the other
way: a note reaching past the loop end is cut there, and a note starting before
the loop start sounds from the loop point with what is left of it. Keeping that
rule is what stops the fold being a source of hung notes, because the note-off of
every note that sounds inside a pass is inside the same pass.

A cap on passes per block, counted rather than asserted, the way
`ClipAudioSource::starvedVoices` counts. A loop shorter than a block is
pathological and reachable: `loopLengthBeats` has no floor in the model.

**No visible-range crop.** The incumbent computes a window and clips the note
list to it because TE needs the sequence to be the clip. Here the span already
is the crop, and it is the resolved span the lane left audible rather than the
clip's placement. That is the same rule audio plays by, one fewer concept, and it
is what makes the next paragraph free.

## Turning looping off must not shorten the clip

`MidiClip::disableLooping` truncates the clip to one loop length, which is why
flattening a looped MIDI clip in MAGDA today plays only its first cycle and why
`ClipOperations` has to clear `loopEnabled`, `midiOffset` and `midiTrimOffset`
by hand after unrolling the notes. Nothing in this engine reads a loop as a
length: the span is the length and the loop is a fold above it, so the clip whose
looping was turned off is the same clip with the fold switched off. The model's
workaround stays where it is, because the incumbent still needs it.

## Note lifetime

The invariant, and the thing the slice is judged on: **the source never emits a
note-off for a note it did not start, and never fails to emit one for a note it
did.** Making that structural rather than careful is what an active-note list is
for. Sixteen channels by a hundred and twenty-eight notes of bits, two hundred
and fifty-six bytes, owned by the source and outliving every clip, block and plan
that passes through it.

Five things end a note, and only the first falls out of the material:

- **A pass ending, or a clip's span ending.** Already in the list, because the
  fold clipped the note to the pass and the compile clipped it to the span. The
  note-off lands on the last sample of the range, which `sampleForBeat`'s clamp
  does without being asked.
- **A locate or a loop wrap** (`!block.continuous`). Everything active goes off
  at sample 0, before anything else this block emits.
- **A stop** (`!block.playing`). Same, and then nothing until it rolls again.
- **A snapshot swap that moved what was sounding.** The source remembers the
  snapshot it last saw. When the pointer changes, it asks the new one what should
  be sounding at this block's start and sends note-offs for active notes that are
  not in that set. This is the fork's `shouldSendNoteOffsForNotesNoLongerPlaying`
  rule and it costs one comparison per block that is not a swap.
- **Destruction.** Nothing to do, and worth saying why: a source is destroyed
  when its track leaves the model, and its output port leaves with it, so there
  is nowhere for a note-off to go and nothing left downstream to hang.

## Chase

Every `!block.continuous` block, after the note-offs above: the controller and
pitch-bend value of each clip as of the seek instant, then note-ons for the notes
that instant is inside, at their remaining length. Both are what the fork does
(`createMessagesForTime`, `getNotesOnAtTime`), and dropping either is audible in
an ordinary way: seeking into a sustained pad would be silence until the next
note, and seeking past a mod-wheel sweep would play the rest of the phrase at the
wrong filter cutoff.

Both are answered by the compiled list. Controllers are the last event of each
`(channel, controller)` before the instant, which the compile can index; the
notes are `endsAt`, walked from the start of the pass.

The two halves of the slice meet here, and it is the one place where densifying
in the compiler rather than at sync time is visible from outside. The incumbent
writes its densified events into TE's sequence when the clip is edited, and on a
locate `chocMidiHelpers::createControllerUpdatesForTime` scans that sequence from
the top for the last event of each controller. So the value it chases with is
whatever the last grid point held, up to a sixteenth of a beat stale, and the
synth sits on that stale value until the next grid point arrives. Emitting on
value change makes the same lookup exactly right instead of nearly right: if
nothing was emitted since, nothing changed since, so the last event *is* the
current value by construction. The chase also stops costing a scan of the clip,
because the index is per controller and per compile rather than per locate.

The other consequence of compiling here is the one the snapshot exists for. The
incumbent's densification lives in `te::MidiClip`'s sequence, so editing a curve
clears and rebuilds it, TE's `TreeWatcher` sees the tree change and calls
`restartPlayback()`, and the graph is rebuilt underneath a playing transport,
which is the click `ClipSynchronizer` warns about in three places. Here an edit
compiles a new snapshot and swaps it in. No plan is recompiled, no graph is
rebuilt, and the only thing the audio thread notices is the swap rule already
written for it.

## Holes

An event whose instant lands in a silenced span is not emitted, which for a note
means it never sounds, and MIDI therefore plays around a hole rather than being
cut by it: a note that started before one goes on ringing through it. Note-offs
are exempt, unconditionally. A hole is a reason for a note not to start and never
a reason for one not to end.

## The port budget

`kMaxMidiBytesPerPort` is four thousand and ninety-six, about four hundred and
fifty short messages over the longest block the plan was prepared for. Ordinary
playback is nowhere near it, and the millisecond floor is what guarantees that
rather than leaving it to how anyone drew a curve: ten messages per controller
per block, ninety bytes.

The chase is the one thing that can still approach it, because it is bounded by
the controllers a clip uses times the channels it uses rather than by time, and
under MPE that is fifteen channels.

So the source counts what it could not fit and stops, the way a track counts a
clip it had no voice for: `droppedEvents()`, readable off the audio thread,
because a note nobody counted is indistinguishable from a note nobody wrote.

**A note-off is never one of them.** Counting the drop is enough for a note-on or
a controller and not for an off, because dropping an off is precisely the failure
the whole slice is judged on, and a test that catches it is not the same as a
design that cannot do it. Two rules make it structural rather than lucky:

- Drops fall on note-ons and controllers first, and a note-on that is dropped is
  never entered in the active list, so it never comes to owe an off. The pressure
  is self-limiting: refusing to start notes is what stops offs accumulating.
- An off that still will not fit is carried rather than dropped. The active list
  already knows exactly what is owed, so the next block emits it at sample zero
  before anything else, which is the same path a locate uses. A note-off a block
  late is ten milliseconds of extra tail. A note-off that never comes is a note
  that rings until the session ends.

## The divergence record, and what #2040 does with it

Two deliberate departures from the incumbent, both written into
`docs/architecture/native-engine.md` beside the Signalsmith priming one rather
than left in a PR body.

- **Controller density.** Value change plus a floor, not a 1/16-beat grid.
- **`midiOffset` on an unlooped arranger clip.** The incumbent drops it
  (`setOffset(0)` in the else branch of `syncMidiClipToEngine`) while the session
  path applies it. Applying it either way is the reading I want, and it means a
  project that leant on the current behaviour plays differently under the engine.
  Small, and it is still a behaviour change and belongs in the record.

The first one needs #2040 to change shape rather than change tolerance, and this
is the reason: Signalsmith priming is a bounded onset artifact in one clip, while
this alters the controller stream feeding an arbitrary synth, so every project
with a live curve diverges everywhere the curve is live and for as long as it is.
Loosening the audio tolerance until that passes is how a real bug in the same
clip ends up hiding inside the expected difference.

So the corpus grows a channel. **The MIDI event stream is compared as its own
artifact**, where the difference is exact, explainable event by event, and a
regression in it is unambiguous. **Curve-driven audio comes out of the near-null
audio assertion** and is covered by that channel instead. Audio stays near-null
everywhere it can still be near-null, which is everything a curve does not reach,
and that is most of the corpus.

## Files

- `clip/MidiEventList.{hpp,cpp}` -- new. The compiled list, the fold, the
  controller index, notes-on-at-instant.
- `clip/MidiClipCompiler.{hpp,cpp}` -- new. Model to list: groove, curves, MPE,
  overlaps, offsets.
- `clip/GrooveTemplate.{hpp,cpp}` -- new. The native template, its formula, the
  compiled table with its own worst-case displacement, and the set the compiler
  is handed. Called from the audio thread, so the lookup allocates nothing.
- `clip/ActiveNoteList.hpp` -- new. What is sounding, and what is owed.
- `clip/ClipMidiSource.{hpp,cpp}` -- new. The source behind a `ClipMidi` op. Owns
  the active list, the grooving scratch and the carried offs.
- `clip/ClipSnapshot.hpp` -- `MidiClipPlayback` carries the compiled list, the
  compiled groove table and the fold's inputs; the model lists and the groove
  name leave.
- `clip/ClipSnapshotCompiler.{hpp,cpp}` -- takes a `GrooveTemplateSet`, compiles
  MIDI clips through the above, diagnoses what it drops.
- `clip/ClipSnapshotDump.cpp` -- event count, channel span, fold, groove extent.
- `magda/engine/CMakeLists.txt`, `tests/CMakeLists.txt`.
- `docs/architecture/native-engine.md` -- slice table, a MIDI section carrying
  the lifetime invariant and why value-change emission makes the chase exact by
  construction rather than nearly right, the two divergences beside the
  Signalsmith one, and the "not built yet" entry comes out.

## Tests

`tests/test_midi_clip_playback.cpp`, `[engine][clip][midi]`, plus
`test_groove_template.cpp` for the port.

- **The compile.** MPE assigns distinct channels to overlapping notes and reuses
  a freed one. Same-pitch overlap drops the first note-off.
- **Groove, including the case that decided where it runs.** It moves a note's
  start and its end and leaves a controller alone. It is anchored to the project
  grid, so the same clip at two placements grooves differently. Then the one the
  compile-time bake would have failed: a 1.5-beat loop under a per-beat template
  grooves its second pass differently from its first, because that pass starts
  off the pattern. And the reordering guard: a template late enough to move an
  event past its neighbour emits them in grooved order, and a grooved note-off
  never precedes its own note-on.
- **Densification, as two properties rather than a message count.** Every
  emitted message changes the quantised value from the one before it, and no two
  are closer than the floor: assert those over a bezier segment, a tension
  segment, a step run and a constant run, rather than golden-testing a list
  whose length is the thing under discussion. Then the shape itself, sampled at
  a handful of instants against the editor's own formulas. Then the two cases
  the grid got wrong in opposite directions: a ramp of one unit over eight bars
  emits its two endpoints, and a full pitch-bend dive over a hundred
  milliseconds is bounded by the floor rather than by fourteen bits.
- **The fold.** A block inside one pass, a block spanning a wrap, a block
  spanning several. A note clipped at the pass end, a note clipped at the pass
  start. `midiOffset` phases the loop; `trimOffset` moves the origin of an
  unlooped clip and is ignored by a looped one.
- **Lifetime, which is the slice.** For every one of: a wrap, a locate forwards,
  a locate backwards, a stop mid-note, a snapshot swap that deletes the clip, a
  snapshot swap that moves the note, and a span that ends mid-note, assert the
  same property rather than a message list, by running the block sequence through
  a checker that pairs ons with offs and fails on any survivor. A note left
  hanging is the failure the whole slice is judged on, so it is the assertion the
  whole test file is built on.
- **Chase.** Locating into a note starts it; locating past a controller sets it;
  locating into a hole starts nothing; under MPE the expression is reconstructed
  on the note's own channel.
- **Holes.** A note starting inside one does not sound, a note ringing into one
  keeps ringing and still ends.
- **Looping off does not shorten.** A clip with four cycles of notes and looping
  turned off plays all four.
- **The budget, which is a lifetime test wearing a different hat.** A clip built
  to overflow one block reports it rather than growing the buffer, and what it
  did emit is still paired. Then the case the design exists for: overflow while
  notes are owed offs, asserting the offs arrive, at sample zero of the next
  block if they could not fit in this one, and that a dropped note-on never
  produces an unmatched off.
