"""Build key-relative token streams from downloaded corpora.

Pipeline per song:
  raw chords [(root_pc, quality)]  ->  infer key if unannotated
                                   ->  key-relative tokens "<interval>:<qual>"
                                   ->  collapse immediate repeats
Then leakage-guarded split into train/val/test JSONL under data/.

Usage:
  python -m dataset.build                 # all available corpora in data/raw/
  python -m dataset.build --only billboard
"""
from __future__ import annotations

import argparse
import json
import os

from chords.roman import chord_to_token, infer_key, pc_histogram_from_chords
from dataset.ingest import INGESTORS
from dataset.sources import SOURCES

HERE = os.path.dirname(__file__)
RAW = os.path.join(HERE, "..", "data", "raw")
OUT = os.path.join(HERE, "..", "data")


def song_to_tokens(song):
    tonic_pc, mode = song["tonic_pc"], song["mode"]
    if tonic_pc is None:
        tonic_pc, mode = infer_key(pc_histogram_from_chords(song["chords"]))
    toks, prev = [], None
    for root_pc, qual in song["chords"]:
        t = chord_to_token(root_pc, qual, tonic_pc)
        if t != prev:            # collapse held/repeated chords
            toks.append(t)
        prev = t
    return toks, (mode or "maj")


def build(only=None, seed=7):
    import random
    rng = random.Random(seed)
    rows = []
    for src in SOURCES:
        if only and src.key != only:
            continue
        raw_dir = os.path.join(RAW, src.key)
        if not os.path.isdir(raw_dir):
            print(f"skip {src.key}: no data at {raw_dir} (see dataset/sources.py)")
            continue
        n = 0
        for song in INGESTORS[src.key](raw_dir):
            toks, mode = song_to_tokens(song)
            if len(toks) >= 4:
                rows.append({"id": f'{src.key}:{song["id"]}', "mode": mode, "tokens": toks})
                n += 1
        print(f"{src.key}: {n} songs")

    if not rows:
        print("no songs ingested — download corpora into data/raw/ first.")
        return
    rng.shuffle(rows)
    n = len(rows)
    n_val, n_test = max(1, n // 10), max(1, n // 10)
    splits = {"test": rows[:n_test], "val": rows[n_test:n_test + n_val],
              "train": rows[n_test + n_val:]}
    os.makedirs(OUT, exist_ok=True)
    for name, part in splits.items():
        with open(os.path.join(OUT, f"{name}.jsonl"), "w") as f:
            for r in part:
                f.write(json.dumps(r) + "\n")
        print(f"wrote {name}: {len(part)}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    build(**vars(ap.parse_args()))
