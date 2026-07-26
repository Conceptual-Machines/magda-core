"""Build the OUT-OF-DISTRIBUTION evaluation set (#1847 step 1).

Why this exists
---------------
`eval/testset.jsonl` is authored from the same case list `dataset/generate.py`
builds its templates from, so scoring on it measures template recall, not
generalisation. The model reports ~98% there and roughly 9/14 on a quick sweep
of phrasings written without looking at the templates. This file makes that
second number reproducible and committed.

How these cases were authored
-----------------------------
From the *intent list* (`magda_dsl/vocab.py`) and the DSL semantics
(`magda_dsl/dsl.py`) only — never from `dataset/generate.py`. Each is written
the way someone types into a console in the middle of a session: lowercase,
contractions, filler ("can you", "gimme", "pls"), first person, hedges, the
verb that came to mind rather than the canonical one, the track named after
the fact ("... on the bass one"), and occasional typos.

`main()` asserts no input string appears in the training/val/in-distribution
data, and reports the token-level Jaccard similarity to the nearest training
row so "out of distribution" is a measured property rather than a claim.

Gold labels are rendered by `dsl.render`, exactly like the in-distribution set,
so the two are directly comparable.

    python -m eval.make_ood_testset
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
    """Plugin @alias surface -> DSL token: 'serum' -> '<serum>'."""
    return "<" + alias + ">"


# Each entry: (natural language, actions, tags). `tags` mark the OOD pressure
# a case applies, so failures can be sliced by cause rather than only by intent.
#
#   filler          politeness / hedging / first-person wrapper
#   unnamed         no track name -> targets the selection or a fresh track
#   colloquial      a verb the templates never use
#   unseen-alias    plugin alias absent from the training data
#   bare-plugin     plugin named without the @ sigil
#   typo            realistic misspelling / missing apostrophe
#   glued-unit      number and unit typed without a space ("-6db")
#   postposed       the track is named after the operation
#   multi-clause    two clauses, only one of which is the command
CASES = [
    # ---------------------------------------------------------------- tracks
    ("can you set me up a bass track",
     [{"type": "create_track", "name": "Bass"}], ["filler"]),
    ("chuck in a drums track",
     [{"type": "create_track", "name": "Drums"}], ["colloquial"]),
    ("gimme a track called Sub Bass",
     [{"type": "create_track", "name": "Sub Bass"}], ["colloquial"]),
    ("i need another vocals track pls",
     [{"type": "create_track", "name": "Vocals"}], ["filler", "typo"]),
    ("add a track and name it Krunchy Kick",
     [{"type": "create_track", "name": "Krunchy Kick"}], []),

    # ---- create_track carrying a device. The failures #1847 opens with. ----
    ("track with @fm_0",
     [{"type": "create_track", "name": "", "plugins": [A("fm_0")]}],
     ["unnamed", "unseen-alias"]),
    # "new track with @polysynth" verbatim was absorbed into the
    # in-distribution set (fdb165ba), so this paraphrases it to stay held out.
    ("just make a track with @polysynth",
     [{"type": "create_track", "name": "", "plugins": [A("polysynth")]}],
     ["unnamed", "unseen-alias", "filler"]),
    ("put @serum on a new track",
     [{"type": "create_track", "name": "", "plugins": [A("serum")]}],
     ["unnamed", "postposed"]),
    ("i want a track with @massive",
     [{"type": "create_track", "name": "", "plugins": [A("massive")]}],
     ["unnamed", "filler"]),
    ("make me a pads track running @diva",
     [{"type": "create_track", "name": "Pads", "plugins": [A("diva")]}],
     ["colloquial"]),
    ("start a lead track with @vital and @ott on it",
     [{"type": "create_track", "name": "Lead",
       "plugins": [A("vital"), A("ott")]}], ["colloquial"]),
    ("can i get a track with @valhalla_vintage_verb",
     [{"type": "create_track", "name": "", "plugins": [A("valhalla_vintage_verb")]}],
     ["unnamed", "filler"]),
    ("new track, @surge_xt on it",
     [{"type": "create_track", "name": "", "plugins": [A("surge_xt")]}],
     ["unnamed", "postposed"]),
    ("track with serum on it",
     [{"type": "create_track", "name": "", "plugins": [A("serum")]}],
     ["unnamed", "bare-plugin"]),

    # ------------------------------------------------------------------ rack
    ("make a rack", [{"type": "create_rack", "name": ""}], ["unnamed"]),
    ("i want a rack on drums",
     [{"type": "create_rack", "name": "Drums"}], ["filler"]),
    ("wrap @serum in a rack on the bass track",
     [{"type": "create_rack", "name": "Bass", "plugins": [A("serum")]}],
     ["colloquial"]),
    ("stick a rack with @ott and @eq on Vocals",
     [{"type": "create_rack", "name": "Vocals", "plugins": [A("ott"), A("eq")]}],
     ["colloquial"]),
    # Parallel chains: one chain per device, not both in series.
    ("i want @eq and @compressor running in parallel in a rack",
     [{"type": "create_rack_parallel", "name": "",
       "plugins": [A("eq"), A("compressor")]}], ["unnamed", "colloquial"]),

    # ----------------------------------------------------------- add_plugin
    ("slap @ott on Drums",
     [{"type": "add_plugin", "name": "Drums", "plugin": A("ott")}], ["colloquial"]),
    ("the bass needs @pro_q_3",
     [{"type": "add_plugin", "name": "Bass", "plugin": A("pro_q_3")}], ["colloquial"]),
    ("can you drop @1176 onto the vocals",
     [{"type": "add_plugin", "name": "Vocals", "plugin": A("1176")}], ["filler"]),
    ("i want @decapitator on the guitar track",
     [{"type": "add_plugin", "name": "Guitar", "plugin": A("decapitator")}], ["filler"]),
    ("add @tape_saturator to Keys",
     [{"type": "add_plugin", "name": "Keys", "plugin": A("tape_saturator")}],
     ["unseen-alias"]),
    ("chuck @reverb and @delay on the pads track",
     [{"type": "add_plugin", "name": "Pads", "plugin": A("reverb")},
      {"type": "add_plugin", "name": "Pads", "plugin": A("delay")}], ["colloquial"]),

    # --------------------------------------------------------------- rename
    ("call the bass track Reese instead",
     [{"type": "rename_track", "name": "Bass", "new_name": "Reese"}], ["colloquial"]),
    ("Drums should be called Acoustic Kit",
     [{"type": "rename_track", "name": "Drums", "new_name": "Acoustic Kit"}],
     ["colloquial"]),
    ("rename Keys, make it Rhodes",
     [{"type": "rename_track", "name": "Keys", "new_name": "Rhodes"}], ["multi-clause"]),

    # ------------------------------------------------------ delete/mute/solo
    ("get rid of the vocals track",
     [{"type": "delete_track", "name": "Vocals"}], ["colloquial"]),
    ("i dont need the perc track anymore, delete it",
     [{"type": "delete_track", "name": "Perc"}], ["typo", "multi-clause"]),

    # -------------------------------------------------------- volume/pan/col
    ("bring the bass down to -6db",
     [{"type": "set_track_volume", "name": "Bass", "volume_db": -6}],
     ["colloquial", "glued-unit"]),
    ("drums are too loud, take them to -4 dB",
     [{"type": "set_track_volume", "name": "Drums", "volume_db": -4}], ["multi-clause"]),
    ("push Keys up to 2 dB",
     [{"type": "set_track_volume", "name": "Keys", "volume_db": 2}], ["colloquial"]),
    ("pan the hats left",
     [{"type": "set_track_pan", "name": "Hats", "pan": -0.5}], []),
    ("put the guitar hard right",
     [{"type": "set_track_pan", "name": "Guitar", "pan": 1.0}], ["colloquial"]),
    ("put the bass back in the centre",
     [{"type": "set_track_pan", "name": "Bass", "pan": 0.0}], ["colloquial"]),
    ("make the vocals track pink",
     [{"type": "set_track_color", "name": "Vocals", "colour": C["pink"]}], []),
    ("can you colour Keys teal",
     [{"type": "set_track_color", "name": "Keys", "colour": C["teal"]}], ["filler"]),

    # ---------------------------------------------------------------- groups
    ("group tracks 2 3 and 4 under Drums",
     [{"type": "group_tracks", "ids": [2, 3, 4], "name": "Drums"}], ["colloquial"]),
    ("bundle tracks 1 and 2 into a group called Bus",
     [{"type": "group_tracks", "ids": [1, 2], "name": "Bus"}], ["colloquial"]),

    # ------------------------------------------------------- clip selection
    ("select everything on the bass track",
     [{"type": "select_all_clips", "name": "Bass"}], ["colloquial"]),
    ("grab all the clips on Drums and call them Loop",
     [{"type": "select_all_clips_rename", "name": "Drums", "new_name": "Loop"}],
     ["colloquial"]),
    ("select the clips named Verse on Keys",
     [{"type": "select_clips_named", "name": "Keys", "clip_name": "Verse"}], []),
    ("select the clips that arent called Intro on Pads",
     [{"type": "select_clips_not_named", "name": "Pads", "clip_name": "Intro"}], ["typo"]),
    ("only the audio clips on Vocals",
     [{"type": "select_clips_type", "name": "Vocals", "clip_type": "audio"}],
     ["colloquial"]),
    ("select anything longer than 8 bars on Bass",
     [{"type": "select_clips_longer_than", "name": "Bass", "bars": 8}], ["colloquial"]),
    ("clips under 2 bars on the drums track",
     [{"type": "select_clips_shorter_than", "name": "Drums", "bars": 2}], ["colloquial"]),
    ("clips at least 4 bars on Bass",
     [{"type": "select_clips_length_at_least", "name": "Bass", "bars": 4}], ["colloquial"]),
    ("select clips no longer than 2 bars on Drums",
     [{"type": "select_clips_length_at_most", "name": "Drums", "bars": 2}], []),
    ("just the 4 bar clips on Keys",
     [{"type": "select_clips_length_exactly", "name": "Keys", "bars": 4}], ["colloquial"]),
    ("select clips that start at bar 32 or later on Keys",
     [{"type": "select_clips_starting_after", "name": "Keys", "bar": 32}], []),
    ("everything before bar 8 on Pads",
     [{"type": "select_clips_starting_before", "name": "Pads", "bar": 8}], ["colloquial"]),

    # -------------------------------------------------------------- clip ops
    ("put an 8 bar clip on Bass",
     [{"type": "clip_new", "name": "Bass", "length_bars": 8}], ["colloquial"]),
    ("new 4 bar clip on the drums track",
     [{"type": "clip_new", "name": "Drums", "length_bars": 4}], []),
    ("call that clip Intro on Keys",
     [{"type": "clip_rename", "name": "Keys", "clip_name": "Intro"}], ["colloquial"]),
    ("nuke clip 3 on Vocals",
     [{"type": "clip_delete", "name": "Vocals", "index": 3}], ["colloquial"]),

    # ------------------------------------------------------------ track move
    ("move the vocals up to position 2",
     [{"type": "track_move", "name": "Vocals", "index": 2}], []),
    ("drag Keys to slot 5",
     [{"type": "track_move", "name": "Keys", "index": 5}], ["colloquial"]),

    # ------------------------------------------------------------- note ops
    ("wipe the notes on the lead track",
     [{"type": "notes_delete", "name": "Lead"}], ["colloquial"]),
    ("drop the bass notes 12 semitones",
     [{"type": "notes_transpose", "name": "Bass", "semitones": -12}], ["colloquial"]),
    ("bump the drums velocity to 110",
     [{"type": "notes_set_velocity", "name": "Drums", "value": 110}], ["colloquial"]),
    ("make the lead notes 1 beat long",
     [{"type": "notes_resize", "name": "Lead", "length": 1}], ["colloquial"]),
    ("quantize the keys to 16ths",
     [{"type": "notes_quantize", "name": "Keys", "grid": 0.25}], []),
    ("set every note on the bass to C1",
     [{"type": "notes_set_pitch", "name": "Bass", "pitch": "C1"}], ["colloquial"]),
    ("select every C3 on the bass track",
     [{"type": "notes_select_pitch", "name": "Bass", "pitch": "C3"}], ["colloquial"]),
    ("anything above 100 velocity on Drums",
     [{"type": "notes_select_velocity_above", "name": "Drums", "value": 100}],
     ["colloquial"]),
    ("notes quieter than 40 on Drums",
     [{"type": "notes_select_velocity_below", "name": "Drums", "value": 40}],
     ["colloquial"]),

    # --------------------------------------------------------------- groove
    ("put the Shuffle groove on at 0.4 strength",
     [{"type": "groove_set", "template": "Shuffle", "strength": 0.4}], ["colloquial"]),
]

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)


def _corpus_inputs():
    """Every input string the model was trained or previously scored on."""
    seen = {}
    for rel in ("data/train.jsonl", "data/val.jsonl", "eval/testset.jsonl"):
        path = os.path.join(POC, rel)
        if not os.path.exists(path):
            continue
        rows = [json.loads(l) for l in open(path, encoding="utf-8") if l.strip()]
        seen[rel] = [r["input"] for r in rows]
    return seen


def _jaccard(a: set, b: set) -> float:
    return len(a & b) / len(a | b) if (a or b) else 0.0


def main():
    out = os.path.join(HERE, "ood_testset.jsonl")
    rows = [{"lang": "en", "input": nl, "output": dsl.render(actions),
             "actions": actions, "ood_tags": tags}
            for nl, actions, tags in CASES]

    # --- guard: nothing here may appear verbatim in train/val/test ---------
    corpus = _corpus_inputs()
    flat = {i.lower() for inputs in corpus.values() for i in inputs}
    dupes = [r["input"] for r in rows if r["input"].lower() in flat]
    if dupes:
        sys.exit(f"ERROR: {len(dupes)} case(s) already appear in the corpus: {dupes[:5]}")

    with open(out, "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    # --- measure the distance from the training distribution ---------------
    train = [set(t.lower() for t in tokenize(i))
             for i in corpus.get("data/train.jsonl", [])]
    sims = []
    for r in rows:
        toks = set(t.lower() for t in tokenize(r["input"]))
        sims.append(max((_jaccard(toks, t) for t in train), default=0.0))
    mean_sim = sum(sims) / len(sims) if sims else 0.0
    identical = sum(1 for s in sims if s == 1.0)

    intents = {a["type"] for r in rows for a in r["actions"]}
    print(f"wrote {len(rows)} OOD cases -> {out}")
    print(f"  intents covered: {len(intents)}")
    print(f"  nearest-train token Jaccard: mean={mean_sim:.2f} "
          f"max={max(sims, default=0):.2f}  (token-identical rows: {identical})")


if __name__ == "__main__":
    main()
