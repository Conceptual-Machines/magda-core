"""Build the DEV set — the held-out phrasing you ARE allowed to tune against.

Why a second one
----------------
`eval/ood_testset.jsonl` is the report set: it must stay sealed, because a set
you select checkpoints against stops predicting anything and starts describing
itself. But selection still has to happen, and val cannot do it — val is drawn
from the same templates as train, so it saturates at 100% and ranks checkpoints
by nothing. Across four encoder runs it kept the worse checkpoint three times.

So: two hand-authored sets, same authoring rules, disjoint sentences.

    eval/dev_testset.jsonl   tune, sweep, pick epochs and encoders against this
    eval/ood_testset.jsonl   open once, at the end, to report

Authored the same way as the OOD set — from the intent list and DSL semantics
only, never from `dataset/generate.py` — and `main()` asserts disjointness from
train, val, the in-distribution set AND the OOD set.

    python -m eval.make_dev_testset
"""
from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from dataset.tagging import tokenize  # noqa: E402
from magda_dsl import dsl, vocab  # noqa: E402

C = vocab.COLORS


def A(alias: str) -> str:
    return "<" + alias + ">"


# Same `ood_tags` vocabulary as make_ood_testset.py, so a weakness found here
# can be checked against the same slice there.
CASES = [
    # ---------------------------------------------------------------- tracks
    ("throw up a keys track", [{"type": "create_track", "name": "Keys"}], ["colloquial"]),
    ("i'd like a strings track",
     [{"type": "create_track", "name": "Strings"}], ["filler"]),
    ("new track, call it Gritty Sub",
     [{"type": "create_track", "name": "Gritty Sub"}], ["multi-clause"]),
    ("could you add a perc track for me",
     [{"type": "create_track", "name": "Perc"}], ["filler"]),

    ("a track with @wavetable please",
     [{"type": "create_track", "name": "", "plugins": [A("wavetable")]}],
     ["unnamed", "filler", "unseen-alias"]),
    ("fresh track running @obxd",
     [{"type": "create_track", "name": "", "plugins": [A("obxd")]}],
     ["unnamed", "colloquial", "unseen-alias"]),
    ("set up a choir track with @valhalla and @pro_q_3",
     [{"type": "create_track", "name": "Choir",
       "plugins": [A("valhalla"), A("pro_q_3")]}], ["colloquial"]),
    ("@diva on a brand new track",
     [{"type": "create_track", "name": "", "plugins": [A("diva")]}],
     ["unnamed", "postposed"]),

    # ------------------------------------------------------------------ rack
    ("rack on the keys track", [{"type": "create_rack", "name": "Keys"}], ["colloquial"]),
    ("build me a rack with @decapitator",
     [{"type": "create_rack", "name": "", "plugins": [A("decapitator")]}],
     ["unnamed", "colloquial"]),

    # ----------------------------------------------------------- add_plugin
    ("shove @massive onto Strings",
     [{"type": "add_plugin", "name": "Strings", "plugin": A("massive")}], ["colloquial"]),
    ("the lead could use @chorus",
     [{"type": "add_plugin", "name": "Lead", "plugin": A("chorus")}], ["colloquial"]),
    ("please put @pro_c_2 on the drums track",
     [{"type": "add_plugin", "name": "Drums", "plugin": A("pro_c_2")}], ["filler"]),
    ("@filter and @phaser on Pads",
     [{"type": "add_plugin", "name": "Pads", "plugin": A("filter")},
      {"type": "add_plugin", "name": "Pads", "plugin": A("phaser")}], ["postposed"]),

    # --------------------------------------------------------------- rename
    ("change the name of Pads to Warm Air",
     [{"type": "rename_track", "name": "Pads", "new_name": "Warm Air"}], []),
    ("Guitar is now called Rhythm Gtr",
     [{"type": "rename_track", "name": "Guitar", "new_name": "Rhythm Gtr"}],
     ["colloquial"]),
    ("lets rename Sub to Sub Boom",
     [{"type": "rename_track", "name": "Sub", "new_name": "Sub Boom"}], ["typo"]),

    # ------------------------------------------------------ delete/mute/solo
    ("bin the strings track", [{"type": "delete_track", "name": "Strings"}], ["colloquial"]),
    ("can we lose Choir", [{"type": "delete_track", "name": "Choir"}], ["filler"]),

    # -------------------------------------------------------- volume/pan/col
    ("knock Strings back to -9 dB",
     [{"type": "set_track_volume", "name": "Strings", "volume_db": -9}], ["colloquial"]),
    ("pads at -15db thanks",
     [{"type": "set_track_volume", "name": "Pads", "volume_db": -15}],
     ["glued-unit", "filler"]),
    ("shove the perc hard left",
     [{"type": "set_track_pan", "name": "Perc", "pan": -1.0}], ["colloquial"]),
    ("Choir slightly right please",
     [{"type": "set_track_pan", "name": "Choir", "pan": 0.25}], ["filler"]),
    ("turn Lead orange",
     [{"type": "set_track_color", "name": "Lead", "colour": C["orange"]}], ["colloquial"]),
    ("i want Sub in yellow",
     [{"type": "set_track_color", "name": "Sub", "colour": C["yellow"]}], ["filler"]),

    # ---------------------------------------------------------------- groups
    ("stick tracks 5 and 6 in a group named Synths",
     [{"type": "group_tracks", "ids": [5, 6], "name": "Synths"}], ["colloquial"]),

    # ------------------------------------------------------- clip selection
    ("all the clips on Strings",
     [{"type": "select_all_clips", "name": "Strings"}], ["colloquial"]),
    ("select every clip on Perc and name them Hit",
     [{"type": "select_all_clips_rename", "name": "Perc", "new_name": "Hit"}], []),
    ("find the clips called Bridge on Lead",
     [{"type": "select_clips_named", "name": "Lead", "clip_name": "Bridge"}],
     ["colloquial"]),
    ("midi clips only on the keys track",
     [{"type": "select_clips_type", "name": "Keys", "clip_type": "midi"}], ["colloquial"]),
    ("anything over 16 bars on Pads",
     [{"type": "select_clips_longer_than", "name": "Pads", "bars": 16}], ["colloquial"]),
    ("clips beneath 4 bars on Sub",
     [{"type": "select_clips_shorter_than", "name": "Sub", "bars": 4}], ["colloquial"]),
    ("select clips from bar 12 onwards on Perc",
     [{"type": "select_clips_starting_after", "name": "Perc", "bar": 12}], []),

    # -------------------------------------------------------------- clip ops
    ("stick a 16 bar clip on Choir",
     [{"type": "clip_new", "name": "Choir", "length_bars": 16}], ["colloquial"]),
    ("that clip on Sub should be called Drop",
     [{"type": "clip_rename", "name": "Sub", "clip_name": "Drop"}], ["colloquial"]),
    ("bin clip 5 on Strings",
     [{"type": "clip_delete", "name": "Strings", "index": 5}], ["colloquial"]),
    ("shift Perc to position 7",
     [{"type": "track_move", "name": "Perc", "index": 7}], ["colloquial"]),

    # ------------------------------------------------------------- note ops
    ("clear out the notes on Choir",
     [{"type": "notes_delete", "name": "Choir"}], ["colloquial"]),
    ("lift the keys notes 7 semitones",
     [{"type": "notes_transpose", "name": "Keys", "semitones": 7}], ["colloquial"]),
    ("velocity on Strings to 64",
     [{"type": "notes_set_velocity", "name": "Strings", "value": 64}], ["colloquial"]),
    ("snap Perc to quarter notes",
     [{"type": "notes_quantize", "name": "Perc", "grid": 1.0}], ["colloquial"]),
    ("grab the D4 notes on Lead",
     [{"type": "notes_select_pitch", "name": "Lead", "pitch": "D4"}], ["colloquial"]),
    ("notes over 90 on Choir",
     [{"type": "notes_select_velocity_above", "name": "Choir", "value": 90}],
     ["colloquial"]),

    # --------------------------------------------------------------- groove
]

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)


def _seen_inputs():
    seen = set()
    for rel in ("data/train.jsonl", "data/val.jsonl",
                "eval/testset.jsonl", "eval/ood_testset.jsonl"):
        path = os.path.join(POC, rel)
        if os.path.exists(path):
            for line in open(path, encoding="utf-8"):
                if line.strip():
                    seen.add(json.loads(line)["input"].lower())
    return seen


def main():
    out = os.path.join(HERE, "dev_testset.jsonl")
    rows = [{"lang": "en", "input": nl, "output": dsl.render(actions),
             "actions": actions, "ood_tags": tags}
            for nl, actions, tags in CASES]

    dupes = [r["input"] for r in rows if r["input"].lower() in _seen_inputs()]
    if dupes:
        sys.exit(f"ERROR: {len(dupes)} case(s) overlap an existing set: {dupes[:5]}")

    with open(out, "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    intents = {a["type"] for r in rows for a in r["actions"]}
    print(f"wrote {len(rows)} dev cases -> {out}")
    print(f"  intents covered: {len(intents)}")
    print("  NOTE: tune against this one. eval/ood_testset.jsonl stays sealed.")


if __name__ == "__main__":
    main()
