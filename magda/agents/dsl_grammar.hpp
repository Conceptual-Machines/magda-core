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

// Method chain
chain: method+

method: "." method_call

method_call: "new_clip" "(" params? ")"
           | "set_track" "(" params? ")"
           | "add_fx" "(" params? ")"
           | "addAutomation" "(" params? ")"
           | "add_automation" "(" params? ")"
           | "delete" "(" ")"
           | "delete_clip" "(" params? ")"
           | "map" "(" func_ref ")"
           | "for_each" "(" func_ref ")"

// Function reference for map/for_each
func_ref: "@" IDENTIFIER

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
- .new_clip(bar=3, length_bars=4) - Create MIDI clip at bar
- .set_track(name="X", volume_db=-3, pan=0.5, mute=true, solo=true)
- .delete() - Delete track

FILTER OPERATIONS (bulk):
- filter(tracks, track.name == "X").delete() - Delete all tracks named X
- filter(tracks, track.name == "X").set_track(mute=true) - Mute all tracks named X

EXAMPLES:
- "create a bass track" -> track(name="Bass", type="audio")
- "create a drums track and mute it" -> track(name="Drums", type="audio").set_track(mute=true)
- "delete track 1" -> track(id=1).delete()
- "mute all tracks named Drums" -> filter(tracks, track.name == "Drums").set_track(mute=true)
- "create a midi track called Lead and add a 4 bar clip at bar 1" ->
  track(name="Lead", type="midi").new_clip(bar=1, length_bars=4)
- "set volume of track 2 to -6 dB" -> track(id=2).set_track(volume_db=-6)

**CRITICAL: Always generate DSL code. Never generate plain text responses.**
)DESC";
}

}  // namespace magda::dsl
