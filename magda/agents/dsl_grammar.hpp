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
         | note_statement
         | chord_statement
         | arpeggio_statement
         | progression_statement
         | pattern_statement

// Track statements
track_statement: "track" "(" params? ")" chain?

// Filter statements (for bulk operations)
filter_statement: "filter" "(" "tracks" "," condition ")" chain?

// Musical content statements (added to most recently created track)
note_statement: "note" "(" params ")"
chord_statement: "chord" "(" params ")"
arpeggio_statement: "arpeggio" "(" params ")"
progression_statement: "progression" "(" params ")"
pattern_statement: "pattern" "(" params ")"

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
- track(name="Bass") - Create track with name
- track(name="Bass", type="audio") - Create track with type (audio, midi)
- track(id=1) - Reference existing track (1-based index)

METHOD CHAINING:
- .clip.new(bar=3, length_bars=4) - Create MIDI clip at bar
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
- "create a bass track" -> track(name="Bass", type="audio")
- "create a drums track and mute it" -> track(name="Drums", type="audio").track.set(mute=true)
- "delete track 1" -> track(id=1).delete()
- "mute all tracks named Drums" -> filter(tracks, track.name == "Drums").track.set(mute=true)
- "create a midi track called Lead and add a 4 bar clip at bar 1" ->
  track(name="Lead", type="midi").clip.new(bar=1, length_bars=4)
- "set volume of track 2 to -6 dB" -> track(id=2).track.set(volume_db=-6)
- "add an EQ to the Bass track" -> track(name="Bass").fx.add(name="eq")
- "add Pro-Q 3 to track 1" -> track(id=1).fx.add(name="Pro-Q 3")
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

NOTE: The DAW state JSON includes "selected_track_id" when a track is selected, and "selected_clip_index" / "selected_clip_track_id" when a clip is selected.
Use track(id=N) to reference any track. When the user says "this track" or implies the current selection, use the selected_track_id from the state.
For note operations, use the selected_clip_track_id to reference the track owning the selected clip.

**CRITICAL: Always generate DSL code. Never generate plain text responses.**
)DESC";
}

}  // namespace magda::dsl
