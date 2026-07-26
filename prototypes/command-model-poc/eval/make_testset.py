"""Build the fixed evaluation test set (curated NL -> gold actions).

Hand-authored intents; gold DSL is rendered canonically so labels never drift
from the renderer. Run once to (re)write eval/testset.jsonl, which is the
committed, version-pinned target every parser/model is scored against.

    python -m eval.make_testset
"""
from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from magda_dsl import dsl, vocab  # noqa: E402

T = vocab.resolve_plugin  # text -> token (legacy; plugins now use @alias below)
C = vocab.COLORS


def A(alias: str) -> str:
    """Plugin @alias surface -> DSL token: 'serum' -> '<serum>'. The model tags
    the @mention and passes it through; it never learns plugin identities."""
    return "<" + alias + ">"

# Each entry: (natural language, actions). Covers every supported command type
# plus phrasing variety (verbs, with/without "track", multi-plugin, bulk).
CASES = [
    # --- create_track ---
    ("create a bass track", [{"type": "create_track", "name": "Bass"}]),
    ("add a new drums track", [{"type": "create_track", "name": "Drums"}]),
    ("make a lead track", [{"type": "create_track", "name": "Lead"}]),
    ("new track called Pads", [{"type": "create_track", "name": "Pads"}]),
    ("create a track named Reese Bass", [{"type": "create_track", "name": "Reese Bass"}]),
    ("create a vocals track", [{"type": "create_track", "name": "Vocals"}]),
    ("make a keys track", [{"type": "create_track", "name": "Keys"}]),
    ("add a new perc track", [{"type": "create_track", "name": "Perc"}]),

    # --- create_track with plugins (@alias references) ---
    ("create a bass track with @serum and @ott",
     [{"type": "create_track", "name": "Bass", "plugins": [A("serum"), A("ott")]}]),
    ("make a lead track with @vital",
     [{"type": "create_track", "name": "Lead", "plugins": [A("vital")]}]),
    ("new pads track with @surge_xt and @reverb",
     [{"type": "create_track", "name": "Pads", "plugins": [A("surge_xt"), A("reverb")]}]),
    ("spin up a vocals track loaded with @pro_q_3",
     [{"type": "create_track", "name": "Vocals", "plugins": [A("pro_q_3")]}]),
    ("create a drums track with @massive and @ott",
     [{"type": "create_track", "name": "Drums", "plugins": [A("massive"), A("ott")]}]),
    # Unnamed track carrying a device. These used to land on create_rack — the
    # only unnamed "new X with @alias" shape in the training data was
    # "new rack with @serum" — producing a rack on the *selected* track instead
    # of a new track. Empty name renders as new=true, which the interpreter
    # creates rather than resolving against the selection.
    ("new track with @polysynth",
     [{"type": "create_track", "name": "", "plugins": [A("polysynth")]}]),
    ("create a track with @serum",
     [{"type": "create_track", "name": "", "plugins": [A("serum")]}]),
    ("add a track with @vital and @ott",
     [{"type": "create_track", "name": "", "plugins": [A("vital"), A("ott")]}]),

    # --- add_plugin (@alias; last case uses an alias absent from training to
    #     test the "generalize to ANY alias" claim) ---
    ("add @serum to the bass track", [{"type": "add_plugin", "name": "Bass", "plugin": A("serum")}]),
    ("put @ott on Drums", [{"type": "add_plugin", "name": "Drums", "plugin": A("ott")}]),
    ("insert @pro_q_3 on vocals", [{"type": "add_plugin", "name": "Vocals", "plugin": A("pro_q_3")}]),
    ("load @vital on the lead track", [{"type": "add_plugin", "name": "Lead", "plugin": A("vital")}]),
    ("throw @eq on the bass track", [{"type": "add_plugin", "name": "Bass", "plugin": A("eq")}]),
    ("add @reverb to Pads", [{"type": "add_plugin", "name": "Pads", "plugin": A("reverb")}]),
    ("put @1176 on the vocals track", [{"type": "add_plugin", "name": "Vocals", "plugin": A("1176")}]),
    ("add @granulator_x to the pads track",
     [{"type": "add_plugin", "name": "Pads", "plugin": A("granulator_x")}]),

    # --- rename_track ---
    ("rename track Bass to Reese Bass", [{"type": "rename_track", "name": "Bass", "new_name": "Reese Bass"}]),
    ("rename the drums track to Acoustic Kit", [{"type": "rename_track", "name": "Drums", "new_name": "Acoustic Kit"}]),
    ("change Lead to Topline", [{"type": "rename_track", "name": "Lead", "new_name": "Topline"}]),
    ("rename track Pads to Warm Pads", [{"type": "rename_track", "name": "Pads", "new_name": "Warm Pads"}]),

    # --- delete_track ---
    ("delete the bass track", [{"type": "delete_track", "name": "Bass"}]),
    ("remove track Drums", [{"type": "delete_track", "name": "Drums"}]),
    ("delete Pads", [{"type": "delete_track", "name": "Pads"}]),
    ("remove the vocals track", [{"type": "delete_track", "name": "Vocals"}]),


    # --- set_track_volume ---
    ("set Bass volume to -6 dB", [{"type": "set_track_volume", "name": "Bass", "volume_db": -6}]),
    ("turn the drums track to -3 dB", [{"type": "set_track_volume", "name": "Drums", "volume_db": -3}]),
    ("make Vocals 0 dB", [{"type": "set_track_volume", "name": "Vocals", "volume_db": 0}]),
    ("set volume of Pads to -12 dB", [{"type": "set_track_volume", "name": "Pads", "volume_db": -12}]),

    # --- set_track_pan ---
    ("pan Bass left", [{"type": "set_track_pan", "name": "Bass", "pan": -0.5}]),
    ("set Drums pan to center", [{"type": "set_track_pan", "name": "Drums", "pan": 0.0}]),
    ("move the vocals track hard right", [{"type": "set_track_pan", "name": "Vocals", "pan": 1.0}]),
    ("put Pads slightly left in the stereo field", [{"type": "set_track_pan", "name": "Pads", "pan": -0.25}]),

    # --- set_track_color ---
    ("make Drums red", [{"type": "set_track_color", "name": "Drums", "colour": C["red"]}]),
    ("color the bass track blue", [{"type": "set_track_color", "name": "Bass", "colour": C["blue"]}]),
    ("set Vocals colour to green", [{"type": "set_track_color", "name": "Vocals", "colour": C["green"]}]),
    ("color code drums red", [{"type": "set_track_color", "name": "Drums", "colour": C["red"]}]),
    ("make the pads track purple", [{"type": "set_track_color", "name": "Pads", "colour": C["purple"]}]),

    # --- group_tracks ---
    ("group tracks 1, 2 and 3 as Drums", [{"type": "group_tracks", "ids": [1, 2, 3], "name": "Drums"}]),
    ("group tracks 4 and 5 into Vocals", [{"type": "group_tracks", "ids": [4, 5], "name": "Vocals"}]),
    ("put tracks 1, 2 and 3 in a group called Rhythm",
     [{"type": "group_tracks", "ids": [1, 2, 3], "name": "Rhythm"}]),
    ("group tracks 2, 3 and 4 as Bass Bus", [{"type": "group_tracks", "ids": [2, 3, 4], "name": "Bass Bus"}]),

    # --- select_all_clips ---
    ("select all clips on Bass", [{"type": "select_all_clips", "name": "Bass"}]),
    ("select every clip on the drums track", [{"type": "select_all_clips", "name": "Drums"}]),
    ("select all clips in Vocals", [{"type": "select_all_clips", "name": "Vocals"}]),
    ("highlight all clips on the pads track", [{"type": "select_all_clips", "name": "Pads"}]),

    # --- chained clip selection + rename ---
    ("select all clips on Bass and rename them to Bass Clip",
     [{"type": "select_all_clips_rename", "name": "Bass", "new_name": "Bass Clip"}]),
    ("rename every clip on the drums track to Drum Clip",
     [{"type": "select_all_clips_rename", "name": "Drums", "new_name": "Drum Clip"}]),
    ("select all clips in Vocals then call them Vocal Take",
     [{"type": "select_all_clips_rename", "name": "Vocals", "new_name": "Vocal Take"}]),

    # --- clip predicate selection ---
    ("select clips named Intro on Bass",
     [{"type": "select_clips_named", "name": "Bass", "clip_name": "Intro"}]),
    ("select the clip called Chorus on the drums track",
     [{"type": "select_clips_named", "name": "Drums", "clip_name": "Chorus"}]),
    ("select midi clips on Pads",
     [{"type": "select_clips_type", "name": "Pads", "clip_type": "midi"}]),
    ("highlight audio regions on Vocals",
     [{"type": "select_clips_type", "name": "Vocals", "clip_type": "audio"}]),
    ("select clips longer than 4 bars on Bass",
     [{"type": "select_clips_longer_than", "name": "Bass", "bars": 4}]),
    ("select all clips over 8 bars in the drums track",
     [{"type": "select_clips_longer_than", "name": "Drums", "bars": 8}]),
    ("select clips shorter than 2 bars on Vocals",
     [{"type": "select_clips_shorter_than", "name": "Vocals", "bars": 2}]),
    ("select all clips under 1 bar in the pads track",
     [{"type": "select_clips_shorter_than", "name": "Pads", "bars": 1}]),
    ("select clips after bar 8 on Bass",
     [{"type": "select_clips_starting_after", "name": "Bass", "bar": 8}]),
    ("select clips starting after bar 4 in the drums track",
     [{"type": "select_clips_starting_after", "name": "Drums", "bar": 4}]),
    ("select clips before bar 16 on Vocals",
     [{"type": "select_clips_starting_before", "name": "Vocals", "bar": 16}]),
    ("highlight regions up to bar 8 on Pads",
     [{"type": "select_clips_starting_before", "name": "Pads", "bar": 8}]),

    # --- create_track with a type descriptor + distinct custom name (the
    #     descriptor is decorative; the name after called/named wins) ---
    ("set up a drum track and label it Punchy", [{"type": "create_track", "name": "Punchy"}]),
    ("make a synth lane called Rumble", [{"type": "create_track", "name": "Rumble"}]),

    # --- clip ops ---
    ("add a 4 bar clip to Bass", [{"type": "clip_new", "name": "Bass", "length_bars": 4}]),
    ("create a 2 bar clip on the drums track", [{"type": "clip_new", "name": "Drums", "length_bars": 2}]),
    ("rename the clip on Bass to Intro", [{"type": "clip_rename", "name": "Bass", "clip_name": "Intro"}]),
    ("rename the selected clips on the lead track to Chorus",
     [{"type": "clip_rename", "name": "Lead", "clip_name": "Chorus"}]),
    ("delete clip 2 on Bass", [{"type": "clip_delete", "name": "Bass", "index": 2}]),
    ("remove clip 0 on Drums", [{"type": "clip_delete", "name": "Drums", "index": 0}]),

    # --- track move ---
    ("move Bass to position 3", [{"type": "track_move", "name": "Bass", "index": 3}]),
    ("move the vocals track to slot 2", [{"type": "track_move", "name": "Vocals", "index": 2}]),

    # --- MIDI note ops ---
    ("delete all the notes on the bass track", [{"type": "notes_delete", "name": "Bass"}]),
    ("clear the notes on the drums track", [{"type": "notes_delete", "name": "Drums"}]),
    ("transpose the notes on Bass up 12 semitones",
     [{"type": "notes_transpose", "name": "Bass", "semitones": 12}]),
    ("shift the lead notes down 5 semitones",
     [{"type": "notes_transpose", "name": "Lead", "semitones": -5}]),
    ("set the note velocity on Bass to 100",
     [{"type": "notes_set_velocity", "name": "Bass", "value": 100}]),
    ("set the drums note velocity to 80",
     [{"type": "notes_set_velocity", "name": "Drums", "value": 80}]),
    ("set the note length on Bass to 0.5 beats",
     [{"type": "notes_resize", "name": "Bass", "length": 0.5}]),
    ("set the lead note length to 2 beats", [{"type": "notes_resize", "name": "Lead", "length": 2}]),
    ("quantize the notes on Bass to 16th notes",
     [{"type": "notes_quantize", "name": "Bass", "grid": 0.25}]),
    ("snap the drums notes to 8th notes", [{"type": "notes_quantize", "name": "Drums", "grid": 0.5}]),
    ("set the note pitch on Bass to C2", [{"type": "notes_set_pitch", "name": "Bass", "pitch": "C2"}]),
    ("set the lead notes to G3", [{"type": "notes_set_pitch", "name": "Lead", "pitch": "G3"}]),
    ("select all C4 notes on Bass", [{"type": "notes_select_pitch", "name": "Bass", "pitch": "C4"}]),
    ("select the E4 notes on the lead track",
     [{"type": "notes_select_pitch", "name": "Lead", "pitch": "E4"}]),
    ("select notes on Bass with velocity above 100",
     [{"type": "notes_select_velocity_above", "name": "Bass", "value": 100}]),
    ("select the drums notes quieter than 60",
     [{"type": "notes_select_velocity_below", "name": "Drums", "value": 60}]),

    # --- groove (timing/swing) ---
    ("use the MPC Swing groove at strength 0.5",
     [{"type": "groove_set", "template": "MPC Swing", "strength": 0.5}]),
    ("apply the Shuffle groove at strength 0.7",
     [{"type": "groove_set", "template": "Shuffle", "strength": 0.7}]),
    ("which grooves are available", [{"type": "groove_list"}]),
    ("show me the groove list", [{"type": "groove_list"}]),

    # --- create_rack (#1837) ---
    ("create a rack", [{"type": "create_rack", "name": ""}]),
    ("add a rack to the bass track", [{"type": "create_rack", "name": "Bass"}]),
    ("create a rack with @serum and @fm_0",
     [{"type": "create_rack", "name": "", "plugins": [A("serum"), A("fm_0")]}]),
    ("create a rack with @serum on drums",
     [{"type": "create_rack", "name": "Drums", "plugins": [A("serum")]}]),

    # --- multi-plugin add_plugin (one fx.add per plugin, no "rack" keyword) ---
    ("add @serum and @ott to the bass track",
     [{"type": "add_plugin", "name": "Bass", "plugin": A("serum")},
      {"type": "add_plugin", "name": "Bass", "plugin": A("ott")}]),

    # --- clip-select comparison operators (#1837 Part B) ---
    ("select clips up to 4 bars in the bass track",
     [{"type": "select_clips_length_at_most", "name": "Bass", "bars": 4}]),
    ("select clips at least 2 bars in the drums track",
     [{"type": "select_clips_length_at_least", "name": "Drums", "bars": 2}]),
    ("select clips exactly 4 bars in the bass track",
     [{"type": "select_clips_length_exactly", "name": "Bass", "bars": 4}]),
    ("select clips not named Intro on Pads",
     [{"type": "select_clips_not_named", "name": "Pads", "clip_name": "Intro"}]),
]


def main():
    out = os.path.join(os.path.dirname(__file__), "testset.jsonl")
    rows = [{"lang": "en", "input": nl, "output": dsl.render(actions), "actions": actions}
            for nl, actions in CASES]

    with open(out, "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    print(f"wrote {len(rows)} test cases (en) -> {out}")


if __name__ == "__main__":
    main()
