"""Template-based synthetic dataset generator: NL request -> MAGDA DSL.

v0 strategy (per the POC spec): templates first. Each generator samples a
structured intent, renders the canonical gold DSL from it (so labels are
correct by construction), and renders a natural-language paraphrase from a
template bank. Later phases can swap the NL side for an LLM teacher's
paraphrases while keeping the same gold DSL.

Usage:
    python -m dataset.generate --n 500 --seed 7 --out data/train.jsonl
"""
from __future__ import annotations

import argparse
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from magda_dsl import dsl, i18n, vocab  # noqa: E402

# ---------------------------------------------------------------------------
# Slot pools
# ---------------------------------------------------------------------------
TRACK_NAMES = [
    "Bass", "Drums", "Lead", "Pads", "Vocals", "Kick", "Snare", "Hats",
    "Reese Bass", "Sub Bass", "Pluck", "Arp", "Strings", "Brass", "Guitar",
    "Perc", "Keys", "Synth", "FX", "Sub", "Top Loop", "Vox", "Choir",
]
INSTRUMENTS = ["serum", "vital", "surge xt", "diva", "massive"]
EFFECTS = ["ott", "pro-q 3", "pro-c 2", "reverb", "delay", "eq", "compressor", "1176", "valhalla"]
COLOR_WORDS = list(vocab.COLORS.keys())


def _plugin_phrase_and_token(name: str):
    """Pick a display spelling for `name` and the token to emit."""
    token = vocab.resolve_plugin(name)
    return name, token


# ---------------------------------------------------------------------------
# Per-command generators -> (nl_text, actions)
# ---------------------------------------------------------------------------
def gen_create_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [
        f"create a {name.lower()} track",
        f"add a new {name.lower()} track",
        f"make a {name.lower()} track",
        f"new track called {name}",
        f"create a track named {name}",
    ]
    return r.choice(templates), [{"type": "create_track", "name": name}]


def gen_create_with_plugins(r: random.Random):
    name = r.choice(TRACK_NAMES)
    inst = r.choice(INSTRUMENTS)
    plugins = [inst]
    mentions = [inst]
    if r.random() < 0.6:
        fx = r.choice(EFFECTS)
        plugins.append(fx)
        mentions.append(fx)
    tokens = [vocab.resolve_plugin(p) for p in plugins]
    joined = " and ".join(mentions)
    templates = [
        f"create a {name.lower()} track with {joined}",
        f"make a {name.lower()} track and add {joined}",
        f"new {name.lower()} track with {joined}",
    ]
    return r.choice(templates), [{"type": "create_track", "name": name, "plugins": tokens}]


def gen_add_plugin(r: random.Random):
    name = r.choice(TRACK_NAMES)
    plug = r.choice(INSTRUMENTS + EFFECTS)
    token = vocab.resolve_plugin(plug)
    templates = [
        f"add {plug} to the {name.lower()} track",
        f"put {plug} on {name}",
        f"load {plug} on the {name.lower()} track",
        f"add a {plug} to {name}",
    ]
    return r.choice(templates), [{"type": "add_plugin", "name": name, "plugin": token}]


def gen_rename_track(r: random.Random):
    old = r.choice(TRACK_NAMES)
    new = r.choice([n for n in TRACK_NAMES if n != old])
    templates = [
        f"rename track {old} to {new}",
        f"rename the {old.lower()} track to {new}",
        f"change {old} to {new}",
    ]
    return r.choice(templates), [{"type": "rename_track", "name": old, "new_name": new}]


def gen_delete_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [
        f"delete the {name.lower()} track",
        f"remove track {name}",
        f"delete {name}",
    ]
    return r.choice(templates), [{"type": "delete_track", "name": name}]


def gen_mute_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [f"mute {name}", f"mute the {name.lower()} track", f"silence {name}"]
    return r.choice(templates), [{"type": "mute_track", "name": name}]


def gen_solo_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [f"solo {name}", f"solo the {name.lower()} track", f"isolate {name}"]
    return r.choice(templates), [{"type": "solo_track", "name": name}]


def gen_set_color(r: random.Random):
    name = r.choice(TRACK_NAMES)
    cw = r.choice(COLOR_WORDS)
    hexv = vocab.COLORS[cw]
    templates = [
        f"make {name} {cw}",
        f"color the {name.lower()} track {cw}",
        f"set {name} colour to {cw}",
        f"color code {name.lower()} {cw}",
    ]
    return r.choice(templates), [{"type": "set_track_color", "name": name, "colour": hexv}]


def gen_group_tracks(r: random.Random):
    n = r.randint(2, 4)
    start = r.randint(1, 4)
    ids = list(range(start, start + n))
    group = r.choice(["Drums", "Bass Bus", "Vocals", "Rhythm", "Synths", "Perc"])
    id_str = ", ".join(str(i) for i in ids[:-1]) + f" and {ids[-1]}"
    templates = [
        f"group tracks {id_str} as {group}",
        f"group tracks {id_str} into {group}",
        f"put tracks {id_str} in a group called {group}",
    ]
    return r.choice(templates), [{"type": "group_tracks", "ids": ids, "name": group}]


GENERATORS = [
    (gen_create_track, 0.18),
    (gen_create_with_plugins, 0.16),
    (gen_add_plugin, 0.18),
    (gen_rename_track, 0.10),
    (gen_delete_track, 0.08),
    (gen_mute_track, 0.08),
    (gen_solo_track, 0.06),
    (gen_set_color, 0.08),
    (gen_group_tracks, 0.08),
]


def make_english_example(r: random.Random):
    funcs = [g for g, _ in GENERATORS]
    weights = [w for _, w in GENERATORS]
    gen = r.choices(funcs, weights=weights, k=1)[0]
    nl, actions = gen(r)
    return {"lang": "en", "input": nl, "output": dsl.render(actions), "actions": actions}


def _load_inputs(path):
    """Return the set of `input` strings in a jsonl file (for leakage guard)."""
    out = set()
    if path and os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    out.add(json.loads(line)["input"])
    return out


def _write(path, rows):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")


def main():
    here = os.path.dirname(__file__)
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=500, help="number of ENGLISH procedural train examples")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--langs", default="en",
                    help="comma list. default en-only (FPGA target: small vocab = on-chip). "
                         "non-en locales emit their curated seed bank, e.g. --langs en,ja,ru,zh")
    ap.add_argument("--out", default=os.path.join(here, "..", "data", "train.jsonl"))
    ap.add_argument("--val", type=int, default=0,
                    help="hold out this many English examples into a disjoint val split")
    ap.add_argument("--val-out", default=os.path.join(here, "..", "data", "val.jsonl"))
    ap.add_argument("--exclude", default=os.path.join(here, "..", "eval", "testset.jsonl"),
                    help="jsonl whose inputs are kept OUT of train/val (leakage guard); '' to disable")
    args = ap.parse_args()

    langs = [l.strip() for l in args.langs.split(",") if l.strip()]
    r = random.Random(args.seed)

    # English inputs that must never appear in train/val (the eval test set).
    excluded = _load_inputs(args.exclude)
    n_excluded = 0

    # ---- English: procedural, deduped, leakage-guarded -------------------
    seen = set()
    en_rows = []
    if "en" in langs:
        target = args.n + args.val
        attempts = 0
        while len(en_rows) < target and attempts < target * 80:
            attempts += 1
            ex = make_english_example(r)
            if ex["input"] in seen:
                continue
            if ex["input"] in excluded:
                n_excluded += 1
                continue
            seen.add(ex["input"])
            en_rows.append(ex)

    # Split English into val (first --val) and train (rest), disjoint by input.
    val_rows = en_rows[:args.val]
    train_en = en_rows[args.val:]

    # ---- Other locales: curated seed banks -> train ----------------------
    train_rows = list(train_en)
    counts = {"en": len(train_en)}
    for lang in langs:
        if lang == "en":
            continue
        bank = [(nl, a) for nl, a in i18n.curated(lang) if nl not in excluded]
        for nl, actions in bank:
            train_rows.append({"lang": lang, "input": nl, "output": dsl.render(actions), "actions": actions})
        counts[lang] = len(bank)

    train_path = os.path.abspath(args.out)
    _write(train_path, train_rows)
    by_lang = ", ".join(f"{k}={v}" for k, v in counts.items())
    extra = f" [excluded {n_excluded} test-set inputs]" if excluded else ""
    print(f"train: {len(train_rows)} examples ({by_lang}){extra} -> {train_path}")

    if args.val:
        val_path = os.path.abspath(args.val_out)
        _write(val_path, val_rows)
        print(f"val:   {len(val_rows)} examples (en) -> {val_path}")


if __name__ == "__main__":
    main()
