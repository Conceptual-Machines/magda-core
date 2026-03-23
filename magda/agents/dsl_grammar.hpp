#pragma once

// ============================================================================
// MAGDA DSL Grammar (Lark format)
// ============================================================================
// Ported from magda-reaper/src/dsl/magda_dsl_grammar.h
//
// This grammar is sent to OpenAI for CFG-constrained output.
// The LLM generates DSL code that matches this grammar exactly.
// The DSL interpreter then executes it against TrackManager/ClipManager.

namespace magda::dsl {

inline const char* getGrammar() {
    return R"GRAMMAR(
// MAGDA DSL Grammar - Lark format
// Functional DSL for DAW operations

start: statement+

statement: track_statement
         | filter_statement

// Track statements
track_statement: "track" "(" params? ")" chain?

// Filter statements (for bulk operations)
filter_statement: "filter" "(" "tracks" "," condition ")" chain?

condition: "track" "." IDENTIFIER "==" value

clip_condition: "clip" "." IDENTIFIER compare_op value

note_condition: "note" "." IDENTIFIER compare_op value

compare_op: "==" | "!=" | ">=" | "<=" | ">" | "<"

// Method chain
chain: method+

method: "." method_call

method_call: "clip" "." "new" "(" params? ")"
           | "clip" "." "rename" "(" params? ")"
           | "clip" "." "delete" "(" params? ")"
           | "clips" "." "select" "(" clip_condition ")"
           | "track" "." "set" "(" params? ")"
           | "fx" "." "add" "(" params? ")"
           | "notes" "." "add" "(" params? ")"
           | "notes" "." "add_chord" "(" params? ")"
           | "notes" "." "add_arpeggio" "(" params? ")"
           | "notes" "." "select" "(" note_condition ")"
           | "notes" "." "delete" "(" ")"
           | "notes" "." "transpose" "(" params? ")"
           | "notes" "." "set_pitch" "(" params? ")"
           | "notes" "." "set_velocity" "(" params? ")"
           | "notes" "." "quantize" "(" params? ")"
           | "notes" "." "resize" "(" params? ")"
           | "delete" "(" ")"
           | "select" "(" ")"
           | "for_each" "(" chain ")"

// Parameters
params: param ("," param)*

param: IDENTIFIER "=" value

value: STRING
     | NUMBER
     | BOOLEAN
     | IDENTIFIER
     | array

// Array for progression chords
array: "[" array_items? "]"
array_items: IDENTIFIER ("," IDENTIFIER)*

// Terminals
STRING: "\"" /[^"]*/ "\""
NUMBER: /-?[0-9]+(\.[0-9]+)?/
BOOLEAN: "true" | "false" | "True" | "False"
IDENTIFIER: /[a-zA-Z_#][a-zA-Z0-9_#]*/

// Whitespace and comments
%import common.WS
%ignore WS
COMMENT: "//" /[^\n]/*
%ignore COMMENT
)GRAMMAR";
}

inline const char* getToolDescription() {
    return R"DESC(
**YOU MUST USE THIS TOOL TO GENERATE YOUR RESPONSE. DO NOT GENERATE TEXT OUTPUT DIRECTLY.**

Executes DAW operations using the MAGDA DSL. Generate functional script code.
Each command goes on a separate line. Track operations execute FIRST, then musical content is added.

TRACK OPERATIONS:
- track() - Create new track
- track(name="Bass") - Create or reference track by name (creates if no track with that name exists, otherwise references the existing one)
- track(name="Bass", new=true) - Always create a new track, even if one with the same name exists
- track(id=1) - Reference existing track (1-based index)

IMPORTANT: When the user says "create a track", always use new=true to ensure a fresh track is created.

METHOD CHAINING:
- .clip.new(bar=3, length_bars=4) - Create MIDI clip at bar
- .clip.new(length_bars=4) - Create MIDI clip after the last clip on the track (omit bar to auto-place)
- .track.set(name="X", volume_db=-3, pan=0.5, mute=true, solo=true)
- .fx.add(name="eq") - Add internal FX (eq, compressor, reverb, delay, chorus, phaser, filter, utility, pitch shift, ir reverb)
- .fx.add(name="Pro-Q 3") - Add scanned third-party plugin by name
- .fx.add(name="Pro-Q 3", format="VST3") - Add plugin with format hint (VST3, AU, VST)
- .delete() - Delete track
- .clip.rename(index=0, name="Intro") - Rename clip at index on track
- .clip.rename(name="Intro") - Rename all currently selected clips (omit index)
- .clip.rename(name="Clip {i}") - Rename selected clips with auto-numbering: Clip 1, Clip 2, etc.
- .clip.delete(index=0) - Delete clip at index on track
- .select() - Select track in the UI
- .clips.select(clip.length_bars >= 2) - Select clips matching predicate (fields: length_bars, start_bar; ops: ==, !=, >, >=, <, <=)

FILTER OPERATIONS (bulk):
- filter(tracks, track.name == "X").delete() - Delete all tracks named X
- filter(tracks, track.name == "X").track.set(mute=true) - Mute all tracks named X
- filter(tracks, track.name == "X").select() - Select all matching tracks
- filter(tracks, track.name == "X").for_each(.clip.new(bar=1, length_bars=4)) - Apply operations to each matched track
- filter(tracks, track.name == "X").for_each(.fx.add(name="reverb").track.set(mute=true)) - Chain multiple operations per track

EXAMPLES:
- "create a bass track" -> track(name="Bass")
- "create a drums track and mute it" -> track(name="Drums").track.set(mute=true)
- "delete track 1" -> track(id=1).delete()
- "mute all tracks named Drums" -> filter(tracks, track.name == "Drums").track.set(mute=true)
- "create a track called Lead and add a 4 bar clip at bar 1" ->
  track(name="Lead").clip.new(bar=1, length_bars=4)
- "set volume of track 2 to -6 dB" -> track(id=2).track.set(volume_db=-6)
- "add an EQ to the Bass track" -> track(name="Bass").fx.add(name="eq")
- "add Pro-Q 3 to track 1" -> track(id=1).fx.add(name="Pro-Q 3")
- "create a track with Surge XT" -> track(name="Surge XT", new=true).fx.add(name="Surge XT")
- "add reverb and delay to the Vocals track" ->
  track(name="Vocals").fx.add(name="reverb")
  track(name="Vocals").fx.add(name="delay")
- "rename the first clip on track 1 to Intro" -> track(id=1).clip.rename(index=0, name="Intro")
- "rename selected clips to FOO" -> track(id=1).clip.rename(name="FOO")   // omit index to rename selected clips
- "select track 1" -> track(id=1).select()
- "select all clips longer than 2 bars on track 1" -> track(id=1).clips.select(clip.length_bars > 2)
- "select all clips shorter than or equal to 1 bar on track 2" -> track(id=2).clips.select(clip.length_bars <= 1)

NOTE OPERATIONS (require a selected clip):
- .notes.select(note.pitch == C4) - Select notes matching predicate (fields: pitch, velocity, start_beat, length_beats; pitch accepts MIDI numbers or note names like C4, C#4, Bb3; C4=60)
- .notes.add(pitch=C4, beat=0, length=1, velocity=100) - Add a note (pitch can be note name or MIDI number)
- .notes.add_chord(root=C4, quality=major, beat=0, length=1, velocity=100, inversion=0) - Add a chord (qualities: major/maj, minor/min, dim, aug, sus2, sus4, dom7/7, maj7, min7, dim7, dom9/9, maj9, min9, dom11/11, min11, maj11, dom13/13, min13, maj13, add9, add11, add13, madd9, 6/maj6, min6, 7b5, 7sharp5, 7b9, 7sharp9, min7b5/half_dim, power/5; inversion: 0=root, 1=first, 2=second)
- .notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, note_length=0.5, velocity=100, inversion=0, pattern=up, fill=true) - Add an arpeggio (same qualities as add_chord; step=beat interval between notes; note_length defaults to step; pattern: up, down, updown; fill=true repeats the pattern to fill the entire clip; beats=8 repeats for exactly 8 beats — use this to split a clip between multiple arpeggios)
- .notes.delete() - Delete currently selected notes
- .notes.transpose(semitones=5) - Transpose selected notes (positive=up, negative=down)
- .notes.set_pitch(pitch=F1) - Set pitch of selected notes (accepts note names like C4, F#3 or MIDI numbers)
- .notes.set_velocity(value=80) - Set velocity of selected notes
- .notes.quantize(grid=0.25) - Quantize selected notes (0.25=16th, 0.5=8th, 1.0=quarter)
- .notes.resize(length=0.5) - Set note length in beats

EXAMPLES (note operations):
- "select all C4 notes on track 1" -> track(id=1).notes.select(note.pitch == C4)
- "select notes with velocity above 100" -> track(id=1).notes.select(note.velocity > 100)
- "transpose selected notes up 5 semitones" -> track(id=1).notes.transpose(semitones=5)
- "set the note to F1" -> track(id=1).notes.set_pitch(pitch=F1)
- "set velocity to 60" -> track(id=1).notes.set_velocity(value=60)
- "delete selected notes" -> track(id=1).notes.delete()
- "add a D4 note at beat 2" -> track(id=1).notes.add(pitch=D4, beat=2, length=1, velocity=100)
- "quantize selected notes to 16th notes" -> track(id=1).notes.quantize(grid=0.25)
- "set note length to half a beat" -> track(id=1).notes.resize(length=0.5)
- "add a C major chord at beat 0" -> track(id=1).notes.add_chord(root=C4, quality=major, beat=0, length=1)
- "add an A minor 7th chord for 2 beats" -> track(id=1).notes.add_chord(root=A3, quality=min7, beat=0, length=2)
- "add a C major chord first inversion" -> track(id=1).notes.add_chord(root=C4, quality=major, inversion=1)
- "add a C major arpeggio" -> track(id=1).notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5)
- "add a descending A minor arpeggio" -> track(id=1).notes.add_arpeggio(root=A3, quality=min, beat=0, step=0.25, pattern=down)
- "add a clip with an E minor arpeggio over 2 bars" -> track(id=1).clip.new(length_bars=2).notes.add_arpeggio(root=E4, quality=min, beat=0, step=0.5, fill=true)
- "add an up-down C major 7 arpeggio" -> track(id=1).notes.add_arpeggio(root=C4, quality=maj7, beat=0, step=0.5, pattern=updown)
- "arpeggio from E minor to C major over 4 bars" ->
  track(id=1).clip.new(length_bars=4).notes.add_arpeggio(root=E4, quality=min, beat=0, step=0.5, beats=8).notes.add_arpeggio(root=C4, quality=major, beat=8, step=0.5, beats=8)
- "add a melody in E minor" ->
  track(id=1).clip.new(length_bars=4).notes.add(pitch=E4, beat=0, length=0.5, velocity=100).notes.add(pitch=G4, beat=0.5, length=0.5, velocity=98).notes.add(pitch=B4, beat=1.0, length=1.0, velocity=102).notes.add(pitch=A4, beat=2.0, length=0.5, velocity=96).notes.add(pitch=G4, beat=2.5, length=0.5, velocity=95).notes.add(pitch=F#4, beat=3.0, length=0.5, velocity=94).notes.add(pitch=E4, beat=3.5, length=0.5, velocity=100)

IMPORTANT: To add multiple notes to the SAME clip, chain .notes.add() calls on a SINGLE statement. Do NOT create a separate clip.new() for each note.

NOTE: The DAW state JSON includes "selected_track_id" when a track is selected, and "selected_clip_index" / "selected_clip_track_id" when a clip is selected.
Use track(id=N) to reference any track. When the user says "this track" or implies the current selection, use the selected_track_id from the state.
For note operations, use the selected_clip_track_id to reference the track owning the selected clip.

**CRITICAL: Always generate DSL code. Never generate plain text responses.**
)DESC";
}

}  // namespace magda::dsl
