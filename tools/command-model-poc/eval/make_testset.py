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

from magda_dsl import dsl, i18n, vocab  # noqa: E402

T = vocab.resolve_plugin  # text -> token
C = vocab.COLORS

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

    # --- create_track with plugins ---
    ("create a bass track with serum and ott",
     [{"type": "create_track", "name": "Bass", "plugins": [T("serum"), T("ott")]}]),
    ("make a lead track with serum",
     [{"type": "create_track", "name": "Lead", "plugins": [T("serum")]}]),
    ("new pads track with vital and reverb",
     [{"type": "create_track", "name": "Pads", "plugins": [T("vital"), T("reverb")]}]),
    ("create a vocals track with pro-q 3",
     [{"type": "create_track", "name": "Vocals", "plugins": [T("pro-q 3")]}]),
    ("create a drums track with surge xt and ott",
     [{"type": "create_track", "name": "Drums", "plugins": [T("surge xt"), T("ott")]}]),

    # --- add_plugin ---
    ("add serum to the bass track", [{"type": "add_plugin", "name": "Bass", "plugin": T("serum")}]),
    ("put ott on Drums", [{"type": "add_plugin", "name": "Drums", "plugin": T("ott")}]),
    ("add pro-q 3 to vocals", [{"type": "add_plugin", "name": "Vocals", "plugin": T("pro-q 3")}]),
    ("load vital on the lead track", [{"type": "add_plugin", "name": "Lead", "plugin": T("vital")}]),
    ("add an eq to the bass track", [{"type": "add_plugin", "name": "Bass", "plugin": "eq"}]),
    ("add reverb to Pads", [{"type": "add_plugin", "name": "Pads", "plugin": "reverb"}]),
    ("add a compressor to the drums track", [{"type": "add_plugin", "name": "Drums", "plugin": "compressor"}]),
    ("put 1176 on the vocals track", [{"type": "add_plugin", "name": "Vocals", "plugin": T("1176")}]),

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

    # --- mute_track ---
    ("mute Drums", [{"type": "mute_track", "name": "Drums"}]),
    ("mute the bass track", [{"type": "mute_track", "name": "Bass"}]),
    ("silence Pads", [{"type": "mute_track", "name": "Pads"}]),
    ("mute the vocals track", [{"type": "mute_track", "name": "Vocals"}]),

    # --- solo_track ---
    ("solo Drums", [{"type": "solo_track", "name": "Drums"}]),
    ("solo the lead track", [{"type": "solo_track", "name": "Lead"}]),
    ("isolate Bass", [{"type": "solo_track", "name": "Bass"}]),

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
]


# English-only by default (FPGA target: a small English vocab fits on-chip;
# multilingual would blow up the embedding table). Flip INCLUDE_NON_EN=True to
# also score ja/ru/zh from the parked curated seed banks.
INCLUDE_NON_EN = False
NON_EN_PER_LANG = 6


def main():
    out = os.path.join(os.path.dirname(__file__), "testset.jsonl")
    rows = []
    for nl, actions in CASES:
        rows.append({"lang": "en", "input": nl, "output": dsl.render(actions), "actions": actions})
    if INCLUDE_NON_EN:
        for lang in ("ja", "ru", "zh"):
            for nl, actions in i18n.curated(lang)[-NON_EN_PER_LANG:]:
                rows.append({"lang": lang, "input": nl, "output": dsl.render(actions), "actions": actions})

    with open(out, "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    by_lang = {}
    for r in rows:
        by_lang[r["lang"]] = by_lang.get(r["lang"], 0) + 1
    summary = ", ".join(f"{k}={v}" for k, v in by_lang.items())
    print(f"wrote {len(rows)} test cases ({summary}) -> {out}")


if __name__ == "__main__":
    main()
