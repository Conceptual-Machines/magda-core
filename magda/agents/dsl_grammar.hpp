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
         | groove_statement

// Track statements
track_statement: "track" "(" params? ")" chain?

// Filter statements (for bulk operations)
filter_statement: "filter" "(" "tracks" "," condition ")" chain?

// Groove statements (swing/shuffle templates)
groove_statement: "groove" "." groove_method

groove_method: "new" "(" params ")"
             | "extract" "(" params ")"
             | "set" "(" params ")"
             | "list" "(" ")"

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
     | function_call
     | array

function_call: IDENTIFIER "(" value ("," value)* ")"

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
- .fx.add(name="<plugin_alias>") - Add third-party plugin using alias token (e.g. <serum_2>, <pro_q_3>, <surge_xt>)
- .fx.add(name="Pro-Q 3") - Add plugin by exact display name (prefer alias tokens when available)
- .fx.add(name="Pro-Q 3", format="VST3") - Add plugin with format hint (VST3, AU, VST)
- .delete() - Delete track
- .clip.rename(index=0, name="Intro") - Rename clip at index on track
- .clip.rename(name="Intro") - Rename all currently selected clips (omit index)
- .clip.rename(name="Clip {i}") - Rename selected clips with auto-numbering: Clip 1, Clip 2, etc.
- .clip.delete(index=0) - Delete clip at index on track
- .select() - Select track in the UI
- .clips.select(clip.length_bars >= 2) - Select clips matching predicate (numeric fields: length_bars, start_bar, length, start, start_beats, id, track_id; string fields: name, type; ops: ==, !=, >, >=, <, <=; string fields support == and != only)

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
- "add <pro_q_3> to track 1" -> track(id=1).fx.add(name="<pro_q_3>")
- "create a track with <surge_xt>" -> track(name="Surge XT", new=true).fx.add(name="<surge_xt>")
- "add reverb and delay to the Vocals track" ->
  track(name="Vocals").fx.add(name="reverb")
  track(name="Vocals").fx.add(name="delay")
- "rename the first clip on track 1 to Intro" -> track(id=1).clip.rename(index=0, name="Intro")
- "rename selected clips to FOO" -> track(id=1).clip.rename(name="FOO")   // omit index to rename selected clips
- "select track 1" -> track(id=1).select()
- "select all clips longer than 2 bars on track 1" -> track(id=1).clips.select(clip.length_bars > 2)
- "select all clips shorter than or equal to 1 bar on track 2" -> track(id=2).clips.select(clip.length_bars <= 1)
- "select the clip named Intro" -> track(id=1).clips.select(clip.name == "Intro")

NOTE EDITING OPERATIONS (require a selected clip; these edit EXISTING notes only — note generation is handled by a separate music agent):
- .notes.select(note.pitch == C4) - Select notes matching predicate (fields: pitch, velocity, start_beat, length_beats; pitch accepts MIDI numbers or note names like C4, C#4, Bb3; C4=60)
- .notes.delete() - Delete currently selected notes
- .notes.transpose(semitones=5) - Transpose selected notes (positive=up, negative=down)
- .notes.set_pitch(pitch=F1) - Set pitch of selected notes (accepts note names like C4, F#3 or MIDI numbers)
- .notes.set_velocity(value=80) - Set velocity of selected notes
- .notes.quantize(grid=0.25) - Quantize selected notes (0.25=16th, 0.5=8th, 1.0=quarter)
- .notes.resize(length=0.5) - Set note length in beats

EXAMPLES (note editing):
- "select all C4 notes on track 1" -> track(id=1).notes.select(note.pitch == C4)
- "select notes with velocity above 100" -> track(id=1).notes.select(note.velocity > 100)
- "transpose selected notes up 5 semitones" -> track(id=1).notes.transpose(semitones=5)
- "set the note to F1" -> track(id=1).notes.set_pitch(pitch=F1)
- "set velocity to 60" -> track(id=1).notes.set_velocity(value=60)
- "delete selected notes" -> track(id=1).notes.delete()
- "quantize selected notes to 16th notes" -> track(id=1).notes.quantize(grid=0.25)
- "set note length to half a beat" -> track(id=1).notes.resize(length=0.5)

IMPORTANT: You do NOT generate musical content (melodies, chords, arpeggios, drum patterns, basslines). Those requests are handled by a separate music agent. Your job is structural: create tracks, add FX/instruments, create clips, edit existing notes. If the user asks for a track plus musical content, emit only the track/fx/clip setup — the music agent will fill the clip with notes.

GROOVE/SWING OPERATIONS (timing feel, NOT drum patterns):
These commands control swing/shuffle timing applied at playback. They do NOT create notes or clips.
- groove.new(name="My Swing", notesPerBeat=4, shifts="0.0,0.15,-0.05,0.4") - Create a custom groove template. shifts = comma-separated lateness values (-1.0 to 1.0, 0=on grid, positive=late/behind beat, negative=early/ahead). notesPerBeat: 2=8th notes, 4=16th notes.
- groove.extract(clip=0, resolution=16, name="Loop Feel") - Extract groove from audio clip transients. clip=index on track, resolution=8 or 16.
- groove.set(template="Basic 8th Swing", strength=0.7) - Apply groove template to current MIDI clip. strength=0.0-1.0.
- groove.list() - List all available groove templates.

EXAMPLES (groove):
- "add swing to this clip" -> groove.set(template="Basic 8th Swing", strength=0.5)
- "create a funky 16th groove" -> groove.new(name="Funky 16ths", notesPerBeat=4, shifts="0.0,0.2,-0.05,0.35,0.0,0.15,-0.1,0.3")
- "extract the groove from this audio clip" -> groove.extract(clip=0, resolution=16, name="Extracted Feel")
- "what grooves are available" -> groove.list()

IMPORTANT: "groove" means TIMING/SWING, not a drum pattern. If the user asks to "add groove/swing/shuffle" to a clip, use groove.set(). Requests for actual drum patterns / beats are handled by the music agent, not you.

BUILT-IN FUNCTIONS:
- random(min, max) - Returns a random value between min and max (inclusive). Integer if both args are integers, float otherwise.
  Example: .clip.new(length_bars=random(1, 4)) - Create a clip with random length between 1 and 4 bars

NOTE: The DAW state JSON includes "selected_track_id" when a track is selected, and "selected_clip_index" / "selected_clip_track_id" when a clip is selected.
Use track(id=N) to reference any track. When the user says "this track" or implies the current selection, use the selected_track_id from the state.
For note operations, use the selected_clip_track_id to reference the track owning the selected clip.

**CRITICAL: Always generate DSL code. Never generate plain text responses.**
)DESC";
}

}  // namespace magda::dsl
